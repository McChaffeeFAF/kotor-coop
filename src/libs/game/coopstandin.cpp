/* Copyright (c) 2026 The reone project contributors */

// Co-op stand-ins (co-op design notes item 12, design note coop_standins). The
// host decides when a player gets one, builds it, and removes it; a client
// only builds the look of the stand-in the host announced.

#include "reone/system/portablemath.h"
#include "reone/game/coopstandin.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <boost/algorithm/string.hpp>

#include "reone/game/coopreplication.h"
#include "reone/game/coopsession.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/system/logutil.h"

using namespace reone::net;

namespace reone {

namespace game {

// Rules and helpers

const std::vector<CoopStandInSection> &coopStandInSections() {
    // From the survey of the K1 data (design note coop_standins). A section
    // not listed plays: stand-ins spawn and fight beside the solo character.
    static const std::vector<CoopStandInSection> sections {
        {"tar_m02ae", CoopStandInRule::Watch, "Taris Upper City cantina: the refereed one-on-one duel ring"},
        {"sta_m45ac", CoopStandInRule::Watch, "Star Forge: the final duel"},
        {"unk_m44ab", CoopStandInRule::Watch, "Rakatan Temple: the forced duel with Bastila"},
        {"unk_m44ac", CoopStandInRule::Watch, "Rakatan Temple: the forced duel with Bastila"},
        {"stunt_*", CoopStandInRule::Watch, "cutscene modules: no gameplay"},
    };
    return sections;
}

CoopStandInRule coopStandInRule(const std::string &module) {
    auto name = boost::to_lower_copy(module);
    for (const auto &section : coopStandInSections()) {
        std::string pattern(section.module);
        bool prefix = !pattern.empty() && pattern.back() == '*';
        if (prefix ? name.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0 && name.size() >= pattern.size() - 1
                   : name == pattern) {
            return section.rule;
        }
    }
    return CoopStandInRule::Play;
}

const char *coopStandInRuleName(CoopStandInRule rule) {
    return rule == CoopStandInRule::Watch ? "watch" : "play";
}

std::string coopStandInResRef(int appearance, int gender, uint32_t sourceHostId) {
    return std::string(kStandInPrefix) + std::to_string(appearance) + ":" + std::to_string(gender) + ":" +
           std::to_string(sourceHostId);
}

bool parseCoopStandInResRef(const std::string &resref, int &appearance, int &gender, uint32_t &sourceHostId) {
    size_t prefix = std::strlen(kStandInPrefix);
    if (resref.size() <= prefix || resref.compare(0, prefix, kStandInPrefix) != 0 || resref.size() > 64) {
        return false;
    }
    std::vector<std::string> parts;
    auto body = resref.substr(prefix);
    boost::split(parts, body, boost::is_any_of(":"));
    if (parts.size() != 3) {
        return false;
    }
    uint64_t values[3] {};
    for (size_t i = 0; i < 3; ++i) {
        const auto &part = parts[i];
        if (part.empty() || part.size() > 10 ||
            !std::all_of(part.begin(), part.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
            return false;
        }
        values[i] = std::stoull(part);
    }
    if (values[0] > 100000 || values[1] > 4 || values[2] == 0 || values[2] > 0xffffffffull) {
        return false;
    }
    appearance = static_cast<int>(values[0]);
    gender = static_cast<int>(values[1]);
    sourceHostId = static_cast<uint32_t>(values[2]);
    return true;
}

std::vector<glm::vec2> coopStandInOffsets(float facing, int index) {
    // Forward and right of the solo character. Players alternate sides; a
    // later pair stands a step further back.
    glm::vec2 forward(portable::cosf(facing), portable::sinf(facing));
    glm::vec2 right(forward.y, -forward.x);
    float side = (index % 2 == 0) ? 1.0f : -1.0f;
    float back = 1.5f + 1.0f * static_cast<float>(index / 2);
    std::vector<glm::vec2> result {
        -forward * back + right * (1.2f * side),
        right * (1.5f * side),
        -forward * (back + 0.5f),
        -forward * back - right * (1.2f * side),
        -right * (1.5f * side),
        -forward * (back + 1.5f) + right * (1.5f * side),
    };
    return result;
}

// Host

bool CoopSession::isStandIn(uint32_t creatureId) const {
    return isHost() && creatureId != 0 && _standInSource.count(creatureId) != 0;
}

uint32_t CoopSession::hostControlledId(const HostClient &client) const {
    if (client.slot.status == SlotStatus::Assigned) {
        return client.slot.creatureId;
    }
    return client.guestId ? client.guestId : client.standIn;
}

SlotAssign CoopSession::hostEffectiveSlot(const HostClient &client) const {
    if (client.guestId) {
        SlotAssign slot;
        slot.status = SlotStatus::Guest;
        slot.npc = -1;
        slot.creatureId = client.guestId;
        if (auto object = _game.getObjectById(client.guestId)) {
            slot.tag = object->tag().substr(0, kMaxStringBytes);
        }
        return slot;
    }
    if (client.standIn && client.slot.status != SlotStatus::Assigned && client.slot.status != SlotStatus::Declined) {
        SlotAssign slot;
        slot.status = SlotStatus::StandIn;
        slot.npc = -1;
        slot.creatureId = client.standIn;
        if (auto object = _game.getObjectById(client.standIn)) {
            slot.tag = object->tag().substr(0, kMaxStringBytes);
        }
        return slot;
    }
    return client.slot;
}

void CoopSession::hostSendSlot(HostClient &client, bool force) {
    auto slot = hostEffectiveSlot(client);
    const auto &sent = client.slotSent;
    if (!force && client.slotSentValid && sent.status == slot.status && sent.creatureId == slot.creatureId &&
        sent.npc == slot.npc && sent.tag == slot.tag) {
        return;
    }
    client.slotSent = slot;
    client.slotSentValid = true;
    send(client.connection, encode(slot));
}

std::string CoopSession::hostStandInPause() const {
    auto module = _game.module();
    if (!module || !module->area()) {
        return "no_area";
    }
    if (!_game._nextModule.empty()) {
        return "transition";
    }
    if (_game._movie) {
        return "movie";
    }
    if (_hostDefeated) {
        return "defeat";
    }
    switch (_game._screen) {
    case Game::Screen::InGame:
    case Game::Screen::InGameMenu:
    case Game::Screen::Container:
    case Game::Screen::Store:
    case Game::Screen::Upgrade:
        break;
    case Game::Screen::Conversation:
        return "conversation";
    default:
        return "screen";
    }
    auto leader = _game.party().getLeader();
    if (!leader || leader->isDead()) {
        return "no_leader";
    }
    if (leader->isInConversation()) {
        return "conversation";
    }
    if (!leader->isCommandable()) {
        // A script holds the solo character for a scene.
        return "cutscene";
    }
    if (coopStandInRule(module->name()) == CoopStandInRule::Watch) {
        return "watch";
    }
    if (_game.party().isSoloMode()) {
        // SetSoloMode: the story means the solo character to be alone here
        // (the final duels set it).
        return "solo_mode";
    }
    return std::string();
}

void CoopSession::hostUpdateStandIns() {
    if (_inModuleSnapshot) {
        return;
    }
    _standInPause = hostStandInPause();
    auto leader = _game.party().getLeader();
    int index = 0;
    for (auto &[connection, client] : _clients) {
        if (!client.welcomed) {
            continue;
        }
        // Design item 12 and "Lost companion": a player with no companion
        // free plays a stand-in, once the host has let them in.
        bool wants = client.slotRequested && client.slotAnswered && client.slot.status != SlotStatus::Assigned &&
                     client.slot.status != SlotStatus::Declined && client.approval != HostClient::Approval::Pending &&
                     client.approval != HostClient::Approval::Declined;
        std::shared_ptr<Creature> existing = client.standIn ? _game.getObjectById<Creature>(client.standIn) : nullptr;
        if (client.standIn && (!existing || !existing->isRuntimeLive())) {
            _standInSource.erase(client.standIn);
            client.standIn = 0;
            existing.reset();
        }
        bool out = wants && _standInPause.empty() && leader;
        if (existing && out) {
            // The party is down to someone else now (a script handed control
            // to another character): the stand-in becomes a copy of them.
            auto source = _standInSource.find(existing->id());
            if (source == _standInSource.end() || source->second != leader->id()) {
                hostDespawnStandIn(client, false);
                existing.reset();
            }
        }
        if (!out && existing) {
            hostDespawnStandIn(client, true);
        } else if (out && !existing) {
            hostSpawnStandIn(client, *leader, index);
        }
        if (wants) {
            ++index;
        }
        if (client.slotAnswered) {
            hostSendSlot(client, false);
        }
    }
}

std::shared_ptr<Creature> CoopSession::hostSpawnStandIn(HostClient &client, const Creature &source, int index) {
    auto module = _game.module();
    if (!module || !module->area()) {
        return nullptr;
    }
    auto &area = *module->area();
    std::shared_ptr<Creature> standIn;
    try {
        standIn = _game.newCreature();
        standIn->setTag("coop_standin_" + std::to_string(client.clientId));
        standIn->setName(source.name());
        standIn->setGender(source.gender());
        standIn->setAppearance(source.appearance());
        try {
            standIn->loadAppearance();
        } catch (const std::exception &e) {
            // Without its model it still plays; the client builds its own look.
            warn(std::string("Co-op: the stand-in's appearance did not load: ") + e.what());
        }
        standIn->setFaction(Faction::Player);
        // A copy of the solo character: class levels, abilities, skills,
        // feats and powers, hit and force points, and the equipped gear.
        standIn->attributes() = source.attributes();
        standIn->setGoodEvil(source.goodEvil());
        standIn->setMaxHitPoints(source.hitPoints());
        standIn->setCurrentHitPoints(standIn->maxHitPoints());
        standIn->addBaseForcePoints(source.forcePoints());
        standIn->setCurrentForcePoints(standIn->maxForcePoints());
        for (const auto &[slot, item] : source.equipment()) {
            if (!item) {
                continue;
            }
            auto copy = _game.newItemClone(*item);
            if (copy && !standIn->equip(slot, copy)) {
                _game.destroyRuntimeObjectGraph(copy);
            }
        }
        // Not in the party table (K2's puppet mechanism): party counts and
        // checks still see the solo character alone.
        standIn->setPuppet(true);
    } catch (const std::exception &e) {
        warn(std::string("Co-op: cannot build a stand-in: ") + e.what());
        return nullptr;
    }
    bool placed = false;
    for (const auto &offset : coopStandInOffsets(source.getFacing(), index)) {
        standIn->setPosition(source.position() + glm::vec3(offset, 0.0f));
        if (area.landObject(*standIn)) {
            placed = true;
            break;
        }
    }
    if (!placed) {
        standIn->setPosition(source.position());
    }
    standIn->setFacing(source.getFacing());
    area.add(standIn);
    if (client.standInHp > 0) {
        // Back after a conversation or a save in the same module visit: the
        // stand-in keeps its wounds.
        standIn->setCurrentHitPoints(std::min(client.standInHp, standIn->maxHitPoints()));
    }
    client.standInHp = 0;
    client.standIn = standIn->id();
    _standInSource[standIn->id()] = source.id();
    hostMarkApproved(client); // only a player the host let in gets a stand-in
    info("Co-op: " + client.name + " plays a stand-in of " + source.tag() + " (id " + std::to_string(standIn->id()) + ")" +
         (placed ? "" : " on the solo character's spot"));
    return standIn;
}

void CoopSession::hostDespawnStandIn(HostClient &client, bool remember) {
    if (!client.standIn) {
        return;
    }
    uint32_t id = client.standIn;
    client.standIn = 0;
    _standInSource.erase(id);
    if (client.viewing) {
        hostSendContainer(client, false);
        client.viewing = 0;
        client.viewSent.clear();
    }
    auto creature = _game.getObjectById<Creature>(id);
    auto module = _game.module();
    if (!creature || !module || !module->area()) {
        return;
    }
    client.standInHp = remember && !creature->isDead() ? creature->currentHitPoints() : 0;
    creature->clearAllActions();
    module->area()->destroyObject(*creature);
    flushDestroyedObjects(*module->area());
    info("Co-op: " + client.name + "'s stand-in went away (" + (_standInPause.empty() ? "replaced" : _standInPause) + ")");
}

void CoopSession::beginModuleSnapshot() {
    if (!isHost() || _inModuleSnapshot) {
        return;
    }
    _inModuleSnapshot = true;
    // Stand-ins are never saved: they are rebuilt beside the solo character.
    auto pause = _standInPause;
    _standInPause = "snapshot";
    for (auto &[connection, client] : _clients) {
        hostDespawnStandIn(client, true);
        if (client.guestId) {
            // Guests keep their state in the host's record, never in the save.
            hostDespawnGuest(client);
        }
    }
    _standInPause = pause;
    // Scaled enemies are saved with their base hit points (design item 6:
    // co-op must not leak into the host's save). The exact live values come
    // back afterwards.
    _snapshotHp.clear();
    for (const auto &[id, scaled] : _scaled) {
        auto creature = _game.getObjectById<Creature>(id);
        if (!creature || !creature->isRuntimeLive() || scaled.factor == 1.0f) {
            continue;
        }
        int base = creature->hitPoints();
        int current = creature->currentHitPoints();
        _snapshotHp[id] = {base, current};
        int maximum = creature->maxHitPoints();
        creature->setMaxHitPoints(scaled.base);
        // The same share of the pool as now, never below 1 for a live one.
        int restored = maximum > 0 ? static_cast<int>(std::lround(static_cast<double>(current) * creature->maxHitPoints() / maximum))
                                   : current;
        creature->setCurrentHitPoints(current > 0 ? std::max(1, restored) : current);
    }
}

void CoopSession::endModuleSnapshot() {
    if (!_inModuleSnapshot) {
        return;
    }
    for (const auto &[id, hp] : _snapshotHp) {
        if (auto creature = _game.getObjectById<Creature>(id)) {
            creature->setMaxHitPoints(hp.base);
            creature->setCurrentHitPoints(hp.current);
        }
    }
    _snapshotHp.clear();
    _inModuleSnapshot = false;
}

// Client

std::shared_ptr<Object> CoopSession::clientSpawnStandIn(Replica &replica) {
    const auto &s = replica.state;
    int appearance = 0;
    int gender = 0;
    uint32_t source = 0;
    auto module = _game.module();
    if (!module || !module->area() || !parseCoopStandInResRef(s.resref, appearance, gender, source)) {
        return nullptr;
    }
    std::shared_ptr<Creature> standIn;
    try {
        standIn = _game.newCreature();
        standIn->setTag(s.tag);
        auto local = std::dynamic_pointer_cast<Creature>(localObjectForHost(source));
        standIn->setName(local ? local->name() : std::string("Stand-in"));
        standIn->setGender(static_cast<Gender>(gender));
        standIn->setAppearance(appearance);
        standIn->loadAppearance();
        standIn->setFaction(Faction::Player);
        standIn->setPuppet(true);
    } catch (const std::exception &e) {
        warn(std::string("Co-op: cannot build the stand-in: ") + e.what());
        return nullptr;
    }
    standIn->setPosition(glm::vec3(s.position.x, s.position.y, s.position.z));
    standIn->setFacing(s.facing);
    module->area()->add(standIn);
    replica.spawned = true;
    return standIn;
}

} // namespace game

} // namespace reone
