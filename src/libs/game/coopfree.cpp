/* Copyright (c) 2026 The reone project contributors */

// Co-op Free Mode (co-op design notes, "Session modes: Free Mode";
// design note coop_net, "Free Mode"). Each guest plays their own character,
// made with character generation and saved on the guest's machine. On the
// host it is a puppet outside the party table, as the stand-ins are; the
// host keeps its full AI party and owns the story. Guests get every XP award
// the host gets and trade with their own credits.

#include "reone/game/coopsession.h"

#include <algorithm>
#include <fstream>
#include <iterator>

#include <boost/algorithm/string.hpp>

#include "reone/game/character.h"
#include "reone/game/coopstandin.h"
#include "reone/game/d20/attributes.h"
#include "reone/game/d20/class.h"
#include "reone/game/d20/classes.h"
#include "reone/game/d20/feats.h"
#include "reone/game/d20/spells.h"
#include "reone/game/experience.h"
#include "reone/game/game.h"
#include "reone/game/gui/chargen.h"
#include "reone/game/gui/confirmpopup.h"
#include "reone/game/gui/conversation.h"
#include "reone/game/object/placeable.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/store.h"
#include "reone/game/party.h"
#include "reone/game/storesession.h"
#include "reone/resource/dialog.h"
#include "reone/resource/provider/dialogs.h"
#include "reone/resource/provider/scripts.h"
#include "reone/script/program.h"
#include "reone/script/routines.h"
#include "reone/system/logutil.h"

using namespace reone::net;

namespace reone {

namespace game {

namespace {

constexpr int kSkillCount = 8;
constexpr int kAbilityCount = 6;
/** Ticks between refreshing a guest's saved state from its creature. */
constexpr uint64_t kGuestSyncTicks = 30;
constexpr int32_t kMaxGuestCredits = 10000000;

CreatureAttributes guestAttributes(Game &game, const GuestCharacter &guest) {
    auto &classes = game.services().game.classes;
    CreatureAttributes attributes;
    for (const auto &entry : guest.classes) {
        if (auto clazz = classes.get(static_cast<ClassType>(entry.clazz))) {
            attributes.addClassLevels(clazz.get(), entry.level);
        }
    }
    for (int ability = 0; ability < kAbilityCount; ++ability) {
        attributes.setAbilityScore(static_cast<Ability>(ability), guest.abilities[ability]);
    }
    for (int skill = 0; skill < kSkillCount; ++skill) {
        attributes.setSkillRank(static_cast<SkillType>(skill), guest.skills[skill]);
    }
    for (auto feat : guest.feats) {
        attributes.addFeat(static_cast<FeatType>(feat));
    }
    for (auto spell : guest.spells) {
        attributes.addSpell(static_cast<SpellType>(spell));
    }
    return attributes;
}

void writeAttributes(const CreatureAttributes &attributes, GuestCharacter &guest) {
    guest.classes.clear();
    for (const auto &[clazz, level] : attributes.classLevels()) {
        if (clazz && level > 0 && guest.classes.size() < 3) {
            guest.classes.push_back({static_cast<int16_t>(clazz->type()), static_cast<int16_t>(level)});
        }
    }
    for (int ability = 0; ability < kAbilityCount; ++ability) {
        guest.abilities[ability] = static_cast<uint8_t>(std::clamp(attributes.getAbilityScore(static_cast<Ability>(ability)), 0, 255));
    }
    for (int skill = 0; skill < kSkillCount; ++skill) {
        guest.skills[skill] = static_cast<uint8_t>(std::clamp(attributes.getSkillRank(static_cast<SkillType>(skill)), 0, 255));
    }
    guest.feats.clear();
    for (auto feat : attributes.feats()) {
        if (static_cast<int>(feat) >= 0 && guest.feats.size() < kMaxSheetEntries) {
            guest.feats.push_back(static_cast<uint16_t>(feat));
        }
    }
    guest.spells.clear();
    for (auto spell : attributes.spells()) {
        if (static_cast<int>(spell) >= 0 && guest.spells.size() < kMaxSheetEntries) {
            guest.spells.push_back(static_cast<uint16_t>(spell));
        }
    }
}

std::string itemResRef(const Item &item) {
    auto resref = boost::to_lower_copy(item.blueprintResRef());
    return resref.size() <= 16 ? resref : std::string();
}

/**
 * The store a merchant's conversation opens: the tag its action script looks
 * up just before OpenStore (OpenStore(GetObjectByTag("..."), ...)). Read
 * from the compiled code, never run.
 */
std::string storeTagFromDialog(Game &game, const std::string &dialogResRef) {
    if (dialogResRef.empty()) {
        return std::string();
    }
    auto &routines = game.routines();
    int getObjectByTag = routines.getIndexByName("GetObjectByTag");
    int openStore = routines.getIndexByName("OpenStore");
    try {
        auto dialog = game.services().resource.dialogs.get(boost::to_lower_copy(dialogResRef));
        if (!dialog) {
            return std::string();
        }
        for (const auto *nodes : {&dialog->entries, &dialog->replies}) {
            for (const auto &node : *nodes) {
                for (const auto *name : {&node.script, &node.script2}) {
                    if (name->empty()) {
                        continue;
                    }
                    auto program = game.services().resource.scripts.get(boost::to_lower_copy(*name));
                    if (!program) {
                        continue;
                    }
                    std::string lastString;
                    std::string lastTag;
                    for (const auto &instruction : program->instructions()) {
                        if (instruction.type == script::InstructionType::CONSTS) {
                            lastString = instruction.strValue;
                        } else if (instruction.type == script::InstructionType::ACTION) {
                            if (instruction.routine == getObjectByTag) {
                                lastTag = lastString;
                            } else if (instruction.routine == openStore && !lastTag.empty()) {
                                return boost::to_lower_copy(lastTag);
                            }
                        }
                    }
                }
            }
        }
    } catch (const std::exception &) {
    }
    return std::string();
}

} // namespace

GuestCharacter coopGuestFromCharacter(Game &game, const Character &character) {
    GuestCharacter guest;
    guest.name = character.name.substr(0, 32);
    if (guest.name.empty()) {
        guest.name = "Guest";
    }
    guest.appearance = character.appearance;
    guest.gender = static_cast<uint8_t>(character.gender);
    writeAttributes(character.attributes, guest);
    // As a new player character: hit and force points from the class dice.
    guest.baseHp = static_cast<int16_t>(std::clamp(character.attributes.getAggregateHitDie(), 1, 32767));
    guest.currentHp = 0; // full
    guest.baseFp = static_cast<int16_t>(std::clamp(character.attributes.getAggregateForceDie(), 0, 32767));
    guest.goodEvil = 50;
    guest.items.push_back({"g_a_clothes01", 1, static_cast<uint8_t>(InventorySlots::body)});
    return guest;
}

// Host

std::string CoopSession::hostCheckGuest(const GuestCharacter &guest) const {
    // Design, "Guest characters": level within the cap, valid items, stats
    // consistent with its class and level.
    auto &services = _game.services().game;
    int level = 0;
    for (const auto &entry : guest.classes) {
        if (!services.classes.get(static_cast<ClassType>(entry.clazz))) {
            return "unknown class " + std::to_string(entry.clazz);
        }
        level += entry.level;
    }
    if (level < 1 || level > kCoopGuestLevelCap) {
        return "level " + std::to_string(level) + " is outside 1-" + std::to_string(kCoopGuestLevelCap);
    }
    for (int ability = 0; ability < kAbilityCount; ++ability) {
        // 3-18 at creation, one point every fourth level, and item-free.
        if (guest.abilities[ability] < 3 || guest.abilities[ability] > 18 + level / 4 + 2) {
            return "ability score out of range";
        }
    }
    for (int skill = 0; skill < kSkillCount; ++skill) {
        if (guest.skills[skill] > level + 3) {
            return "skill rank above level + 3";
        }
    }
    for (auto feat : guest.feats) {
        if (!services.feats.get(static_cast<FeatType>(feat))) {
            return "unknown feat " + std::to_string(feat);
        }
    }
    for (auto spell : guest.spells) {
        if (!services.spells.get(static_cast<SpellType>(spell))) {
            return "unknown power " + std::to_string(spell);
        }
    }
    if (guest.baseHp > 30 * level + 30 || guest.credits > kMaxGuestCredits) {
        return "hit points or credits out of range";
    }
    return std::string();
}

void CoopSession::hostHandleGuest(HostClient &client, const GuestCharacter &guest) {
    if (client.guestReceived) {
        return; // the host's record is the character from here on
    }
    auto known = _guests.find(client.name);
    auto owner = _guestOwners.find(client.name);
    if (known != _guests.end() && owner != _guestOwners.end() && client.rejoinToken && owner->second == client.rejoinToken) {
        // A rejoin with the token the record belongs to: the host's copy is the current one.
        client.guestReceived = true;
        client.requestOrder = _nextRequestOrder++;
        info("Co-op: " + client.name + " rejoins as " + known->second.name);
        return;
    }
    if (known != _guests.end()) {
        // Someone else's record under this name (their token is gone): the
        // name is only a label, and this player brings their own character.
        hostRetireGuest(client.name, true);
    }
    auto why = hostCheckGuest(guest);
    if (!why.empty()) {
        warn("Co-op: refused " + client.name + "'s character: " + why);
        hostReject(client, RejectReason::Malformed, "character: " + why);
        return;
    }
    _guests[client.name] = guest;
    _guestOwners[client.name] = client.rejoinToken;
    client.guestReceived = true;
    client.requestOrder = _nextRequestOrder++;
    hostTrimAbsentGuests();
    info("Co-op: " + client.name + " brings " + guest.name + " (" + std::to_string(guest.items.size()) + " items, " +
         std::to_string(guest.credits) + " credits)");
}

void CoopSession::hostUpdateGuests() {
    if (_inModuleSnapshot) {
        return;
    }
    _standInPause = hostStandInPause();
    auto leader = _game.party().getLeader();
    uint64_t tick = _game.tickCount();
    int index = 0;
    for (auto &[connection, client] : _clients) {
        if (!client.welcomed || !client.guestReceived) {
            continue;
        }
        // Design, "Handshake" step 4: the host accepts or declines a new
        // player before their character enters the world, as in Story Mode.
        if (client.approval == HostClient::Approval::None && !client.trusted) {
            auto record = _guests.find(client.name);
            std::string character = record != _guests.end() ? record->second.name : client.name;
            if (_approver) {
                bool accept = _approver(CoopJoinRequest {client.clientId, client.name, std::string()});
                client.approval = accept ? HostClient::Approval::Accepted : HostClient::Approval::Declined;
            } else if (_config.askToJoin) {
                client.approval = HostClient::Approval::Pending;
                client.approvalTag.clear();
                client.approvalName = character + ", their own character";
                info("Co-op: " + client.name + " wants to join as " + client.approvalName + "; waiting for the host");
                hostQueueJoin(client);
            }
        }
        if (client.approval == HostClient::Approval::Declined) {
            if (client.slot.status != SlotStatus::Declined) {
                client.slot = SlotAssign();
                client.slot.status = SlotStatus::Declined;
                send(client.connection, encode(client.slot));
                hostReject(client, RejectReason::Declined, "the host declined the request");
            }
            continue;
        }
        if (client.approval == HostClient::Approval::Pending) {
            continue;
        }
        if (!client.approved) {
            hostMarkApproved(client); // accepted, or nobody to ask
        }
        std::shared_ptr<Creature> existing = client.guestId ? _game.getObjectById<Creature>(client.guestId) : nullptr;
        if (client.guestId && (!existing || !existing->isRuntimeLive())) {
            _standInSource.erase(client.guestId);
            client.guestId = 0;
            existing.reset();
        }
        // Guests leave for scenes, as the stand-ins do, and come back beside
        // the host's leader (design, "Cutscenes and conversations").
        // A guest operating a handed-over terminal stays: it is the speaker.
        bool out = (_standInPause.empty() || (client.terminalControl && existing)) && leader;
        if (!out && existing) {
            hostDespawnGuest(client);
        } else if (out && !existing) {
            hostSpawnGuest(client, *leader, index);
        } else if (existing && tick >= client.guestSyncTick + kGuestSyncTicks) {
            hostSyncGuest(client, true);
        }
        ++index;
        if (client.slotAnswered) {
            hostSendSlot(client, false);
        }
    }
}

std::shared_ptr<Creature> CoopSession::hostSpawnGuest(HostClient &client, const Creature &leader, int index) {
    auto module = _game.module();
    auto record = _guests.find(client.name);
    if (!module || !module->area() || record == _guests.end()) {
        return nullptr;
    }
    if (!client.levelFloorChecked) {
        // Design, "Guest characters": a character arrives at the host's level.
        // A lower one gets the XP for it and takes the levels through its own
        // level-up screens (the host checks each); a higher one keeps its own.
        client.levelFloorChecked = true;
        auto pc = _game.party().actualPlayer();
        int hostLevel = pc ? pc->attributes().getAggregateLevel() : 0;
        int guestLevel = 0;
        for (const auto &entry : record->second.classes) {
            guestLevel += entry.level;
        }
        if (hostLevel > guestLevel && hostLevel > 1) {
            int floor = experienceForNextLevel(_game.services().resource.twoDas, std::min(hostLevel, kCoopGuestLevelCap) - 1);
            if (record->second.xp < floor) {
                record->second.xp = floor;
                info("Co-op: " + client.name + "'s " + record->second.name + " starts at the host's level " + std::to_string(hostLevel) +
                     " (" + std::to_string(floor) + " XP)");
            }
        }
    }
    const auto &guest = record->second;
    auto &area = *module->area();
    std::shared_ptr<Creature> creature;
    try {
        creature = _game.newCreature();
        creature->setTag("coop_guest_" + std::to_string(client.clientId));
        creature->setName(guest.name);
        creature->setGender(static_cast<Gender>(guest.gender));
        creature->setAppearance(guest.appearance);
        try {
            creature->loadAppearance();
        } catch (const std::exception &e) {
            warn(std::string("Co-op: a guest's appearance did not load: ") + e.what());
        }
        creature->setFaction(Faction::Player);
        creature->attributes() = guestAttributes(_game, guest);
        creature->setGoodEvil(guest.goodEvil);
        creature->setMaxHitPoints(guest.baseHp);
        int maximum = creature->maxHitPoints();
        creature->setCurrentHitPoints(guest.currentHp > 0 ? std::min<int>(guest.currentHp, maximum) : maximum);
        creature->addBaseForcePoints(guest.baseFp);
        creature->setCurrentForcePoints(creature->maxForcePoints());
        creature->setXP(guest.xp);
        for (const auto &entry : guest.items) {
            auto item = _game.newItemFromBlueprint(entry.resref);
            if (!item) {
                continue;
            }
            item->setStackSize(entry.stack);
            if (entry.upgrades != 0) {
                item->setUpgrades(entry.upgrades);
            }
            if (entry.modelVariation >= 0) {
                item->setModelVariation(entry.modelVariation);
            }
            if (entry.slot != kGuestInventory) {
                if (!creature->equip(entry.slot, item)) {
                    creature->addItem(item);
                }
            } else {
                creature->addItem(item);
            }
        }
        // Outside the party table (K2's puppet mechanism): scripts that count
        // or check the party still see the host's party.
        creature->setPuppet(true);
    } catch (const std::exception &e) {
        warn(std::string("Co-op: cannot build a guest's character: ") + e.what());
        return nullptr;
    }
    // Back after a scene or a save: where it stood, while that spot still
    // joins the leader; else beside the leader.
    bool returned = hostPlaceAtAwaySpot(client, *creature, leader);
    bool placed = returned;
    for (const auto &offset : coopStandInOffsets(leader.getFacing(), index)) {
        if (placed) {
            break;
        }
        creature->setPosition(leader.position() + glm::vec3(offset, 0.0f));
        if (area.landObject(*creature)) {
            placed = true;
        }
    }
    if (!placed) {
        creature->setPosition(leader.position());
    }
    if (!returned) {
        creature->setFacing(leader.getFacing());
    }
    area.add(creature);
    client.guestId = creature->id();
    client.guestSyncTick = _game.tickCount();
    _standInSource[creature->id()] = creature->id();
    info("Co-op: " + client.name + "'s " + guest.name + " is in " + module->name() + " (id " + std::to_string(creature->id()) + ")");
    return creature;
}

void CoopSession::hostSyncGuest(HostClient &client, bool sendIt) {
    auto record = _guests.find(client.name);
    auto creature = client.guestId ? _game.getObjectById<Creature>(client.guestId) : nullptr;
    if (record == _guests.end()) {
        return;
    }
    auto &guest = record->second;
    if (creature) {
        writeAttributes(creature->attributes(), guest);
        guest.xp = static_cast<int32_t>(creature->xp());
        guest.baseHp = static_cast<int16_t>(std::clamp(creature->hitPoints(), 1, 32767));
        guest.currentHp = creature->isDead() ? 1 : static_cast<int16_t>(std::clamp(creature->currentHitPoints(), 1, 32767));
        guest.baseFp = static_cast<int16_t>(std::clamp(creature->forcePoints(), 0, 32767));
        guest.goodEvil = static_cast<uint8_t>(creature->goodEvil());
        guest.items.clear();
        for (const auto &[slot, item] : creature->equipment()) {
            auto resref = item ? itemResRef(*item) : std::string();
            if (!resref.empty() && slot >= 0 && slot <= 30 && guest.items.size() < kMaxGuestItems) {
                guest.items.push_back({resref, static_cast<uint16_t>(std::clamp(item->stackSize(), 1, 65535)), static_cast<uint8_t>(slot),
                                       item->upgrades(), static_cast<int16_t>(std::clamp(item->modelVariation(), -1, 1000))});
            }
        }
        for (const auto &item : creature->items()) {
            auto resref = item ? itemResRef(*item) : std::string();
            if (!resref.empty() && guest.items.size() < kMaxGuestItems) {
                guest.items.push_back({resref, static_cast<uint16_t>(std::clamp(item->stackSize(), 1, 65535)), kGuestInventory,
                                       item->upgrades(), static_cast<int16_t>(std::clamp(item->modelVariation(), -1, 1000))});
            }
        }
    }
    client.guestSyncTick = _game.tickCount();
    if (sendIt) {
        // The guest's machine keeps the character (Diablo-style): it saves
        // what the host sends.
        auto bytes = encode(guest);
        if (bytes != client.guestSent && bytes.size() <= kMaxGuestCharacterBytes) {
            client.guestSent = bytes;
            send(client.connection, bytes);
        }
    }
}

void CoopSession::hostDespawnGuest(HostClient &client) {
    if (!client.guestId) {
        return;
    }
    hostSyncGuest(client, true);
    if (client.storeOpen) {
        hostSendStore(client, false);
        client.storeOpen = 0;
    }
    if (client.benchOpen) {
        hostSendBench(client, false);
        client.benchOpen = 0;
    }
    if (client.viewing) {
        hostSendContainer(client, false);
        client.viewing = 0;
        client.viewSent.clear();
    }
    uint32_t id = client.guestId;
    client.guestId = 0;
    _standInSource.erase(id);
    auto creature = _game.getObjectById<Creature>(id);
    auto module = _game.module();
    if (creature && module && module->area()) {
        hostRememberAwaySpot(client, *creature);
        creature->clearAllActions();
        module->area()->destroyObject(*creature);
        flushDestroyedObjects(*module->area());
    }
}

void CoopSession::onPartyXP(int amount) {
    if (!isHost() || !freeMode() || amount <= 0) {
        return;
    }
    // Design, "XP: mirrors the host": each guest gets the same award.
    for (auto &[connection, client] : _clients) {
        auto record = _guests.find(client.name);
        if (!client.guestReceived || record == _guests.end()) {
            continue;
        }
        record->second.xp = static_cast<int32_t>(std::min<int64_t>(static_cast<int64_t>(record->second.xp) + amount, INT32_MAX));
        if (auto creature = client.guestId ? _game.getObjectById<Creature>(client.guestId) : nullptr) {
            creature->setXP(record->second.xp);
        }
    }
}

void CoopSession::hostSendGuestInventory(HostClient &client) {
    auto creature = client.guestId ? _game.getObjectById<Creature>(client.guestId) : nullptr;
    auto record = _guests.find(client.name);
    if (!client.welcomed || client.moduleGeneration != _generation || !creature || record == _guests.end()) {
        return;
    }
    // A guest's own bag and credits (design, "separate per player").
    SharedInventory inventory;
    inventory.owner = creature->id();
    inventory.credits = record->second.credits;
    for (const auto &item : creature->items()) {
        auto resref = item ? itemResRef(*item) : std::string();
        if (resref.empty() || inventory.items.size() >= kMaxInventoryItems) {
            continue;
        }
        InventoryItem entry;
        entry.id = item->id();
        entry.resref = resref;
        entry.stack = static_cast<uint16_t>(std::clamp(item->stackSize(), 1, 65535));
        entry.flags = item->activateSpell() ? kInventoryUsable : 0;
        inventory.items.push_back(std::move(entry));
    }
    auto bytes = encode(inventory);
    if (bytes != client.inventorySent) {
        client.inventorySent = bytes;
        send(client.connection, bytes);
    }
}

// Stores (design, "Stores": guests buy and sell with their own credits; the
// stock is shared, so a unique item one player buys is gone for everyone)

bool CoopSession::hostOpenGuestStore(HostClient &client, const std::shared_ptr<Object> &merchant) {
    auto module = _game.module();
    if (!merchant || !module || !module->area()) {
        return false;
    }
    auto &area = *module->area();
    std::shared_ptr<Store> store;
    auto tag = storeTagFromDialog(_game, merchant->conversation());
    if (!tag.empty()) {
        store = std::dynamic_pointer_cast<Store>(area.getObjectByTag(tag));
    }
    if (!store) {
        auto stores = area.getObjectsByType(ObjectType::Store);
        if (stores.size() == 1) {
            store = std::static_pointer_cast<Store>(stores.front());
        }
    }
    if (!store) {
        warn("Co-op: no store found for " + merchant->tag() + (tag.empty() ? std::string() : " (tag " + tag + ")"));
        return false;
    }
    client.storeOpen = store->id();
    client.storeSent.clear();
    info("Co-op: " + client.name + " browses " + store->tag());
    hostSendStore(client, true);
    return true;
}

void CoopSession::hostSendStore(HostClient &client, bool open) {
    StoreView view;
    view.store = client.storeOpen;
    view.open = open;
    // Free Mode: the guest's own wallet. Story Mode: the shared party pool.
    auto record = _guests.find(client.name);
    view.credits = freeMode() ? (record != _guests.end() ? record->second.credits : 0) : _game.party().gold();
    auto store = open ? _game.getObjectById<Store>(client.storeOpen) : nullptr;
    auto actor = hostControlledId(client) ? _game.getObjectById<Creature>(hostControlledId(client)) : nullptr;
    // Story Mode: the actor's non-equipped items are the shared party
    // inventory (Party::sharedInventoryReceiver), not just what this one
    // companion happens to be carrying.
    auto inventoryOwner = actor ? _game.party().sharedInventoryReceiver(actor) : nullptr;
    if (!view.store) {
        return;
    }
    if (store && actor && inventoryOwner) {
        StoreSession session(_game);
        session.open(store, actor, 0, 0);
        for (const auto &item : store->items()) {
            auto resref = item ? itemResRef(*item) : std::string();
            if (resref.empty() || view.stock.size() >= kMaxStoreItems) {
                continue;
            }
            view.stock.push_back({item->id(), resref, static_cast<uint16_t>(std::clamp(item->stackSize(), 1, 65535)),
                                  session.buyPrice(*item)});
        }
        for (const auto &item : inventoryOwner->items()) {
            auto resref = item ? itemResRef(*item) : std::string();
            if (resref.empty() || !session.isSellable(*item) || view.sellable.size() >= kMaxStoreItems) {
                continue;
            }
            view.sellable.push_back({item->id(), resref, static_cast<uint16_t>(std::clamp(item->stackSize(), 1, 65535)),
                                     session.sellPrice(*item)});
        }
    } else {
        view.open = false;
    }
    auto bytes = encode(view);
    if (bytes != client.storeSent) {
        client.storeSent = bytes;
        send(client.connection, bytes);
    }
}

CommandReject CoopSession::hostStoreCommand(HostClient &client, const Command &command, bool &handled) {
    handled = true;
    if (command.type != CommandType::StoreBuy && command.type != CommandType::StoreSell && command.type != CommandType::StoreClose) {
        handled = false;
        return CommandReject::None;
    }
    if (!client.storeOpen || command.target != client.storeOpen) {
        return CommandReject::StoreClosed;
    }
    if (command.type == CommandType::StoreClose) {
        hostSendStore(client, false);
        client.storeOpen = 0;
        return CommandReject::None;
    }
    bool story = !freeMode();
    auto store = _game.getObjectById<Store>(client.storeOpen);
    auto actor = hostControlledId(client) ? _game.getObjectById<Creature>(hostControlledId(client)) : nullptr;
    auto record = _guests.find(client.name);
    if (!store || !actor || actor->isDead() || (!story && record == _guests.end())) {
        return CommandReject::StoreClosed;
    }
    // Story Mode debits/credits the shared party pool and inventory
    // (Party::sharedInventoryReceiver); Free Mode uses the guest's own.
    auto inventoryOwner = story ? _game.party().sharedInventoryReceiver(actor) : actor;
    StoreSession session(_game);
    session.open(store, actor, 0, 0);
    auto item = _game.getObjectById<Item>(static_cast<uint32_t>(command.param));
    if (command.type == CommandType::StoreBuy) {
        const auto &stock = store->items();
        if (!store->sellsToPlayer()) {
            return CommandReject::StoreClosed;
        }
        if (!item || std::find(stock.begin(), stock.end(), item) == stock.end()) {
            return CommandReject::ItemUnavailable; // someone else bought it first
        }
        int price = session.buyPrice(*item);
        int credits = story ? _game.party().gold() : record->second.credits;
        if (credits < price) {
            return CommandReject::NotEnoughCredits;
        }
        if (story) {
            _game.party().takeGold(price);
        } else {
            record->second.credits -= price;
        }
        auto bought = _game.newItemClone(*item);
        bought->setStackSize(1);
        bought->setInfinite(false);
        bought->setOwner(0);
        inventoryOwner->addItem(bought);
        if (!item->isInfinite()) {
            bool last = false;
            store->removeItem(item, last);
            if (last) {
                _game.destroyRuntimeObjectGraph(item);
            }
        }
        info("Co-op: " + client.name + " bought " + bought->tag() + " for " + std::to_string(price) + " (" +
             std::to_string(credits - price) + " left)");
    } else {
        const auto &owned = inventoryOwner->items();
        if (!store->buysFromPlayer()) {
            return CommandReject::StoreClosed;
        }
        if (!item || std::find(owned.begin(), owned.end(), item) == owned.end() || !session.isSellable(*item)) {
            return CommandReject::ItemUnavailable;
        }
        int price = session.sellPrice(*item);
        auto sold = _game.newItemClone(*item);
        sold->setStackSize(1);
        sold->setInfinite(false);
        sold->setOwner(0);
        store->addItem(sold);
        bool last = false;
        inventoryOwner->removeItem(item, last);
        if (last) {
            _game.destroyRuntimeObjectGraph(item);
        }
        if (story) {
            _game.party().giveGold(price);
        } else {
            record->second.credits = std::min(kMaxGuestCredits, record->second.credits + price);
        }
        info("Co-op: " + client.name + " sold " + sold->tag() + " for " + std::to_string(price));
    }
    hostSendStore(client, true);
    // Every other guest trading here sees the stock change.
    for (auto &[connection, other] : _clients) {
        if (&other != &client && other.storeOpen == client.storeOpen) {
            hostSendStore(other, true);
        }
    }
    return CommandReject::None;
}

std::vector<GuestRosterEntry> CoopSession::guestList() const {
    std::vector<GuestRosterEntry> result;
    if (!freeMode()) {
        return result;
    }
    if (isClient()) {
        for (const auto &entry : _roster) {
            if (entry.name != _guest.name) {
                result.push_back(entry);
            }
        }
        return result;
    }
    for (const auto &[connection, client] : _clients) {
        auto record = _guests.find(client.name);
        if (!client.welcomed || !client.guestReceived || record == _guests.end()) {
            continue;
        }
        GuestRosterEntry entry;
        entry.name = record->second.name.substr(0, 32);
        int level = 0;
        for (const auto &clazz : record->second.classes) {
            level += clazz.level;
        }
        entry.hp = record->second.currentHp > 0 ? record->second.currentHp : record->second.baseHp;
        entry.maxHp = record->second.baseHp;
        entry.present = false;
        if (auto creature = client.guestId ? _game.getObjectById<Creature>(client.guestId) : nullptr) {
            entry.hp = static_cast<int16_t>(std::clamp(creature->currentHitPoints(), -32768, 32767));
            entry.maxHp = static_cast<int16_t>(std::clamp(creature->maxHitPoints(), 0, 32767));
            level = creature->attributes().getAggregateLevel();
            entry.present = true;
        }
        entry.level = static_cast<uint8_t>(std::clamp(level, 1, 50));
        if (result.size() < 16) {
            result.push_back(std::move(entry));
        }
    }
    return result;
}

void CoopSession::hostSendRoster() {
    GuestRoster roster;
    roster.entries = guestList();
    auto bytes = encode(roster);
    if (bytes == _rosterSent) {
        return;
    }
    _rosterSent = bytes;
    for (auto &[connection, client] : _clients) {
        if (client.welcomed) {
            send(connection, bytes);
        }
    }
}

// Store and minigame prompts (design, "Store and minigame prompts for
// non-hosts"): a guest who clicks an NPC whose conversation leads to a
// store or a minigame gets a prompt instead. The dialog's own scripts never
// run, so no story state changes; a guest's Pazaak wagers only its own
// credits, and module minigames (swoop races, turrets) stay the host's.

namespace {

/** Largest Pazaak wager a guest's own game takes. */
constexpr int32_t kGuestPazaakMaxWager = 100;

OfferKind offerKindFromDialog(Game &game, const std::string &dialogResRef) {
    if (dialogResRef.empty()) {
        return OfferKind::None;
    }
    auto &routines = game.routines();
    int openStore = routines.getIndexByName("OpenStore");
    int playPazaak = routines.getIndexByName("PlayPazaak");
    int startNewModule = routines.getIndexByName("StartNewModule");
    OfferKind kind = OfferKind::None;
    try {
        auto dialog = game.services().resource.dialogs.get(boost::to_lower_copy(dialogResRef));
        if (!dialog) {
            return OfferKind::None;
        }
        for (const auto *nodes : {&dialog->entries, &dialog->replies}) {
            for (const auto &node : *nodes) {
                for (const auto *name : {&node.script, &node.script2}) {
                    if (name->empty()) {
                        continue;
                    }
                    auto program = game.services().resource.scripts.get(boost::to_lower_copy(*name));
                    if (!program) {
                        continue;
                    }
                    bool minigameModule = false;
                    for (const auto &instruction : program->instructions()) {
                        if (instruction.type == script::InstructionType::CONSTS) {
                            auto value = boost::to_lower_copy(instruction.strValue);
                            minigameModule = minigameModule || (value.size() > 2 && value.compare(value.size() - 2, 2, "mg") == 0);
                        } else if (instruction.type == script::InstructionType::ACTION) {
                            if (instruction.routine == openStore) {
                                return OfferKind::Store;
                            }
                            if (instruction.routine == playPazaak) {
                                kind = OfferKind::Pazaak;
                            } else if (instruction.routine == startNewModule && minigameModule && kind == OfferKind::None) {
                                kind = OfferKind::MiniGame;
                            }
                        }
                    }
                }
            }
        }
    } catch (const std::exception &) {
        return OfferKind::None;
    }
    return kind;
}

std::string refusalText(const Object &npc, OfferKind kind) {
    auto name = npc.name().empty() ? npc.tag() : npc.name();
    return name.substr(0, 64) + (kind == OfferKind::Pazaak ? " won't play Pazaak right now" : " won't trade right now");
}

} // namespace

bool CoopSession::hostDialogReaches(HostClient &client, const Object &npc, OfferKind kind) {
    uint32_t saved = _pcSpeaker;
    _pcSpeaker = hostControlledId(client);
    bool reaches = coopDialogReachesOffer(_game, npc, kind);
    _pcSpeaker = saved;
    return reaches;
}

bool CoopSession::hostRefusesTrade(HostClient &client, const Object &merchant) {
    if (offerKindFromDialog(_game, merchant.conversation()) != OfferKind::Store || hostDialogReaches(client, merchant, OfferKind::Store)) {
        return false;
    }
    hostNotice(client, NoticeKind::Refusal, refusalText(merchant, OfferKind::Store));
    info("Co-op: " + merchant.tag() + " won't trade with " + client.name + " right now");
    return true;
}

bool CoopSession::hostOfferGuest(HostClient &client, const std::shared_ptr<Object> &npc) {
    if (!npc) {
        return false;
    }
    auto kind = offerKindFromDialog(_game, npc->conversation());
    if (kind == OfferKind::None) {
        return false;
    }
    if ((kind == OfferKind::Store || kind == OfferKind::Pazaak) && !hostDialogReaches(client, *npc, kind)) {
        // The retail conversation would not get there now (a quest not done,
        // a merchant who refuses): no prompt, a notice instead.
        if (kind == OfferKind::Store && hostDialogReaches(client, *npc, OfferKind::Pazaak)) {
            kind = OfferKind::Pazaak;
        } else {
            hostNotice(client, NoticeKind::Refusal, refusalText(*npc, kind));
            client.offer = Offer();
            info("Co-op: " + npc->tag() + " won't " + (kind == OfferKind::Store ? "trade" : "play Pazaak") + " with " + client.name + " right now");
            return true;
        }
    }
    auto record = _guests.find(client.name);
    Offer offer;
    offer.kind = kind;
    offer.target = npc->id();
    offer.name = (npc->name().empty() ? npc->tag() : npc->name()).substr(0, 64);
    offer.maximumWager = kind == OfferKind::Pazaak && record != _guests.end() ? std::min(record->second.credits, kGuestPazaakMaxWager) : 0;
    client.offer = offer;
    client.pazaakPlaying = false;
    send(client.connection, encode(offer));
    info("Co-op: offered " + client.name + " a " +
         (kind == OfferKind::Store ? "store" : kind == OfferKind::Pazaak ? "Pazaak game" : "minigame") + " at " + npc->tag());
    return true;
}

CommandReject CoopSession::hostOfferCommand(HostClient &client, const Command &command, bool &handled) {
    handled = true;
    if (command.type == CommandType::PickReply) {
        // A reply in the terminal session the host handed this guest.
        bool active = false;
        if (client.terminalControl && _game.currentScreen() == Game::Screen::Conversation && _game._conversation) {
            auto state = _game._conversation->inspectionState();
            active = state.owner && state.owner->id() == client.terminalControl;
        }
        if (!active) {
            return CommandReject::NotYourTerminal;
        }
        return _game._conversation->pickReplyAtRow(command.param) ? CommandReject::None : CommandReject::TargetInvalid;
    }
    if (command.type != CommandType::OfferAnswer && command.type != CommandType::PazaakResult) {
        handled = false;
        return CommandReject::None;
    }
    if (!freeMode() || client.offer.kind == OfferKind::None || command.target != client.offer.target) {
        return CommandReject::NoOffer;
    }
    if (command.type == CommandType::OfferAnswer) {
        if (command.param != 1) {
            client.offer = Offer();
            return CommandReject::None;
        }
        switch (client.offer.kind) {
        case OfferKind::Store: {
            auto npc = _game.getObjectById(client.offer.target);
            client.offer = Offer();
            return npc && hostOpenGuestStore(client, npc) ? CommandReject::None : CommandReject::StoreClosed;
        }
        case OfferKind::Pazaak:
            client.pazaakPlaying = true;
            info("Co-op: " + client.name + " plays Pazaak at their own table (no story effect)");
            return CommandReject::None;
        default:
            return hostRequestGuestMiniGame(client);
        }
    }
    // The guest's own Pazaak game ended: settle its own credits, nothing else.
    if (!client.pazaakPlaying) {
        return CommandReject::NoOffer;
    }
    int32_t delta = command.param;
    auto record = _guests.find(client.name);
    if (record == _guests.end() || std::abs(delta) > client.offer.maximumWager || record->second.credits + delta < 0) {
        client.offer = Offer();
        client.pazaakPlaying = false;
        return CommandReject::NoOffer;
    }
    record->second.credits += delta;
    client.offer = Offer();
    client.pazaakPlaying = false;
    info("Co-op: " + client.name + (delta >= 0 ? " won " : " lost ") + std::to_string(std::abs(delta)) + " credits at Pazaak");
    return CommandReject::None;
}

void CoopSession::clientHandleOffer(const Offer &offer) {
    _offer = offer;
    if (offer.kind == OfferKind::MiniGame) {
        // A prompt like the others: the answer starts the minigame on the host, which this player then drives.
        if (!showsPrompts()) {
            return; // headless: coopoffer answers
        }
        if (!_game._confirmPopup) {
            _game._confirmPopup = _game.tryLoadGUI<ConfirmPopup>();
        }
        if (_game._confirmPopup) {
            _game._confirmPopup->showChoices(offer.name + " offers a race.",
                                             {{"Race", [this]() { clientAnswerOffer(true); }}, {"Cancel", [this]() { clientAnswerOffer(false); }}});
        }
        return;
    }
    if (!showsPrompts()) {
        return; // headless: coopoffer answers
    }
    if (!_game._confirmPopup) {
        _game._confirmPopup = _game.tryLoadGUI<ConfirmPopup>();
    }
    if (!_game._confirmPopup) {
        return;
    }
    bool store = offer.kind == OfferKind::Store;
    _game._confirmPopup->showChoices(store ? offer.name + " has goods for sale." : offer.name + " offers a game of Pazaak.",
                                     {{store ? "Browse store" : "Play Pazaak", [this]() { clientAnswerOffer(true); }},
                                      {"Cancel", [this]() { clientAnswerOffer(false); }}});
}

bool CoopSession::clientAnswerOffer(bool yes) {
    if (!isClient() || _offer.kind == OfferKind::None) {
        return false;
    }
    submitRequest(CommandType::OfferAnswer, _offer.target, yes ? 1 : 0);
    if (yes && _offer.kind == OfferKind::Pazaak) {
        // A Pazaak game of this guest's own, on its own machine: it runs no
        // script, so the host's story never hears of it.
        int wager = std::min<int>(_offer.maximumWager, _game.party().gold());
        _guestPazaak = _game.startDevelopmentPazaak(_offer.name, std::max(0, wager));
        if (!_guestPazaak) {
            _offer = Offer();
        }
        return true;
    }
    _offer = Offer();
    return true;
}

void CoopSession::clientPazaakFinished(bool won, int wager) {
    if (!_guestPazaak) {
        return;
    }
    _guestPazaak = false;
    submitRequest(CommandType::PazaakResult, _offer.target, won ? wager : -wager);
    _offer = Offer();
}

// Computer terminals in Free Mode (design, "Computer terminals" item 5):
// guests use story terminals only through the host's hand-over; info-only
// terminals (logs, messages) can be read by anyone at any time. A story
// terminal is one whose dialog's scripts change story state or journal.

namespace {

/** 0: not a terminal; 1: info-only; 2: a story terminal. Read from the data, never run. */
int terminalKind(Game &game, const std::string &dialogResRef) {
    static std::map<std::string, int> cache;
    auto key = boost::to_lower_copy(dialogResRef);
    if (key.empty()) {
        return 0;
    }
    auto cached = cache.find(key);
    if (cached != cache.end()) {
        return cached->second;
    }
    int kind = 0;
    try {
        auto dialog = game.services().resource.dialogs.get(key);
        if (dialog && dialog->conversationType == resource::ConversationType::Computer) {
            kind = 1;
            for (const auto *nodes : {&dialog->entries, &dialog->replies}) {
                for (const auto &node : *nodes) {
                    if (!node.quest.empty() || coopScriptChangesStory(game, node.script) || coopScriptChangesStory(game, node.script2)) {
                        kind = 2;
                    }
                }
            }
        }
    } catch (const std::exception &) {
        kind = 0;
    }
    cache[key] = kind;
    return kind;
}

} // namespace

CommandReject CoopSession::hostGuestTerminal(HostClient &client, const std::shared_ptr<Object> &terminal) {
    int kind = terminalKind(_game, terminal->conversation());
    if (kind == 0) {
        return CommandReject::HostLeadsConversation;
    }
    if (kind == 1) {
        // Changes nothing: the guest reads it on its own screen.
        hostNotice(client, NoticeKind::TerminalRead, std::to_string(terminal->id()) + " " + boost::to_lower_copy(terminal->conversation()));
        info("Co-op: " + client.name + " reads " + terminal->tag());
        return CommandReject::None;
    }
    if (!client.handedOver.count(terminal->id())) {
        hostPromptHandOver(client, *terminal);
        return CommandReject::NotHandedOver;
    }
    if (_game.currentScreen() == Game::Screen::Conversation || client.terminalControl) {
        return CommandReject::Busy;
    }
    // The host's own session of the terminal, shown to everyone as
    // conversations are; the guest makes the choices and its character's
    // skills make the checks.
    _pcSpeaker = client.guestId;
    client.terminalControl = terminal->id();
    client.terminalStartTick = _game.tickCount();
    _game.startDialog(terminal, terminal->conversation());
    hostNotice(client, NoticeKind::TerminalControl, "on");
    info("Co-op: " + client.name + " operates " + terminal->tag());
    return CommandReject::None;
}

void CoopSession::hostUpdateTerminals() {
    for (auto &[connection, client] : _clients) {
        if (!client.terminalControl) {
            continue;
        }
        bool active = false;
        if (_game.currentScreen() == Game::Screen::Conversation && _game._conversation) {
            auto state = _game._conversation->inspectionState();
            active = state.owner && state.owner->id() == client.terminalControl;
        }
        if (active || _game.tickCount() < client.terminalStartTick + 30) {
            continue;
        }
        client.terminalControl = 0;
        _pcSpeaker = 0;
        hostNotice(client, NoticeKind::TerminalControl, "off");
    }
}

void CoopSession::clientHandleTerminalNotice(const Notice &notice) {
    if (notice.kind == NoticeKind::TerminalControl) {
        _clientTerminal = notice.text == "on";
        showNotice(NoticeKind::Info, _clientTerminal ? "You operate the terminal" : "The terminal session ended");
        return;
    }
    auto space = notice.text.find(' ');
    if (space == std::string::npos) {
        return;
    }
    uint32_t host = 0;
    try {
        host = static_cast<uint32_t>(std::stoul(notice.text.substr(0, space)));
    } catch (const std::exception &) {
        return;
    }
    auto dialog = notice.text.substr(space + 1);
    auto local = localObjectForHost(host);
    if (!local || dialog.empty() || dialog.size() > 16) {
        return;
    }
    // An info-only terminal read on this machine: this client runs no
    // scripts, so reading it changes nothing anywhere.
    _terminalRead = dialog;
    _game.startDialog(local, dialog);
}

bool CoopSession::clientPickReply(int row) {
    if (!isClient() || !_clientTerminal || row < 0) {
        return false;
    }
    submitRequest(CommandType::PickReply, 0, row);
    return true;
}

bool CoopSession::setGuestCredits(uint32_t clientId, int credits) {
    for (auto &[connection, client] : _clients) {
        auto record = _guests.find(client.name);
        if (client.clientId == clientId && record != _guests.end()) {
            record->second.credits = std::clamp(credits, 0, kMaxGuestCredits);
            return true;
        }
    }
    return false;
}

// Client

std::filesystem::path CoopSession::guestPath() const {
    std::string name;
    for (char c : _config.playerName) {
        name += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    }
    return std::filesystem::current_path() / ("coop_guest_" + (name.empty() ? std::string("player") : name) + ".bin");
}

void CoopSession::clientSaveGuest() const {
    auto bytes = encode(_guest);
    try {
        auto path = guestPath();
        auto temp = path;
        temp += ".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        std::filesystem::rename(temp, path);
    } catch (const std::exception &e) {
        warn(std::string("Co-op: could not save the guest character: ") + e.what());
    }
}

void CoopSession::clientAfterWelcomeFree() {
    if (_guestSent) {
        // A reconnect: the host keeps our character; say who we are again.
        send(_hostConnection, encode(_guest));
        return;
    }
    std::vector<uint8_t> bytes;
    try {
        std::ifstream in(guestPath(), std::ios::binary);
        if (in) {
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
    } catch (const std::exception &) {
        bytes.clear();
    }
    GuestCharacter guest;
    if (!bytes.empty() && decode(bytes, guest)) {
        info("Co-op: bringing " + guest.name + " from " + guestPath().string());
        clientSetGuest(guest);
        return;
    }
    // First join: full character generation (design, "Created with the full
    // K1 character creation"). Headless runs use the newgame command.
    info("Co-op: no guest character yet; character generation makes one");
    if (!_game._options.graphics.headless) {
        clientOpenGuestCreation();
    }
}

void CoopSession::clientOpenGuestCreation() {
    // Character generation without starting a new game: nothing of the
    // host's world is touched, and finishing hands the character to co-op.
    if (!_game._charGen) {
        _game._charGen = _game.tryLoadGUI<CharacterGeneration>();
    }
    if (_game._charGen) {
        _game.changeScreen(Game::Screen::CharacterGeneration);
    }
}

void CoopSession::clientSetGuest(const GuestCharacter &guest) {
    _guest = guest;
    clientSaveGuest();
    ++_guestSaves;
    if (_hostConnection && _clientState == ClientState::Joined) {
        send(_hostConnection, encode(_guest));
        _guestSent = true;
    }
    info("Co-op: playing " + guest.name);
    // The held module loads on the next network poll, not from inside the
    // character generation screen that called this.
}

void CoopSession::clientHandleGuest(const GuestCharacter &guest) {
    _guest = guest;
    clientSaveGuest();
    ++_guestSaves;
}

void CoopSession::clientHandleStore(const StoreView &view) {
    _storeView = view;
    if (!view.open) {
        _storeView.open = false;
        if (_game.currentScreen() == Game::Screen::Store) {
            _game.closeStore();
        }
        return;
    }
    auto store = std::dynamic_pointer_cast<Store>(_localStore);
    if (!store || !store->isRuntimeLive()) {
        store = _game.newStore();
        store->setTag("coop_store");
        _localStore = store;
    }
    // The local shell holds the host's stock, from this install's templates.
    std::vector<std::shared_ptr<Item>> old(store->items().begin(), store->items().end());
    for (auto &item : old) {
        store->removeItemStack(item);
        _game.destroyRuntimeObjectGraph(item);
    }
    _storeLocal.clear();
    for (const auto &entry : view.stock) {
        auto item = _game.newItemFromBlueprint(entry.resref);
        if (!item) {
            continue;
        }
        item->setStackSize(entry.stack);
        store->addItem(item);
        _storeLocal[item->id()] = entry.id;
    }
    if (_game.currentScreen() != Game::Screen::Store) {
        auto leader = _game.party().getLeader();
        _game.openStore(store, leader, 0, 0);
    }
}

bool CoopSession::clientStoreBuy(uint32_t localItemId) {
    auto it = _storeLocal.find(localItemId);
    if (!clientStoreOpen() || it == _storeLocal.end()) {
        return clientStoreOpen();
    }
    submitRequest(CommandType::StoreBuy, _storeView.store, static_cast<int32_t>(it->second));
    return true;
}

bool CoopSession::clientStoreSell(uint32_t localItemId) {
    uint32_t host = clientHostItemId(localItemId);
    if (!clientStoreOpen()) {
        return false;
    }
    if (host) {
        submitRequest(CommandType::StoreSell, _storeView.store, static_cast<int32_t>(host));
    }
    return true;
}

bool CoopSession::clientStoreClosed() {
    if (!clientStoreOpen()) {
        return false;
    }
    _storeView.open = false;
    submitRequest(CommandType::StoreClose, _storeView.store, 0);
    return true;
}

} // namespace game

} // namespace reone
