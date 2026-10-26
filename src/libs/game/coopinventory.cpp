/* Copyright (c) 2026 The reone project contributors */

// Co-op completeness (design note coop_net, "Co-op completeness"): the
// client's own use of the shared inventory (medpacs, usable items, equipping
// its companion), its companion's level-up, placeables it may use, and the
// host's barks. The host checks and carries out every request; a client
// shows what the host sends.

#include "reone/system/portablemath.h"
#include "reone/game/coopsession.h"

#include <algorithm>
#include <cmath>

#include <boost/algorithm/string.hpp>

#include "reone/game/action/opencontainer.h"
#include "reone/game/coopcommand.h"
#include "reone/game/coopcontrol_policy.h"
#include "reone/game/coopprogress.h"
#include "reone/game/d20/attributes.h"
#include "reone/game/d20/spells.h"
#include "reone/game/equipmentoperation.h"
#include "reone/game/game.h"
#include "reone/game/gui/confirmpopup.h"
#include "reone/game/gui/hud.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/game/d20/spell.h"
#include "reone/game/contextaction.h"
#include "reone/game/player.h"
#include "reone/system/logutil.h"

using namespace reone::net;

namespace reone {

namespace game {

namespace {

/** The number of equipment slots a creature has (InventorySlots). */
constexpr int kMaxEquipmentSlot = 19;

bool parseSlot(const std::string &text, int &slot) {
    if (text.empty() || text.size() > 2 ||
        !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return false;
    }
    slot = std::stoi(text);
    return slot >= 0 && slot <= kMaxEquipmentSlot;
}

} // namespace

// Host

CommandReject CoopSession::hostCompanionCommand(HostClient &client, const Command &command, bool &handled) {
    handled = true;
    if (command.type != CommandType::Equip && command.type != CommandType::LevelUp) {
        handled = false;
        return CommandReject::None;
    }
    // Orders for the companion itself, not actions in the world: its own
    // equipment and its level-up. A stand-in has neither.
    // A Free Mode guest's own character equips from its own bag and levels
    // up to the level cap by itself (design, "Guest characters").
    bool guest = client.guestId != 0 && client.slot.status != SlotStatus::Assigned;
    uint32_t controlled = client.slot.status == SlotStatus::Assigned ? client.slot.creatureId : client.guestId;
    if (!controlled) {
        return client.standIn ? CommandReject::NotSupported : CommandReject::NoSlot;
    }
    if (command.actor != controlled) {
        return CommandReject::NotControlled;
    }
    auto &party = _game.party();
    auto actor = _game.getObjectById<Creature>(controlled);
    std::shared_ptr<Creature> owner = guest ? actor : party.actualPlayer(); // holds the items it equips from
    if (!actor || !actor->isRuntimeLive() || actor->isDead() || !(guest || party.isMember(*actor)) || !owner) {
        return CommandReject::ActorUnavailable;
    }
    if (_game.movie() || (_game.currentScreen() == Game::Screen::Conversation && !coopPersonalConversation(_game))) {
        return CommandReject::Busy;
    }
    if (command.type == CommandType::Equip) {
        // Design item 4: each player equips their own character from the
        // shared inventory; the host settles conflicts (first request wins).
        int slot = -1;
        if (!parseSlot(command.item, slot)) {
            return CommandReject::TargetInvalid;
        }
        std::shared_ptr<Item> item;
        if (command.param != 0) {
            item = _game.getObjectById<Item>(static_cast<uint32_t>(command.param));
            const auto &items = owner->items();
            if (!item || std::find(items.begin(), items.end(), item) == items.end()) {
                return CommandReject::ItemUnavailable;
            }
            auto decision = evaluateEquipmentCandidate(*actor, slot, item.get());
            if (!decision.valid) {
                return CommandReject::EquipRejected;
            }
        } else if (!actor->getEquippedItem(slot)) {
            return CommandReject::ItemUnavailable;
        }
        auto outcome = applyEquipmentOperation(_game, *actor, *owner, item, slot);
        if (outcome == EquipmentOperationOutcome::Rejected || outcome == EquipmentOperationOutcome::Failed) {
            return CommandReject::EquipRejected;
        }
        info("Co-op: " + client.name + (item ? " equipped " + item->tag() : std::string(" cleared a slot")) + " on " +
             actor->tag() + " (slot " + std::to_string(slot) + ")");
        return CommandReject::None;
    }
    // Level-up (design item 5): within the host's level.
    CoopLevelUpChoice choice;
    if (!decodeCoopLevelUp(command.item, choice)) {
        return CommandReject::LevelUpInvalid;
    }
    int hostLevel = guest ? kCoopGuestLevelCap : owner->attributes().getAggregateLevel();
    CreatureAttributes result;
    auto reason = validateCoopLevelUp(_game, *actor, hostLevel, choice, result);
    if (reason != CommandReject::None) {
        return reason;
    }
    int before = actor->attributes().getAggregateLevel();
    applyCoopLevelUp(_game, *actor, result);
    info("Co-op: " + client.name + " levelled " + actor->tag() + " " + std::to_string(before) + " -> " +
         std::to_string(actor->attributes().getAggregateLevel()) + " (" + command.item + ")");
    return CommandReject::None;
}

void CoopSession::hostSendInventories() {
    if (freeMode()) {
        for (auto &[connection, client] : _clients) {
            hostSendGuestInventory(client);
        }
        return;
    }
    auto owner = _game.party().actualPlayer();
    SharedInventory inventory;
    if (owner) {
        inventory.owner = owner->id();
        inventory.credits = _game.party().gold();
        for (const auto &item : owner->items()) {
            if (!item || inventory.items.size() >= kMaxInventoryItems) {
                continue;
            }
            auto resref = boost::to_lower_copy(item->blueprintResRef());
            if (resref.empty() || resref.size() > 16) {
                continue; // nothing a client could build from its own install
            }
            InventoryItem entry;
            entry.id = item->id();
            entry.resref = resref;
            entry.stack = static_cast<uint16_t>(std::clamp(item->stackSize(), 1, 65535));
            entry.flags = item->activateSpell() ? kInventoryUsable : 0;
            inventory.items.push_back(std::move(entry));
        }
    }
    auto bytes = encode(inventory);
    for (auto &[connection, client] : _clients) {
        if (!client.welcomed || client.moduleGeneration != _generation || client.inventorySent == bytes) {
            continue;
        }
        client.inventorySent = bytes;
        send(connection, bytes);
    }
}

void CoopSession::hostSendSheets() {
    auto owner = _game.party().actualPlayer();
    int hostLevel = owner ? owner->attributes().getAggregateLevel() : 0;
    for (auto &[connection, client] : _clients) {
        if (!client.welcomed || client.moduleGeneration != _generation) {
            continue;
        }
        uint32_t id = hostControlledId(client);
        auto creature = id ? _game.getObjectById<Creature>(id) : nullptr;
        if (!creature) {
            continue;
        }
        bool companion = client.slot.status == SlotStatus::Assigned;
        bool allowed = companion ? creature->attributes().getAggregateLevel() < hostLevel
                                 : client.guestId == id && creature->attributes().getAggregateLevel() < kCoopGuestLevelCap;
        auto bytes = encode(coopCharacterSheet(*creature, allowed));
        if (client.sheetSent == bytes) {
            continue;
        }
        client.sheetSent = bytes;
        send(connection, bytes);
    }
}

bool CoopSession::handOver(uint32_t clientId, uint32_t objectId, bool on) {
    for (auto &[connection, client] : _clients) {
        if (client.clientId != clientId || !client.welcomed) {
            continue;
        }
        if (on) {
            client.handedOver.insert(objectId);
            hostNotice(client, NoticeKind::Info, "The host handed you a terminal");
        } else {
            client.handedOver.erase(objectId);
        }
        info("Co-op: the host " + std::string(on ? "handed " : "took back ") + std::to_string(objectId) + (on ? " to " : " from ") +
             client.name);
        return true;
    }
    return false;
}

void CoopSession::hostPromptHandOver(HostClient &client, const Object &object) {
    // The terminal design: the host decides. With a screen, the host gets a
    // Hand over / Keep prompt; headless runs answer with coophandover.
    if (!showsPrompts() || _game.currentScreen() != Game::Screen::InGame) {
        return;
    }
    if (!_game._confirmPopup) {
        _game._confirmPopup = _game.tryLoadGUI<ConfirmPopup>();
    }
    if (!_game._confirmPopup || _game._confirmPopup->isVisible()) {
        return;
    }
    uint32_t clientId = client.clientId;
    uint32_t objectId = object.id();
    std::string name = object.name().empty() ? object.tag() : object.name();
    _game._confirmPopup->showChoices(client.name + " wants to use " + name + ".",
                                     {{"Hand over", [this, clientId, objectId]() { handOver(clientId, objectId, true); }},
                                      {"Keep", []() {}}});
}

void CoopSession::hostUsePlaceable(HostClient &client, const std::shared_ptr<Creature> &actor, const std::shared_ptr<Object> &target) {
    // Walk there as a click does, then use it as the host's own click would
    // (the placeable's OnUsed, with the companion as the user).
    actor->clearAllActions();
    auto action = _game.newAction<OpenContainerAction>(target);
    std::string name = client.name;
    uint32_t actorId = actor->id();
    action->setOnReached([this, name, actorId](const std::shared_ptr<Object> &object) {
        auto placeable = std::dynamic_pointer_cast<Placeable>(object);
        auto user = _game.getObjectById<Creature>(actorId);
        if (!placeable || !user || user->isDead()) {
            return;
        }
        info("Co-op: " + name + " used " + placeable->tag());
        placeable->runOnUsed(user);
    });
    actor->addAction(std::move(action));
}

void CoopSession::onBark(const std::string &text, float duration) {
    if (!isHost()) {
        return;
    }
    Bark bark;
    bark.text = text.substr(0, kMaxStringBytes);
    bark.duration = std::isfinite(duration) ? std::clamp(duration, 0.0f, 600.0f) : 0.0f;
    auto bytes = encode(bark);
    for (auto &[connection, client] : _clients) {
        if (client.welcomed) {
            send(connection, bytes);
        }
    }
    ++_barksSent;
}

// Client

void CoopSession::clientHandleInventory(const SharedInventory &inventory) {
    ++_inventoryUpdates;
    _inventoryCredits = inventory.credits;
    if (freeMode()) {
        // A guest's own credits, as the local party's: the store screen reads them.
        auto &party = _game.party();
        party.takeGold(party.gold());
        party.giveGold(inventory.credits);
    }
    auto player = _game.party().player();
    if (!player) {
        return;
    }
    // The shared inventory lives on the host's player character, as on the
    // host: this copy is what the client's own inventory, equipment and
    // action bar screens list.
    std::set<uint32_t> wanted;
    for (const auto &entry : inventory.items) {
        wanted.insert(entry.id);
    }
    for (auto it = _invHost.begin(); it != _invHost.end();) {
        auto local = _game.getObjectById<Item>(it->second);
        if (!wanted.count(it->first) || !local) {
            if (local) {
                player->removeItemStack(local);
                _game.destroyRuntimeObjectGraph(local);
                _invLocal.erase(local->id());
            }
            it = _invHost.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto &entry : inventory.items) {
        auto found = _invHost.find(entry.id);
        std::shared_ptr<Item> local = found != _invHost.end() ? _game.getObjectById<Item>(found->second) : nullptr;
        if (local && !boost::iequals(local->blueprintResRef(), entry.resref)) {
            player->removeItemStack(local);
            _invLocal.erase(local->id());
            _game.destroyRuntimeObjectGraph(local);
            local.reset();
        }
        if (!local) {
            local = _game.newItemFromBlueprint(entry.resref);
            if (!local) {
                continue;
            }
            local->setStackSize(entry.stack);
            player->addItem(local);
            _invHost[entry.id] = local->id();
            _invLocal[local->id()] = entry.id;
        } else if (local->stackSize() != entry.stack) {
            local->setStackSize(entry.stack);
        }
    }
}

void CoopSession::clientHandleSheet(const CharacterSheet &sheet) {
    ++_sheetUpdates;
    _sheet = sheet;
    _sheetValid = true;
    _equipHostIds.clear();
    for (const auto &entry : sheet.equipment) {
        _equipHostIds[entry.slot] = entry.id;
    }
    auto creature = std::dynamic_pointer_cast<Creature>(localObjectForHost(sheet.creature));
    if (creature) {
        applyCoopCharacterSheet(_game, *creature, sheet);
    }
}

void CoopSession::clientHandleBark(const Bark &bark) {
    ++_barksReceived;
    _lastBark = bark.text;
    if (_game._hud) {
        _game._hud->barkBubble().setBarkText(bark.text, bark.duration);
    }
}

uint32_t CoopSession::clientHostItemId(uint32_t localItemId) const {
    auto it = _invLocal.find(localItemId);
    return it != _invLocal.end() ? it->second : 0;
}

bool CoopSession::clientUseItem(const std::shared_ptr<Item> &localItem, uint32_t targetHostId) {
    if (!isClient()) {
        return false;
    }
    uint32_t hostItem = localItem ? clientHostItemId(localItem->id()) : 0;
    if (!hostItem) {
        showNotice(NoticeKind::Refusal, "That item is not in the shared inventory");
        return true;
    }
    Command command;
    command.type = CommandType::UseItem;
    command.param = static_cast<int32_t>(hostItem);
    command.item = localItem->tag().substr(0, 32);
    command.target = targetHostId;
    submitCommand(command);
    return true;
}

bool CoopSession::clientEquip(const std::shared_ptr<Item> &localItem, int slot) {
    if (!isClient()) {
        return false;
    }
    Command command;
    command.type = CommandType::Equip;
    command.item = std::to_string(slot);
    if (localItem) {
        uint32_t hostItem = clientHostItemId(localItem->id());
        if (!hostItem) {
            showNotice(NoticeKind::Refusal, "That item is not in the shared inventory");
            return true;
        }
        command.param = static_cast<int32_t>(hostItem);
    }
    submitCommand(command);
    return true;
}

bool CoopSession::clientMayLevelUp() const {
    return isClient() && _sheetValid && (_slot.status == SlotStatus::Assigned || _slot.status == SlotStatus::Guest) && _sheet.creature == _slot.creatureId &&
           _sheet.levelUp;
}

bool CoopSession::clientLevelUp(const CreatureAttributes &before, const CreatureAttributes &after, int clazz) {
    if (!isClient()) {
        return false;
    }
    auto choice = coopLevelUpDiff(before, after, clazz);
    Command command;
    command.type = CommandType::LevelUp;
    command.item = encodeCoopLevelUp(choice);
    uint32_t seq = submitCommand(command);
    info("Co-op: asked the host to level up (" + command.item + ", seq " + std::to_string(seq) + ")");
    return true;
}


// Live-session fixes (alpha 0.2.1 playtest)

// The session's own view of the control policy's world facts (the policy's
// adapter reads Game::coop(), which a session under test need not be).
static coopcontrol::WorldFacts sessionControlWorld(bool host, bool client, bool dedicated, bool hostOnlyScenes) {
    coopcontrol::WorldFacts world;
    world.coopHost = host;
    world.coopClient = client;
    world.dedicated = dedicated;
    world.hostOnlyScenes = hostOnlyScenes;
    return world;
}

uint32_t CoopSession::scriptActor(uint32_t objectId, const std::string &script) const {
    // The rule is the control policy's (coopcontrol::scriptActorFor).
    auto world = sessionControlWorld(isHost(), isClient(), _config.dedicated, _config.hostOnlyScenes);
    coopcontrol::CreatureFacts facts;
    facts.guestControlled = isPlayerControlled(objectId);
    if (coopcontrol::scriptActorFor(facts, world, false) == coopcontrol::ScriptActor::Self) {
        return objectId;
    }
    // A script that names this creature's tag is about the creature itself:
    // tar02's Sith base trigger opens for GetTag(GetEnteringObject()) "t3m4",
    // the Endar Spire's doors compare with GetObjectByTag("end_trask").
    bool namesTag = false;
    if (!script.empty()) {
        auto creature = _game.getObjectById<Creature>(objectId);
        namesTag = creature && coopScriptNamesTag(_game, script, creature->tag());
    }
    if (coopcontrol::scriptActorFor(facts, world, namesTag) == coopcontrol::ScriptActor::Self) {
        return objectId;
    }
    auto pc = _game.party().player();
    return pc ? pc->id() : objectId;
}

bool CoopSession::hostHoldsScene(uint32_t objectId, const std::string &script) const {
    // The rule is the control policy's (coopcontrol::sceneStartAllowed).
    // A dedicated host has no character of its own: its story driver starts scenes as before.
    auto world = sessionControlWorld(isHost(), isClient(), _config.dedicated, _config.hostOnlyScenes);
    coopcontrol::CreatureFacts facts;
    facts.guestControlled = isPlayerControlled(objectId);
    if (script.empty() || !coopcontrol::sceneRuleApplies(facts, world)) {
        return false;
    }
    if (!coopScriptStartsScene(_game, script)) {
        return false;
    }
    // Only what a player starts by its own input waits for the host: a
    // script that moved the creature here (a door's jump, a scripted walk)
    // runs the scene as it would in single player.
    bool movedByScript = false;
    if (auto creature = _game.getObjectById<Creature>(objectId)) {
        uint64_t now = _game.tickCount();
        uint64_t window = static_cast<uint64_t>(std::max(1, _game.tickRate()));
        movedByScript = creature->scriptDrivenSince(now > window ? now - window : 1);
        if (movedByScript) {
            debug("Co-op: a script moved " + creature->tag() + " into " + script + "; its scene runs");
        }
    }
    if (coopcontrol::sceneStartAllowed(facts, world, true, movedByScript)) {
        return false;
    }
    debug("Co-op: a player's creature walked into " + script + "; its scene waits for the host's character");
    return true;
}

bool CoopSession::clientContextAction(const ContextAction &action, const std::shared_ptr<Object> &target) {
    if (!isClient()) {
        return false;
    }
    auto host = target ? _ids.host(target->id()) : std::nullopt;
    if (!host) {
        return true;
    }
    Command command;
    command.target = *host;
    switch (action.type) {
    case ActionType::AttackObject:
        command.type = CommandType::Attack;
        break;
    case ActionType::UseFeat:
        command.type = CommandType::UseFeat;
        command.param = static_cast<int32_t>(action.feat);
        break;
    case ActionType::UseSkill:
        command.type = CommandType::UseSkill;
        command.param = static_cast<int32_t>(action.skill);
        break;
    case ActionType::DisableTrap:
        command.type = CommandType::DisarmTrap;
        break;
    case ActionType::CastSpellAtObject:
        if (action.item) {
            return clientUseItem(action.item, *host);
        }
        if (!action.spell) {
            return true;
        }
        command.type = CommandType::UsePower;
        command.param = static_cast<int32_t>(action.spell->type);
        break;
    default:
        showNotice(NoticeKind::Refusal, "The host does that");
        return true;
    }
    submitCommand(command);
    return true;
}

bool CoopSession::clientInCombat(uint32_t localId) const {
    auto host = _ids.host(localId);
    if (!host) {
        return false;
    }
    auto it = _replicas.find(*host);
    return it != _replicas.end() && (it->second.state.flags & kFlagInCombat) != 0;
}

} // namespace game

} // namespace reone
