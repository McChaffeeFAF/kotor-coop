/* Copyright (c) 2026 The reone project contributors */

#include "reone/net/protocol.h"

#include <cctype>
#include <cmath>

#include "reone/net/bytes.h"

namespace reone {

namespace net {

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;

// Replication tolerances: below these a change is not worth a byte.
constexpr float kPositionEpsilon = 0.001f;

ByteWriter begin(MessageType type) {
    ByteWriter w;
    w.u8(static_cast<uint8_t>(type));
    return w;
}

bool expect(ByteReader &r, MessageType type) {
    return r.u8() == static_cast<uint8_t>(type) && r.ok();
}

bool finish(const ByteReader &r) {
    return r.ok() && r.atEnd();
}

void writeVec3(ByteWriter &w, const Vec3 &v) {
    w.f32(v.x);
    w.f32(v.y);
    w.f32(v.z);
}

Vec3 readVec3(ByteReader &r) {
    Vec3 v;
    v.x = r.f32();
    v.y = r.f32();
    v.z = r.f32();
    return v;
}

void writeFingerprint(ByteWriter &w, const Fingerprint &f) {
    w.boolean(f.present);
    w.u64(f.key);
    w.u64(f.modules);
    w.u64(f.tlk);
}

Fingerprint readFingerprint(ByteReader &r) {
    Fingerprint f;
    f.present = r.boolean();
    f.key = r.u64();
    f.modules = r.u64();
    f.tlk = r.u64();
    return f;
}

bool validRejectReason(uint8_t value) {
    return value >= static_cast<uint8_t>(RejectReason::ProtocolMismatch) &&
           value <= static_cast<uint8_t>(RejectReason::Kicked);
}

bool validSlotStatus(uint8_t value) {
    return value >= static_cast<uint8_t>(SlotStatus::Assigned) &&
           value <= static_cast<uint8_t>(SlotStatus::Guest);
}

bool validCommandType(uint8_t value) {
    return value >= static_cast<uint8_t>(CommandType::MoveTo) &&
           value <= static_cast<uint8_t>(CommandType::DisarmTrap);
}

bool validCommandReject(uint8_t value) {
    return value <= static_cast<uint8_t>(CommandReject::ClassSlotsFull);
}

bool validEventKind(uint8_t value) {
    return value >= static_cast<uint8_t>(EventKind::VfxBegin) &&
           value <= static_cast<uint8_t>(EventKind::Projectile);
}

void writeEntity(ByteWriter &w, const EntityRecord &record) {
    const auto &s = record.state;
    uint16_t f = record.fields;
    w.u32(s.id);
    w.u16(f);
    if (f & kFieldIdentity) {
        w.u8(s.type);
        w.str(s.key);
        w.str(s.tag);
        w.str(s.resref);
    }
    if (f & kFieldPosition) {
        writeVec3(w, s.position);
    }
    if (f & kFieldFacing) {
        w.u16(quantizeFacing(s.facing));
    }
    if (f & kFieldAnim) {
        w.str(s.anim);
        w.f32(s.animTime);
        w.u16(s.animFlags);
        w.f32(s.animSpeed);
        w.u8(s.animSerial);
    }
    if (f & kFieldVitals) {
        w.i16(s.hp);
        w.i16(s.maxHp);
        w.boolean(s.dead);
    }
    if (f & kFieldWeapons) {
        w.str(s.rightWeapon);
        w.str(s.leftWeapon);
        w.str(s.armor);
        w.str(s.headgear);
    }
    if (f & kFieldOpenLock) {
        w.u8(static_cast<uint8_t>((s.open ? 1 : 0) | (s.locked ? 2 : 0)));
    }
    if (f & kFieldDoor) {
        w.u8(s.doorState);
        w.u8(s.doorTransition);
    }
    if (f & kFieldFlags) {
        w.u8(s.flags);
    }
    if (f & kFieldTarget) {
        w.u32(s.attackTarget);
    }
}

bool readEntity(ByteReader &r, EntityRecord &record) {
    auto &s = record.state;
    s.id = r.u32();
    uint16_t f = r.u16();
    if (!r.ok() || (f & ~kFieldAllMask) != 0 || s.id == 0) {
        r.fail();
        return false;
    }
    record.fields = f;
    if (f & kFieldIdentity) {
        s.type = r.u8();
        s.key = r.str();
        s.tag = r.str();
        s.resref = r.str();
    }
    if (f & kFieldPosition) {
        s.position = readVec3(r);
    }
    if (f & kFieldFacing) {
        s.facing = dequantizeFacing(r.u16());
    }
    if (f & kFieldAnim) {
        s.anim = r.str();
        s.animTime = r.f32();
        s.animFlags = r.u16();
        s.animSpeed = r.f32();
        s.animSerial = r.u8();
        if (s.animTime < 0.0f || s.animTime > 3600.0f || s.animSpeed < 0.0f || s.animSpeed > 100.0f) {
            r.fail();
        }
    }
    if (f & kFieldVitals) {
        s.hp = r.i16();
        s.maxHp = r.i16();
        s.dead = r.boolean();
    }
    if (f & kFieldWeapons) {
        s.rightWeapon = r.str();
        s.leftWeapon = r.str();
        s.armor = r.str();
        s.headgear = r.str();
    }
    if (f & kFieldOpenLock) {
        uint8_t bits = r.u8();
        if (bits > 3) {
            r.fail();
        }
        s.open = (bits & 1) != 0;
        s.locked = (bits & 2) != 0;
    }
    if (f & kFieldDoor) {
        s.doorState = r.u8();
        s.doorTransition = r.u8();
        if (s.doorState > 3 || s.doorTransition > 2) {
            r.fail();
        }
    }
    if (f & kFieldFlags) {
        s.flags = r.u8();
        if (s.flags > 0x3f) {
            r.fail();
        }
    }
    if (f & kFieldTarget) {
        s.attackTarget = r.u32();
    }
    return r.ok();
}

void writeWorld(ByteWriter &w, uint16_t fields, const WorldState &s) {
    w.u16(fields);
    if (fields & kWorldScreen) {
        w.u8(s.screen);
    }
    if (fields & kWorldFade) {
        w.f32(s.fadeOpacity);
        writeVec3(w, s.fadeColor);
    }
    if (fields & kWorldMovie) {
        w.boolean(s.movie);
        w.str(s.movieName);
    }
    if (fields & kWorldCamera) {
        w.u8(s.cameraType);
        w.u32(s.cameraId);
    }
    if (fields & kWorldConversation) {
        const auto &c = s.conversation;
        w.boolean(c.active);
        w.u8(c.kind);
        w.str(c.dialog);
        w.i32(c.entry);
        w.u32(c.owner);
        w.str(c.speaker);
        w.u16(static_cast<uint16_t>(c.replies.size()));
        for (auto reply : c.replies) {
            w.i32(reply);
        }
        w.boolean(c.entryEnded);
    }
    if (fields & kWorldParty) {
        w.u8(static_cast<uint8_t>(s.party.size()));
        for (const auto &entry : s.party) {
            w.u8(static_cast<uint8_t>(entry.npc));
            w.u32(entry.id);
        }
        w.u32(s.leader);
    }
    if (fields & kWorldPause) {
        w.boolean(s.paused);
        w.str(s.pausedBy);
    }
}

bool readWorld(ByteReader &r, uint16_t &fields, WorldState &s) {
    fields = r.u16();
    if ((fields & ~kWorldAllMask) != 0) {
        r.fail();
        return false;
    }
    if (fields & kWorldScreen) {
        s.screen = r.u8();
    }
    if (fields & kWorldFade) {
        s.fadeOpacity = r.f32();
        s.fadeColor = readVec3(r);
        if (s.fadeOpacity < 0.0f || s.fadeOpacity > 1.0f) {
            r.fail();
        }
    }
    if (fields & kWorldMovie) {
        s.movie = r.boolean();
        s.movieName = r.str();
    }
    if (fields & kWorldCamera) {
        s.cameraType = r.u8();
        s.cameraId = r.u32();
    }
    if (fields & kWorldConversation) {
        auto &c = s.conversation;
        c.active = r.boolean();
        c.kind = r.u8();
        c.dialog = r.str();
        c.entry = r.i32();
        c.owner = r.u32();
        c.speaker = r.str();
        uint16_t count = r.u16();
        if (count > kMaxReplies || c.kind > 1) {
            r.fail();
            return false;
        }
        c.replies.clear();
        for (uint16_t i = 0; i < count && r.ok(); ++i) {
            c.replies.push_back(r.i32());
        }
        c.entryEnded = r.boolean();
    }
    if (fields & kWorldParty) {
        uint8_t count = r.u8();
        if (count > kMaxPartyEntries) {
            r.fail();
            return false;
        }
        s.party.clear();
        for (uint8_t i = 0; i < count && r.ok(); ++i) {
            PartyEntry entry;
            entry.npc = static_cast<int8_t>(r.u8());
            entry.id = r.u32();
            s.party.push_back(entry);
        }
        s.leader = r.u32();
    }
    if (fields & kWorldPause) {
        s.paused = r.boolean();
        s.pausedBy = r.str();
    }
    return r.ok();
}

bool validText(const std::string &text) {
    for (char c : text) {
        if (static_cast<unsigned char>(c) < 0x20) {
            return false;
        }
    }
    return true;
}

bool validResRef(const std::string &text) {
    if (text.empty() || text.size() > 16) {
        return false;
    }
    for (char c : text) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

} // namespace

bool isClientMessage(MessageType type) {
    switch (type) {
    case MessageType::Hello:
    case MessageType::SlotRequest:
    case MessageType::Command:
    case MessageType::Bye:
    case MessageType::SnapshotAck:
    case MessageType::GuestCharacter:
        return true;
    default:
        return false;
    }
}

bool isHostMessage(MessageType type) {
    switch (type) {
    case MessageType::Welcome:
    case MessageType::Reject:
    case MessageType::SlotAssign:
    case MessageType::ModuleInfo:
    case MessageType::Snapshot:
    case MessageType::CommandResult:
    case MessageType::Bye:
    case MessageType::Notice:
    case MessageType::ContainerView:
    case MessageType::ReadyCheck:
    case MessageType::Events:
    case MessageType::SharedInventory:
    case MessageType::CharacterSheet:
    case MessageType::Bark:
    case MessageType::GuestCharacter:
    case MessageType::StoreView:
    case MessageType::GuestRoster:
    case MessageType::Offer:
        return true;
    default:
        return false;
    }
}

std::optional<MessageType> peekType(const std::vector<uint8_t> &bytes) {
    if (bytes.empty()) {
        return std::nullopt;
    }
    uint8_t value = bytes.front();
    bool completeness = value >= static_cast<uint8_t>(MessageType::SharedInventory) &&
                        value <= static_cast<uint8_t>(MessageType::Offer);
    if (!completeness && (value < static_cast<uint8_t>(MessageType::Hello) || value > static_cast<uint8_t>(MessageType::Events))) {
        return std::nullopt;
    }
    return static_cast<MessageType>(value);
}

const char *messageTypeName(MessageType type) {
    switch (type) {
    case MessageType::Hello:
        return "hello";
    case MessageType::Welcome:
        return "welcome";
    case MessageType::Reject:
        return "reject";
    case MessageType::SlotRequest:
        return "slot_request";
    case MessageType::SlotAssign:
        return "slot_assign";
    case MessageType::ModuleInfo:
        return "module_info";
    case MessageType::Snapshot:
        return "snapshot";
    case MessageType::Command:
        return "command";
    case MessageType::CommandResult:
        return "command_result";
    case MessageType::Bye:
        return "bye";
    case MessageType::Notice:
        return "notice";
    case MessageType::ContainerView:
        return "container_view";
    case MessageType::ReadyCheck:
        return "ready_check";
    case MessageType::SnapshotAck:
        return "snapshot_ack";
    case MessageType::Events:
        return "events";
    case MessageType::SharedInventory:
        return "shared_inventory";
    case MessageType::CharacterSheet:
        return "character_sheet";
    case MessageType::Bark:
        return "bark";
    case MessageType::GuestCharacter:
        return "guest_character";
    case MessageType::StoreView:
        return "store_view";
    case MessageType::GuestRoster:
        return "guest_roster";
    case MessageType::Offer:
        return "offer";
    }
    return "unknown";
}

const char *rejectReasonName(RejectReason reason) {
    switch (reason) {
    case RejectReason::ProtocolMismatch:
        return "protocol_mismatch";
    case RejectReason::BuildMismatch:
        return "build_mismatch";
    case RejectReason::FilesMismatchK1:
        return "files_mismatch_k1";
    case RejectReason::FilesMismatchK2:
        return "files_mismatch_k2";
    case RejectReason::ModeMismatch:
        return "mode_mismatch";
    case RejectReason::SessionFull:
        return "session_full";
    case RejectReason::Declined:
        return "declined";
    case RejectReason::Malformed:
        return "malformed";
    case RejectReason::Kicked:
        return "kicked";
    }
    return "unknown";
}

const char *slotStatusName(SlotStatus status) {
    switch (status) {
    case SlotStatus::Assigned:
        return "assigned";
    case SlotStatus::Waiting:
        return "waiting";
    case SlotStatus::Released:
        return "released";
    case SlotStatus::Declined:
        return "declined";
    case SlotStatus::StandIn:
        return "standin";
    case SlotStatus::Guest:
        return "guest";
    }
    return "unknown";
}

const char *commandTypeName(CommandType type) {
    switch (type) {
    case CommandType::MoveTo:
        return "move_to";
    case CommandType::Attack:
        return "attack";
    case CommandType::UseObject:
        return "use_object";
    case CommandType::OpenDoor:
        return "open_door";
    case CommandType::Cancel:
        return "cancel";
    case CommandType::UseItem:
        return "use_item";
    case CommandType::UseFeat:
        return "use_feat";
    case CommandType::UsePower:
        return "use_power";
    case CommandType::UseSkill:
        return "use_skill";
    case CommandType::Ready:
        return "ready";
    case CommandType::Pause:
        return "pause";
    case CommandType::TakeItem:
        return "take_item";
    case CommandType::CloseContainer:
        return "close_container";
    case CommandType::PickSlot:
        return "pick_slot";
    case CommandType::Equip:
        return "equip";
    case CommandType::LevelUp:
        return "level_up";
    case CommandType::StoreBuy:
        return "store_buy";
    case CommandType::StoreSell:
        return "store_sell";
    case CommandType::StoreClose:
        return "store_close";
    case CommandType::OfferAnswer:
        return "offer_answer";
    case CommandType::PazaakResult:
        return "pazaak_result";
    case CommandType::PickReply:
        return "pick_reply";
    case CommandType::SwitchWeapons:
        return "switch_weapons";
    case CommandType::AutoCombat:
        return "auto_combat";
    case CommandType::SetMine:
        return "set_mine";
    case CommandType::DisarmTrap:
        return "disarm_trap";
    }
    return "unknown";
}

const char *commandRejectName(CommandReject reason) {
    switch (reason) {
    case CommandReject::None:
        return "none";
    case CommandReject::NoSlot:
        return "no_slot";
    case CommandReject::NotControlled:
        return "not_controlled";
    case CommandReject::ActorUnavailable:
        return "actor_unavailable";
    case CommandReject::TargetInvalid:
        return "target_invalid";
    case CommandReject::TargetTooFar:
        return "target_too_far";
    case CommandReject::PointInvalid:
        return "point_invalid";
    case CommandReject::Busy:
        return "busy";
    case CommandReject::RateLimited:
        return "rate_limited";
    case CommandReject::NotSupported:
        return "not_supported";
    case CommandReject::ItemUnavailable:
        return "item_unavailable";
    case CommandReject::FeatUnavailable:
        return "feat_unavailable";
    case CommandReject::PowerUnavailable:
        return "power_unavailable";
    case CommandReject::SkillUnavailable:
        return "skill_unavailable";
    case CommandReject::HostLeadsConversation:
        return "host_leads_conversation";
    case CommandReject::StoreUnavailable:
        return "store_unavailable";
    case CommandReject::PauseDisabled:
        return "pause_disabled";
    case CommandReject::PauseCooldown:
        return "pause_cooldown";
    case CommandReject::NoReadyCheck:
        return "no_ready_check";
    case CommandReject::HostOnlyTransitions:
        return "host_only_transitions";
    case CommandReject::ContainerNotOpen:
        return "container_not_open";
    case CommandReject::LevelUpUnavailable:
        return "level_up_unavailable";
    case CommandReject::LevelUpInvalid:
        return "level_up_invalid";
    case CommandReject::EquipRejected:
        return "equip_rejected";
    case CommandReject::NotHandedOver:
        return "not_handed_over";
    case CommandReject::NotEnoughCredits:
        return "not_enough_credits";
    case CommandReject::StoreClosed:
        return "store_closed";
    case CommandReject::NoOffer:
        return "no_offer";
    case CommandReject::GuestsCannotRace:
        return "guests_cannot_race";
    case CommandReject::NotYourTerminal:
        return "not_your_terminal";
    case CommandReject::ClassLocked:
        return "class_locked";
    case CommandReject::ClassAlignment:
        return "class_alignment";
    case CommandReject::ClassSlotsFull:
        return "class_slots_full";
    }
    return "unknown";
}

// Hello

std::vector<uint8_t> encode(const Hello &m) {
    auto w = begin(MessageType::Hello);
    w.u16(m.protocol);
    w.str(m.build);
    w.str(m.mode);
    w.str(m.playerName);
    writeFingerprint(w, m.k1);
    writeFingerprint(w, m.k2);
    w.u64(m.rejoinToken);
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, Hello &out) {
    if (bytes.size() > kMaxClientMessageBytes) {
        return false;
    }
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::Hello)) {
        return false;
    }
    out.protocol = r.u16();
    out.build = r.str();
    out.mode = r.str();
    out.playerName = r.str();
    out.k1 = readFingerprint(r);
    out.k2 = readFingerprint(r);
    out.rejoinToken = r.u64();
    return finish(r);
}

// Welcome

std::vector<uint8_t> encode(const Welcome &m) {
    auto w = begin(MessageType::Welcome);
    w.u32(m.clientId);
    w.u32(m.hostTick);
    w.u16(m.tickRate);
    w.str(m.mode);
    w.u64(m.rejoinToken);
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, Welcome &out) {
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::Welcome)) {
        return false;
    }
    out.clientId = r.u32();
    out.hostTick = r.u32();
    out.tickRate = r.u16();
    out.mode = r.str();
    out.rejoinToken = r.u64();
    return finish(r) && out.tickRate >= 1 && out.tickRate <= 1000;
}

// Reject

std::vector<uint8_t> encode(const Reject &m) {
    auto w = begin(MessageType::Reject);
    w.u8(static_cast<uint8_t>(m.reason));
    w.str(m.detail);
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, Reject &out) {
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::Reject)) {
        return false;
    }
    uint8_t reason = r.u8();
    out.detail = r.str();
    if (!validRejectReason(reason)) {
        return false;
    }
    out.reason = static_cast<RejectReason>(reason);
    return finish(r);
}

// SlotRequest

std::vector<uint8_t> encode(const SlotRequest &m) {
    auto w = begin(MessageType::SlotRequest);
    w.str(m.preferredTag);
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, SlotRequest &out) {
    if (bytes.size() > kMaxClientMessageBytes) {
        return false;
    }
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::SlotRequest)) {
        return false;
    }
    out.preferredTag = r.str();
    return finish(r);
}

// SlotAssign

std::vector<uint8_t> encode(const SlotAssign &m) {
    auto w = begin(MessageType::SlotAssign);
    w.u8(static_cast<uint8_t>(m.status));
    w.i32(m.npc);
    w.u32(m.creatureId);
    w.str(m.tag);
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, SlotAssign &out) {
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::SlotAssign)) {
        return false;
    }
    uint8_t status = r.u8();
    out.npc = r.i32();
    out.creatureId = r.u32();
    out.tag = r.str();
    if (!validSlotStatus(status)) {
        return false;
    }
    out.status = static_cast<SlotStatus>(status);
    if (out.status == SlotStatus::Assigned && out.creatureId == 0) {
        return false;
    }
    return finish(r);
}

// ModuleInfo

std::vector<uint8_t> encode(const ModuleInfo &m) {
    auto w = begin(MessageType::ModuleInfo);
    w.u32(m.generation);
    w.str(m.module);
    w.u32(m.player.hostId);
    w.i32(m.player.appearance);
    w.u8(m.player.gender);
    w.str(m.player.name);
    w.i32(m.player.portrait);
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, ModuleInfo &out) {
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::ModuleInfo)) {
        return false;
    }
    out.generation = r.u32();
    out.module = r.str();
    out.player.hostId = r.u32();
    out.player.appearance = r.i32();
    out.player.gender = r.u8();
    out.player.name = r.str();
    out.player.portrait = r.i32();
    if (out.player.portrait < -1 || out.player.portrait >= 100000) {
        return false;
    }
    if (!finish(r) || out.module.empty() || out.module.size() > 16) {
        return false;
    }
    // A module name is a resref: letters, digits and underscores.
    for (char c : out.module) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
            return false;
        }
    }
    return out.player.appearance >= 0 && out.player.appearance < 100000;
}

// Snapshot

std::vector<uint8_t> encode(const Snapshot &m) {
    auto w = begin(MessageType::Snapshot);
    w.u32(m.tick);
    w.u32(m.generation);
    w.u32(m.baseTick);
    writeWorld(w, m.worldFields, m.world);
    w.u16(static_cast<uint16_t>(m.entities.size()));
    for (const auto &entity : m.entities) {
        writeEntity(w, entity);
    }
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, Snapshot &out) {
    if (bytes.size() > kMaxHostMessageBytes) {
        return false;
    }
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::Snapshot)) {
        return false;
    }
    out.tick = r.u32();
    out.generation = r.u32();
    out.baseTick = r.u32();
    // A base is an earlier snapshot of the same module visit.
    if (!r.ok() || out.tick == 0 || out.baseTick >= out.tick) {
        return false;
    }
    out.world = WorldState();
    if (!readWorld(r, out.worldFields, out.world)) {
        return false;
    }
    uint16_t entityCount = r.u16();
    if (!r.ok() || entityCount > kMaxEntities) {
        return false;
    }
    out.entities.clear();
    out.entities.reserve(entityCount);
    for (uint16_t i = 0; i < entityCount; ++i) {
        EntityRecord record;
        if (!readEntity(r, record)) {
            return false;
        }
        out.entities.push_back(std::move(record));
    }
    return finish(r);
}

// SnapshotAck

std::vector<uint8_t> encode(const SnapshotAck &m) {
    auto w = begin(MessageType::SnapshotAck);
    w.u32(m.generation);
    w.u32(m.tick);
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, SnapshotAck &out) {
    if (bytes.size() > kMaxClientMessageBytes) {
        return false;
    }
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::SnapshotAck)) {
        return false;
    }
    out.generation = r.u32();
    out.tick = r.u32();
    return finish(r) && out.tick != 0;
}

// Events

std::vector<uint8_t> encode(const EventBatch &m) {
    auto w = begin(MessageType::Events);
    w.u32(m.generation);
    w.u32(m.tick);
    w.u16(static_cast<uint16_t>(m.events.size()));
    for (const auto &event : m.events) {
        w.u8(static_cast<uint8_t>(event.kind));
        w.u32(event.handle);
        switch (event.kind) {
        case EventKind::VfxBegin:
            w.u32(event.row);
            w.u32(event.a);
            writeVec3(w, event.location);
            w.f32(event.facing);
            break;
        case EventKind::VfxEnd:
            break;
        case EventKind::Projectile:
            w.u32(event.a);
            w.u32(event.b);
            w.u8(event.flags);
            break;
        }
    }
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, EventBatch &out) {
    if (bytes.size() > kMaxHostMessageBytes) {
        return false;
    }
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::Events)) {
        return false;
    }
    out.generation = r.u32();
    out.tick = r.u32();
    uint16_t eventCount = r.u16();
    if (!r.ok() || eventCount > kMaxEvents) {
        return false;
    }
    out.events.clear();
    for (uint16_t i = 0; i < eventCount; ++i) {
        uint8_t kind = r.u8();
        if (!validEventKind(kind)) {
            return false;
        }
        NetEvent event;
        event.kind = static_cast<EventKind>(kind);
        event.handle = r.u32();
        switch (event.kind) {
        case EventKind::VfxBegin:
            event.row = r.u32();
            event.a = r.u32();
            event.location = readVec3(r);
            event.facing = r.f32();
            break;
        case EventKind::VfxEnd:
            break;
        case EventKind::Projectile:
            event.a = r.u32();
            event.b = r.u32();
            event.flags = r.u8();
            if (event.flags > 3) {
                return false;
            }
            break;
        }
        if (!r.ok()) {
            return false;
        }
        out.events.push_back(event);
    }
    return finish(r);
}

// Command

std::vector<uint8_t> encode(const Command &m) {
    auto w = begin(MessageType::Command);
    w.u32(m.seq);
    w.u8(static_cast<uint8_t>(m.type));
    w.u32(m.actor);
    w.u32(m.target);
    writeVec3(w, m.point);
    w.boolean(m.run);
    w.i32(m.param);
    w.str(m.item);
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, Command &out) {
    if (bytes.size() > kMaxClientMessageBytes) {
        return false;
    }
    ByteReader r(bytes, 64);
    if (!expect(r, MessageType::Command)) {
        return false;
    }
    out.seq = r.u32();
    uint8_t type = r.u8();
    out.actor = r.u32();
    out.target = r.u32();
    out.point = readVec3(r);
    out.run = r.boolean();
    out.param = r.i32();
    out.item = r.str();
    if (!finish(r) || !validCommandType(type)) {
        return false;
    }
    out.type = static_cast<CommandType>(type);
    // Points are area coordinates; K1 areas are a few hundred metres across.
    for (float v : {out.point.x, out.point.y, out.point.z}) {
        if (std::fabs(v) > 100000.0f) {
            return false;
        }
    }
    return true;
}

// CommandResult

std::vector<uint8_t> encode(const CommandResult &m) {
    auto w = begin(MessageType::CommandResult);
    w.u32(m.seq);
    w.boolean(m.accepted);
    w.u8(static_cast<uint8_t>(m.reason));
    w.u32(m.hostTick);
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, CommandResult &out) {
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::CommandResult)) {
        return false;
    }
    out.seq = r.u32();
    out.accepted = r.boolean();
    uint8_t reason = r.u8();
    out.hostTick = r.u32();
    if (!finish(r) || !validCommandReject(reason)) {
        return false;
    }
    out.reason = static_cast<CommandReject>(reason);
    return out.accepted == (out.reason == CommandReject::None);
}

// Bye

std::vector<uint8_t> encode(const Bye &m) {
    auto w = begin(MessageType::Bye);
    w.str(m.reason);
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, Bye &out) {
    if (bytes.size() > kMaxClientMessageBytes) {
        return false;
    }
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::Bye)) {
        return false;
    }
    out.reason = r.str();
    return finish(r);
}

// Notice

std::vector<uint8_t> encode(const Notice &m) {
    auto w = begin(MessageType::Notice);
    w.u8(static_cast<uint8_t>(m.kind));
    w.str(m.text);
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, Notice &out) {
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::Notice)) {
        return false;
    }
    uint8_t kind = r.u8();
    out.text = r.str();
    if (!finish(r) || kind > static_cast<uint8_t>(NoticeKind::TerminalControl) || !validText(out.text)) {
        return false;
    }
    out.kind = static_cast<NoticeKind>(kind);
    return true;
}

// ContainerView

std::vector<uint8_t> encode(const ContainerView &m) {
    auto w = begin(MessageType::ContainerView);
    w.u32(m.container);
    w.boolean(m.open);
    w.u16(static_cast<uint16_t>(m.items.size()));
    for (const auto &item : m.items) {
        w.u32(item.id);
        w.str(item.resref);
        w.str(item.tag);
        w.u16(item.stack);
    }
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, ContainerView &out) {
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::ContainerView)) {
        return false;
    }
    out.container = r.u32();
    out.open = r.boolean();
    uint16_t count = r.u16();
    if (!r.ok() || count > kMaxContainerItems || out.container == 0) {
        return false;
    }
    out.items.clear();
    for (uint16_t i = 0; i < count; ++i) {
        ContainerItem item;
        item.id = r.u32();
        item.resref = r.str();
        item.tag = r.str();
        item.stack = r.u16();
        if (!r.ok() || item.id == 0 || item.stack == 0 || !validResRef(item.resref) || !validText(item.tag)) {
            return false;
        }
        out.items.push_back(std::move(item));
    }
    return finish(r);
}

// ReadyCheck

std::vector<uint8_t> encode(const ReadyCheck &m) {
    auto w = begin(MessageType::ReadyCheck);
    w.u32(m.id);
    w.u8(static_cast<uint8_t>(m.state));
    w.str(m.initiator);
    w.str(m.destination);
    w.u16(m.secondsLeft);
    w.u8(static_cast<uint8_t>(m.players.size()));
    for (const auto &player : m.players) {
        w.str(player.name);
        w.u8(static_cast<uint8_t>(player.answer));
        w.boolean(player.busy);
    }
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, ReadyCheck &out) {
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::ReadyCheck)) {
        return false;
    }
    out.id = r.u32();
    uint8_t state = r.u8();
    out.initiator = r.str();
    out.destination = r.str();
    out.secondsLeft = r.u16();
    uint8_t count = r.u8();
    if (!r.ok() || state > static_cast<uint8_t>(ReadyState::Cancelled) || count > kMaxReadyPlayers ||
        !validText(out.initiator) || !validText(out.destination)) {
        return false;
    }
    out.state = static_cast<ReadyState>(state);
    out.players.clear();
    for (uint8_t i = 0; i < count; ++i) {
        ReadyPlayer player;
        player.name = r.str();
        uint8_t answer = r.u8();
        player.busy = r.boolean();
        if (!r.ok() || answer > static_cast<uint8_t>(ReadyAnswer::NotReady) || !validText(player.name)) {
            return false;
        }
        player.answer = static_cast<ReadyAnswer>(answer);
        out.players.push_back(std::move(player));
    }
    return finish(r);
}

// Entity deltas

uint16_t quantizeFacing(float radians) {
    float wrapped = std::fmod(radians, kTwoPi);
    if (wrapped < 0.0f) {
        wrapped += kTwoPi;
    }
    auto value = static_cast<uint32_t>(std::lround(wrapped / kTwoPi * 65536.0f));
    return static_cast<uint16_t>(value & 0xffff);
}

float dequantizeFacing(uint16_t value) {
    return static_cast<float>(value) / 65536.0f * kTwoPi;
}

uint16_t diffEntity(const EntityState &prev, const EntityState &cur) {
    uint16_t mask = 0;
    if (prev.type != cur.type || prev.key != cur.key || prev.tag != cur.tag || prev.resref != cur.resref) {
        mask |= kFieldIdentity;
    }
    if (std::fabs(prev.position.x - cur.position.x) > kPositionEpsilon ||
        std::fabs(prev.position.y - cur.position.y) > kPositionEpsilon ||
        std::fabs(prev.position.z - cur.position.z) > kPositionEpsilon) {
        mask |= kFieldPosition;
    }
    if (quantizeFacing(prev.facing) != quantizeFacing(cur.facing)) {
        mask |= kFieldFacing;
    }
    if (prev.anim != cur.anim || prev.animFlags != cur.animFlags || prev.animSpeed != cur.animSpeed) {
        mask |= kFieldAnim;
    }
    if (prev.hp != cur.hp || prev.maxHp != cur.maxHp || prev.dead != cur.dead) {
        mask |= kFieldVitals;
    }
    if (prev.rightWeapon != cur.rightWeapon || prev.leftWeapon != cur.leftWeapon || prev.armor != cur.armor ||
        prev.headgear != cur.headgear) {
        mask |= kFieldWeapons;
    }
    if (prev.open != cur.open || prev.locked != cur.locked) {
        mask |= kFieldOpenLock;
    }
    if (prev.doorState != cur.doorState || prev.doorTransition != cur.doorTransition) {
        mask |= kFieldDoor;
    }
    if (prev.flags != cur.flags) {
        mask |= kFieldFlags;
    }
    if (prev.attackTarget != cur.attackTarget) {
        mask |= kFieldTarget;
    }
    return mask;
}

void mergeEntity(EntityState &dst, const EntityState &src, uint16_t mask) {
    dst.id = src.id;
    if (mask & kFieldIdentity) {
        dst.type = src.type;
        dst.key = src.key;
        dst.tag = src.tag;
        dst.resref = src.resref;
    }
    if (mask & kFieldPosition) {
        dst.position = src.position;
    }
    if (mask & kFieldFacing) {
        dst.facing = src.facing;
    }
    if (mask & kFieldAnim) {
        dst.anim = src.anim;
        dst.animTime = src.animTime;
        dst.animFlags = src.animFlags;
        dst.animSpeed = src.animSpeed;
        dst.animSerial = src.animSerial;
        dst.animSentTick = src.animSentTick;
    }
    if (mask & kFieldVitals) {
        dst.hp = src.hp;
        dst.maxHp = src.maxHp;
        dst.dead = src.dead;
    }
    if (mask & kFieldWeapons) {
        dst.rightWeapon = src.rightWeapon;
        dst.leftWeapon = src.leftWeapon;
        dst.armor = src.armor;
        dst.headgear = src.headgear;
    }
    if (mask & kFieldOpenLock) {
        dst.open = src.open;
        dst.locked = src.locked;
    }
    if (mask & kFieldDoor) {
        dst.doorState = src.doorState;
        dst.doorTransition = src.doorTransition;
    }
    if (mask & kFieldFlags) {
        dst.flags = src.flags;
    }
    if (mask & kFieldTarget) {
        dst.attackTarget = src.attackTarget;
    }
}

uint16_t exactDiffEntity(const EntityState &prev, const EntityState &cur) {
    uint16_t mask = 0;
    if (prev.type != cur.type || prev.key != cur.key || prev.tag != cur.tag || prev.resref != cur.resref) {
        mask |= kFieldIdentity;
    }
    if (prev.position != cur.position) {
        mask |= kFieldPosition;
    }
    if (prev.facing != cur.facing) {
        mask |= kFieldFacing;
    }
    if (prev.anim != cur.anim || prev.animTime != cur.animTime || prev.animFlags != cur.animFlags ||
        prev.animSpeed != cur.animSpeed || prev.animSerial != cur.animSerial) {
        mask |= kFieldAnim;
    }
    if (prev.hp != cur.hp || prev.maxHp != cur.maxHp || prev.dead != cur.dead) {
        mask |= kFieldVitals;
    }
    if (prev.rightWeapon != cur.rightWeapon || prev.leftWeapon != cur.leftWeapon || prev.armor != cur.armor ||
        prev.headgear != cur.headgear) {
        mask |= kFieldWeapons;
    }
    if (prev.open != cur.open || prev.locked != cur.locked) {
        mask |= kFieldOpenLock;
    }
    if (prev.doorState != cur.doorState || prev.doorTransition != cur.doorTransition) {
        mask |= kFieldDoor;
    }
    if (prev.flags != cur.flags) {
        mask |= kFieldFlags;
    }
    if (prev.attackTarget != cur.attackTarget) {
        mask |= kFieldTarget;
    }
    return mask;
}

uint16_t exactDiffWorld(const WorldState &prev, const WorldState &cur) {
    uint16_t fields = 0;
    if (prev.screen != cur.screen) {
        fields |= kWorldScreen;
    }
    if (prev.fadeOpacity != cur.fadeOpacity || prev.fadeColor != cur.fadeColor) {
        fields |= kWorldFade;
    }
    if (prev.movie != cur.movie || prev.movieName != cur.movieName) {
        fields |= kWorldMovie;
    }
    if (prev.cameraType != cur.cameraType || prev.cameraId != cur.cameraId) {
        fields |= kWorldCamera;
    }
    if (prev.conversation != cur.conversation) {
        fields |= kWorldConversation;
    }
    if (!(prev.party == cur.party) || prev.leader != cur.leader) {
        fields |= kWorldParty;
    }
    if (prev.paused != cur.paused || prev.pausedBy != cur.pausedBy) {
        fields |= kWorldPause;
    }
    return fields;
}

void mergeWorld(WorldState &dst, const WorldState &src, uint16_t mask) {
    if (mask & kWorldScreen) {
        dst.screen = src.screen;
    }
    if (mask & kWorldFade) {
        dst.fadeOpacity = src.fadeOpacity;
        dst.fadeColor = src.fadeColor;
    }
    if (mask & kWorldMovie) {
        dst.movie = src.movie;
        dst.movieName = src.movieName;
    }
    if (mask & kWorldCamera) {
        dst.cameraType = src.cameraType;
        dst.cameraId = src.cameraId;
    }
    if (mask & kWorldConversation) {
        dst.conversation = src.conversation;
    }
    if (mask & kWorldParty) {
        dst.party = src.party;
        dst.leader = src.leader;
    }
    if (mask & kWorldPause) {
        dst.paused = src.paused;
        dst.pausedBy = src.pausedBy;
    }
}

// Co-op completeness: shared inventory, character sheet, bark

std::vector<uint8_t> encode(const SharedInventory &m) {
    auto w = begin(MessageType::SharedInventory);
    w.u32(m.owner);
    w.i32(m.credits);
    w.u16(static_cast<uint16_t>(m.items.size()));
    for (const auto &item : m.items) {
        w.u32(item.id);
        w.str(item.resref);
        w.u16(item.stack);
        w.u8(item.flags);
    }
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, SharedInventory &out) {
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::SharedInventory)) {
        return false;
    }
    out.owner = r.u32();
    out.credits = r.i32();
    uint16_t count = r.u16();
    if (!r.ok() || count > kMaxInventoryItems || out.credits < 0) {
        return false;
    }
    out.items.clear();
    for (uint16_t i = 0; i < count; ++i) {
        InventoryItem item;
        item.id = r.u32();
        item.resref = r.str();
        item.stack = r.u16();
        item.flags = r.u8();
        if (!r.ok() || item.id == 0 || item.stack == 0 || !validResRef(item.resref) || (item.flags & ~kInventoryUsable)) {
            return false;
        }
        out.items.push_back(std::move(item));
    }
    return finish(r);
}

std::vector<uint8_t> encode(const CharacterSheet &m) {
    auto w = begin(MessageType::CharacterSheet);
    w.u32(m.creature);
    w.i32(m.xp);
    w.boolean(m.levelUp);
    w.u8(static_cast<uint8_t>(m.classes.size()));
    for (const auto &entry : m.classes) {
        w.i16(entry.clazz);
        w.i16(entry.level);
    }
    for (uint8_t value : m.abilities) {
        w.u8(value);
    }
    for (uint8_t value : m.skills) {
        w.u8(value);
    }
    w.u16(static_cast<uint16_t>(m.feats.size()));
    for (uint16_t feat : m.feats) {
        w.u16(feat);
    }
    w.u16(static_cast<uint16_t>(m.spells.size()));
    for (uint16_t spell : m.spells) {
        w.u16(spell);
    }
    w.i16(m.baseHp);
    w.i16(m.baseFp);
    w.u8(static_cast<uint8_t>(m.equipment.size()));
    for (const auto &entry : m.equipment) {
        w.u8(entry.slot);
        w.u32(entry.id);
        w.str(entry.resref);
    }
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, CharacterSheet &out) {
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::CharacterSheet)) {
        return false;
    }
    out = CharacterSheet();
    out.creature = r.u32();
    out.xp = r.i32();
    out.levelUp = r.boolean();
    uint8_t classes = r.u8();
    if (!r.ok() || out.creature == 0 || out.xp < 0 || classes > 3) {
        return false;
    }
    for (uint8_t i = 0; i < classes; ++i) {
        SheetClass entry;
        entry.clazz = r.i16();
        entry.level = r.i16();
        if (!r.ok() || entry.clazz < 0 || entry.level < 0 || entry.level > 60) {
            return false;
        }
        out.classes.push_back(entry);
    }
    for (auto &value : out.abilities) {
        value = r.u8();
    }
    for (auto &value : out.skills) {
        value = r.u8();
    }
    uint16_t feats = r.u16();
    if (!r.ok() || feats > kMaxSheetEntries) {
        return false;
    }
    for (uint16_t i = 0; i < feats; ++i) {
        out.feats.push_back(r.u16());
    }
    uint16_t spells = r.u16();
    if (!r.ok() || spells > kMaxSheetEntries) {
        return false;
    }
    for (uint16_t i = 0; i < spells; ++i) {
        out.spells.push_back(r.u16());
    }
    out.baseHp = r.i16();
    out.baseFp = r.i16();
    uint8_t equipment = r.u8();
    if (!r.ok() || equipment > kMaxSheetEquipment) {
        return false;
    }
    for (uint8_t i = 0; i < equipment; ++i) {
        SheetEquipment entry;
        entry.slot = r.u8();
        entry.id = r.u32();
        entry.resref = r.str();
        if (!r.ok() || entry.slot > 30 || entry.id == 0 || !validResRef(entry.resref)) {
            return false;
        }
        out.equipment.push_back(std::move(entry));
    }
    return finish(r);
}

std::vector<uint8_t> encode(const Bark &m) {
    auto w = begin(MessageType::Bark);
    w.str(m.text);
    w.f32(m.duration);
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, Bark &out) {
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::Bark)) {
        return false;
    }
    out.text = r.str();
    out.duration = r.f32();
    if (!r.ok() || !validText(out.text) || !std::isfinite(out.duration) || out.duration < 0.0f || out.duration > 600.0f) {
        return false;
    }
    return finish(r);
}

// Free Mode: guest characters and guest stores

std::vector<uint8_t> encode(const GuestCharacter &m) {
    auto w = begin(MessageType::GuestCharacter);
    w.str(m.name);
    w.i32(m.appearance);
    w.u8(m.gender);
    w.i32(m.portrait);
    w.u8(static_cast<uint8_t>(m.classes.size()));
    for (const auto &entry : m.classes) {
        w.i16(entry.clazz);
        w.i16(entry.level);
    }
    for (uint8_t value : m.abilities) {
        w.u8(value);
    }
    for (uint8_t value : m.skills) {
        w.u8(value);
    }
    w.u16(static_cast<uint16_t>(m.feats.size()));
    for (uint16_t feat : m.feats) {
        w.u16(feat);
    }
    w.u16(static_cast<uint16_t>(m.spells.size()));
    for (uint16_t spell : m.spells) {
        w.u16(spell);
    }
    w.i32(m.xp);
    w.i32(m.credits);
    w.i16(m.baseHp);
    w.i16(m.currentHp);
    w.i16(m.baseFp);
    w.u8(m.goodEvil);
    w.u16(static_cast<uint16_t>(m.items.size()));
    for (const auto &item : m.items) {
        w.str(item.resref);
        w.u16(item.stack);
        w.u8(item.slot);
    }
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, GuestCharacter &out) {
    if (bytes.size() > kMaxGuestCharacterBytes) {
        return false;
    }
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::GuestCharacter)) {
        return false;
    }
    out = GuestCharacter();
    out.name = r.str();
    out.appearance = r.i32();
    out.gender = r.u8();
    out.portrait = r.i32();
    uint8_t classes = r.u8();
    if (!r.ok() || out.name.empty() || out.name.size() > 32 || !validText(out.name) || out.appearance < 0 ||
        out.appearance > 100000 || out.gender > 4 || out.portrait < -1 || out.portrait > 100000 || classes == 0 || classes > 3) {
        return false;
    }
    for (uint8_t i = 0; i < classes; ++i) {
        SheetClass entry;
        entry.clazz = r.i16();
        entry.level = r.i16();
        if (!r.ok() || entry.clazz < 0 || entry.level < 1 || entry.level > 50) {
            return false;
        }
        out.classes.push_back(entry);
    }
    for (auto &value : out.abilities) {
        value = r.u8();
    }
    for (auto &value : out.skills) {
        value = r.u8();
    }
    uint16_t feats = r.u16();
    if (!r.ok() || feats > kMaxSheetEntries) {
        return false;
    }
    for (uint16_t i = 0; i < feats; ++i) {
        out.feats.push_back(r.u16());
    }
    uint16_t spells = r.u16();
    if (!r.ok() || spells > kMaxSheetEntries) {
        return false;
    }
    for (uint16_t i = 0; i < spells; ++i) {
        out.spells.push_back(r.u16());
    }
    out.xp = r.i32();
    out.credits = r.i32();
    out.baseHp = r.i16();
    out.currentHp = r.i16();
    out.baseFp = r.i16();
    out.goodEvil = r.u8();
    uint16_t items = r.u16();
    if (!r.ok() || out.xp < 0 || out.credits < 0 || out.baseHp < 1 || out.baseFp < 0 || out.goodEvil > 100 ||
        items > kMaxGuestItems) {
        return false;
    }
    for (uint16_t i = 0; i < items; ++i) {
        GuestItem item;
        item.resref = r.str();
        item.stack = r.u16();
        item.slot = r.u8();
        if (!r.ok() || !validResRef(item.resref) || item.stack == 0 || (item.slot > 30 && item.slot != kGuestInventory)) {
            return false;
        }
        out.items.push_back(std::move(item));
    }
    return finish(r);
}

namespace {

void writeStoreEntries(ByteWriter &w, const std::vector<StoreEntry> &entries) {
    w.u16(static_cast<uint16_t>(entries.size()));
    for (const auto &entry : entries) {
        w.u32(entry.id);
        w.str(entry.resref);
        w.u16(entry.stack);
        w.i32(entry.price);
    }
}

bool readStoreEntries(ByteReader &r, std::vector<StoreEntry> &entries) {
    uint16_t count = r.u16();
    if (!r.ok() || count > kMaxStoreItems) {
        return false;
    }
    entries.clear();
    for (uint16_t i = 0; i < count; ++i) {
        StoreEntry entry;
        entry.id = r.u32();
        entry.resref = r.str();
        entry.stack = r.u16();
        entry.price = r.i32();
        if (!r.ok() || entry.id == 0 || !validResRef(entry.resref) || entry.stack == 0 || entry.price < 0) {
            return false;
        }
        entries.push_back(std::move(entry));
    }
    return true;
}

} // namespace

std::vector<uint8_t> encode(const StoreView &m) {
    auto w = begin(MessageType::StoreView);
    w.u32(m.store);
    w.boolean(m.open);
    w.i32(m.credits);
    writeStoreEntries(w, m.stock);
    writeStoreEntries(w, m.sellable);
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, StoreView &out) {
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::StoreView)) {
        return false;
    }
    out = StoreView();
    out.store = r.u32();
    out.open = r.boolean();
    out.credits = r.i32();
    if (!r.ok() || out.store == 0 || out.credits < 0) {
        return false;
    }
    if (!readStoreEntries(r, out.stock) || !readStoreEntries(r, out.sellable)) {
        return false;
    }
    return finish(r);
}

std::vector<uint8_t> encode(const Offer &m) {
    auto w = begin(MessageType::Offer);
    w.u8(static_cast<uint8_t>(m.kind));
    w.u32(m.target);
    w.str(m.name);
    w.i32(m.maximumWager);
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, Offer &out) {
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::Offer)) {
        return false;
    }
    uint8_t kind = r.u8();
    out.target = r.u32();
    out.name = r.str();
    out.maximumWager = r.i32();
    if (!r.ok() || kind < 1 || kind > 3 || out.target == 0 || !validText(out.name) || out.maximumWager < 0) {
        return false;
    }
    out.kind = static_cast<OfferKind>(kind);
    return finish(r);
}

std::vector<uint8_t> encode(const GuestRoster &m) {
    auto w = begin(MessageType::GuestRoster);
    w.u8(static_cast<uint8_t>(m.entries.size()));
    for (const auto &entry : m.entries) {
        w.str(entry.name);
        w.i16(entry.hp);
        w.i16(entry.maxHp);
        w.u8(entry.level);
        w.boolean(entry.present);
    }
    return w.take();
}

bool decode(const std::vector<uint8_t> &bytes, GuestRoster &out) {
    ByteReader r(bytes, kMaxStringBytes);
    if (!expect(r, MessageType::GuestRoster)) {
        return false;
    }
    out = GuestRoster();
    uint8_t count = r.u8();
    if (!r.ok() || count > 16) {
        return false;
    }
    for (uint8_t i = 0; i < count; ++i) {
        GuestRosterEntry entry;
        entry.name = r.str();
        entry.hp = r.i16();
        entry.maxHp = r.i16();
        entry.level = r.u8();
        entry.present = r.boolean();
        if (!r.ok() || entry.name.empty() || entry.name.size() > 32 || !validText(entry.name) || entry.level > 50) {
            return false;
        }
        out.entries.push_back(std::move(entry));
    }
    return finish(r);
}

} // namespace net

} // namespace reone
