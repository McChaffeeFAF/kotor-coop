/* Copyright (c) 2026 The reone project contributors */

// Co-op N2 flows (design note coop_net): containers and loot, the area-exit
// ready check, pause, and enemy scaling. The host decides every one of them;
// a client only shows what the host sends and asks.

#include "reone/game/coopsession.h"

#include <algorithm>
#include <cmath>

#include <boost/algorithm/string.hpp>

#include "action/commonactions.h"
#include "reone/game/action/opendoor.h"
#include "reone/game/action/opencontainer.h"
#include "reone/game/coopreplication.h"
#include "reone/game/game.h"
#include "reone/game/gui/hud.h"
#include "reone/game/gui/confirmpopup.h"
#include "reone/game/gui/container.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/script/types.h"
#include "reone/system/logutil.h"

using namespace reone::net;

namespace reone {

namespace game {

namespace {

/** How near a creature must stay to a container it has open. */
constexpr float kContainerReach = 6.0f;

std::vector<ContainerItem> containerItems(const Object &container) {
    std::vector<ContainerItem> result;
    for (const auto &item : container.items()) {
        if (!item || !item->isDropable() || result.size() >= kMaxContainerItems) {
            continue;
        }
        ContainerItem entry;
        entry.id = item->id();
        entry.resref = boost::to_lower_copy(item->blueprintResRef());
        entry.tag = item->tag().substr(0, kMaxStringBytes);
        entry.stack = static_cast<uint16_t>(std::clamp(item->stackSize(), 1, 65535));
        if (entry.resref.empty() || entry.resref.size() > 16) {
            continue; // nothing a client could build from its own install
        }
        result.push_back(std::move(entry));
    }
    return result;
}

bool sameItems(const std::vector<ContainerItem> &a, const std::vector<ContainerItem> &b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].id != b[i].id || a[i].stack != b[i].stack || a[i].resref != b[i].resref) {
            return false;
        }
    }
    return true;
}

bool isContainer(const Object &object) {
    if (auto placeable = dynamic_cast<const Placeable *>(&object)) {
        return placeable->hasInventory();
    }
    if (auto creature = dynamic_cast<const Creature *>(&object)) {
        return creature->isDead();
    }
    return false;
}

} // namespace

int CoopSession::hostPlayerCount() const {
    int players = 1;
    for (const auto &[connection, client] : _clients) {
        if (client.welcomed) {
            ++players;
        }
    }
    return players;
}

bool CoopSession::showsPrompts() const {
    // Headless harness runs answer prompts through the control channel,
    // unless a screenshot run asks to see them (coopprompts 1).
    return !_game._options.graphics.headless || _forcePrompts;
}

void CoopSession::bark(const std::string &text, float duration) {
    // The HUD owns the bubble; there is none before the first module loads.
    // Straight to it: a session notice is this player's own, never a bark
    // for the others.
    if (_game._hud) {
        _game._hud->barkBubble().setBarkText(text, duration);
    }
}

float CoopSession::hostNow() const {
    return static_cast<float>(_game.tickCount()) * _game.tickSeconds();
}

void CoopSession::hostNotice(HostClient &client, NoticeKind kind, const std::string &text) {
    Notice notice;
    notice.kind = kind;
    notice.text = text.substr(0, kMaxStringBytes);
    send(client.connection, encode(notice));
}

// Session commands: requests that are not orders for the creature

CommandReject CoopSession::hostSessionCommand(HostClient &client, const Command &command, bool &handled) {
    handled = true;
    if ((command.type == CommandType::Ready || command.type == CommandType::Pause) && !hostApproved(client)) {
        // A join still waiting for the host (or declined) has no say in the
        // session yet: no pausing everyone, no vote on the exit.
        return CommandReject::NoSlot;
    }
    switch (command.type) {
    case CommandType::Ready:
        if (!_ready.active()) {
            return CommandReject::NoReadyCheck;
        }
        if (command.param != 0 && command.param != 1) {
            return CommandReject::PointInvalid;
        }
        _ready.answer(client.clientId, command.param == 1);
        info("Co-op: " + client.name + " is " + (command.param == 1 ? "ready" : "not ready") + " for " + _ready.destination());
        return CommandReject::None;

    case CommandType::Pause: {
        if (_config.noPause) {
            return CommandReject::PauseDisabled;
        }
        if (command.param != 0 && command.param != 1) {
            return CommandReject::PointInvalid;
        }
        bool pause = command.param == 1;
        if (pause && !_game.isPaused()) {
            if (!_pauseCooldown.mayPause(client.clientId, hostNow())) {
                return CommandReject::PauseCooldown;
            }
            _pauseCooldown.recordPause(client.clientId, hostNow());
            _pausedBy = client.name;
        }
        _game.setPaused(pause);
        _lastPaused = pause;
        info("Co-op: " + client.name + (pause ? " paused" : " resumed") + " the game");
        return CommandReject::None;
    }

    case CommandType::TakeItem: {
        if (!client.viewing || command.target != client.viewing) {
            return CommandReject::ContainerNotOpen;
        }
        auto container = _game.getObjectById(client.viewing);
        auto actor = hostControlledId(client) ? _game.getObjectById<Creature>(hostControlledId(client)) : nullptr;
        // Story Mode: loot goes to the shared inventory. Free Mode: a guest
        // keeps its own (plot items still to come: the host's, per design).
        std::shared_ptr<Object> player = freeMode() && actor && client.guestId == actor->id()
                                             ? std::static_pointer_cast<Object>(actor)
                                             : std::static_pointer_cast<Object>(_game.party().player());
        if (!container || !container->isRuntimeLive() || !player) {
            return CommandReject::TargetInvalid;
        }
        if (!actor || actor->isDead()) {
            return CommandReject::ActorUnavailable;
        }
        if (freeMode() && client.guestId == actor->id()) {
            return hostGuestTake(client, *container, *actor, command.param);
        }
        // The loot goes into the shared party inventory (design item 4). The
        // host owns the contents: whoever's request arrives first gets the
        // item, a later request for it finds it gone.
        std::vector<uint32_t> ids;
        if (command.param == 0) {
            for (const auto &item : containerItems(*container)) {
                ids.push_back(item.id);
            }
        } else {
            ids.push_back(static_cast<uint32_t>(command.param));
        }
        int taken = 0;
        auto creature = std::dynamic_pointer_cast<Creature>(container);
        auto placeable = std::dynamic_pointer_cast<Placeable>(container);
        for (uint32_t id : ids) {
            bool credits = false;
            if (auto item = _game.getObjectById<Item>(id)) {
                credits = item->isCredits();
            }
            if (!container->moveDropableItemTo(id, *player)) {
                continue;
            }
            ++taken;
            if (creature) {
                creature->signalInventoryDisturbed(InventoryDisturbType::Removed, actor->id(), credits ? script::kObjectInvalid : id);
            }
        }
        if (taken == 0) {
            return CommandReject::ItemUnavailable;
        }
        if (placeable) {
            placeable->runOnInvDisturbed(actor->id(), InventoryDisturbType::Removed, script::kObjectInvalid);
        }
        info("Co-op: " + client.name + " took " + std::to_string(taken) + " item(s) from " + container->tag());
        return CommandReject::None;
    }

    case CommandType::CloseContainer: {
        if (!client.viewing || command.target != client.viewing) {
            return CommandReject::ContainerNotOpen;
        }
        auto container = _game.getObjectById(client.viewing);
        if (auto placeable = std::dynamic_pointer_cast<Placeable>(container)) {
            if (uint32_t actor = hostControlledId(client)) {
                placeable->signalClosed(actor);
            }
        }
        client.viewing = 0;
        client.viewSent.clear();
        return CommandReject::None;
    }

    case CommandType::PickSlot: {
        // Pick again (design, "Lost companion"): a free companion by tag.
        auto &party = _game.party();
        for (const auto &member : party.members()) {
            if (member.npc == kNpcPlayer || !member.creature || !boost::iequals(member.creature->tag(), command.item)) {
                continue;
            }
            if (isPlayerControlled(member.creature->id()) || isReservedForOther(member.creature->tag(), client)) {
                return CommandReject::TargetInvalid;
            }
            client.preferredTag = member.creature->tag();
            client.slot = SlotAssign();
            client.slot.status = SlotStatus::Released;
            client.slotRequested = true;
            hostUpdateSlots();
            return CommandReject::None;
        }
        return CommandReject::TargetInvalid;
    }

    default:
        handled = false;
        return CommandReject::None;
    }
}

void CoopSession::hostCarryOutUse(HostClient &client, const Command &command, const CoopCommandTargets &targets) {
    auto &actor = targets.actor;
    auto &target = targets.target;
    if (!actor || !target) {
        return;
    }
    switch (targets.use) {
    case CoopUse::Container: {
        // Walk there as a click does; on arrival this client opens its own
        // container screen. The host's screen stays as it is.
        actor->clearAllActions();
        auto action = _game.newAction<OpenContainerAction>(target);
        uint32_t clientId = client.clientId;
        uint32_t actorId = actor->id();
        action->setOnReached([this, clientId, actorId](const std::shared_ptr<Object> &container) {
            hostOpenContainer(clientId, container, actorId);
        });
        actor->addAction(std::move(action));
        break;
    }
    case CoopUse::Placeable:
        hostUsePlaceable(client, actor, target);
        break;
    case CoopUse::Bench:
        hostUseBench(client, actor, target);
        break;
    case CoopUse::Transition: {
        auto door = std::dynamic_pointer_cast<Door>(target);
        if (!door) {
            break;
        }
        if (auto module = _game.module(); module && module->area() && module->area()->refuseTransitionWhilePartyIsDown(*actor)) {
            break;
        }
        // The host's own click path: walk to the exit, then use it. On
        // arrival a shut, locked door runs its fail-to-open; otherwise this
        // client's ready check starts.
        uint32_t clientId = client.clientId;
        auto arrive = [this, clientId](const std::shared_ptr<Door> &door, Creature &creature) {
            if (!door->isRuntimeLive() || !door->isAreaExit()) {
                return;
            }
            if (auto module = _game.module(); module && module->area() && module->area()->refuseTransitionWhilePartyIsDown(creature)) {
                return;
            }
            bool shut = !door->isOpen() || door->isOpening();
            if (shut && door->isLocked()) {
                tryUnlockDoorWithKey(_game, *door, creature, _game.party());
            }
            if (shut && door->isLocked()) {
                door->onFailToOpen(creature);
                return;
            }
            PendingExit exit;
            exit.module = door->linkedToModule();
            exit.entry = door->linkedTo();
            hostStartReadyCheck(clientId, exit);
        };
        if (_game.instantDoorTransitions()) {
            arrive(door, *actor);
            break;
        }
        actor->clearAllActions();
        auto action = _game.newAction<OpenDoorAction>(door);
        action->setUserAction(true);
        action->setUseTransition(true);
        action->setOnArrival(std::move(arrive));
        actor->addAction(std::move(action));
        break;
    }
    default:
        break;
    }
}

// Containers (design items 14-15)

void CoopSession::hostOpenContainer(uint32_t clientId, const std::shared_ptr<Object> &container, uint32_t actor) {
    auto it = std::find_if(_clients.begin(), _clients.end(), [clientId](const auto &entry) {
        return entry.second.clientId == clientId;
    });
    if (it == _clients.end() || !container || !container->isRuntimeLive()) {
        return;
    }
    auto &client = it->second;
    if (auto placeable = std::dynamic_pointer_cast<Placeable>(container)) {
        // What the container screen does on the host: OnOpen generates loot.
        placeable->openContainer(actor);
    }
    client.viewing = container->id();
    client.viewSent.clear();
    hostSendContainer(client, true);
    info("Co-op: client " + std::to_string(clientId) + " opened " + container->tag());
}

void CoopSession::hostSendContainer(HostClient &client, bool open) {
    ContainerView view;
    view.container = client.viewing;
    view.open = open;
    if (open) {
        if (auto container = _game.getObjectById(client.viewing)) {
            view.items = hostContainerView(client, *container);
        }
        client.viewSent = view.items;
    }
    if (view.container) {
        send(client.connection, encode(view));
    }
    if (!open) {
        client.viewing = 0;
        client.viewSent.clear();
    }
}

void CoopSession::hostUpdateContainers() {
    for (auto &[connection, client] : _clients) {
        if (!client.viewing) {
            continue;
        }
        auto container = _game.getObjectById(client.viewing);
        auto actor = hostControlledId(client) ? _game.getObjectById<Creature>(hostControlledId(client)) : nullptr;
        auto module = _game.module();
        bool gone = !container || !container->isRuntimeLive() || !isContainer(*container) || !actor || actor->isDead() ||
                    !module || !module->area() || glm::distance(glm::vec2(actor->position()), glm::vec2(container->position())) > kContainerReach;
        if (gone) {
            // The body decayed, the creature was moved off by a scene, or the
            // companion is no longer this client's: close its screen.
            hostSendContainer(client, false);
            continue;
        }
        // Live: an item another player took disappears from every list.
        if (!sameItems(hostContainerView(client, *container), client.viewSent)) {
            hostSendContainer(client, true);
        }
    }
}

// Free Mode loot: every guest loots its own copy of a container (design,
// "Containers and bodies are instanced per player"). Plot items are real and
// shared, and always go to the host (story scripts check the PC's
// inventory). The host's own looting takes the real contents.

namespace {

constexpr uint32_t kVirtualLoot = 0x40000000;

} // namespace

void CoopSession::onContainerOpened(const Object &container) {
    if (isHost() && freeMode()) {
        hostCaptureLoot(container);
    }
}

std::string CoopSession::lootKey(const Object &container) const {
    auto module = _game.module();
    if (!module || !module->area()) {
        return CoopLootLedger::key(container.tag(), container.position());
    }
    return coopLootKey(*module->area(), container);
}

std::string CoopSession::lootModule() const {
    auto module = _game.module();
    return module ? boost::to_lower_copy(module->name()) : std::string();
}

void CoopSession::hostCaptureLoot(const Object &container) {
    if (_lootBaseline.count(container.id())) {
        return;
    }
    auto &entries = _lootBaseline[container.id()];
    auto module = lootModule();
    auto key = lootKey(container);
    if (auto saved = _lootLedger.find(module, key)) {
        // A revisit: the copy taken on the first opening, with the shared
        // plot items pointed at this visit's objects.
        std::set<uint32_t> used;
        for (const auto &stored : saved->baseline) {
            LootEntry entry;
            entry.resref = stored.resref;
            entry.tag = stored.tag;
            entry.stack = stored.stack;
            entry.plot = stored.plot;
            entry.credits = stored.credits;
            if (entry.plot) {
                for (const auto &item : container.items()) {
                    if (item && !used.count(item->id()) && boost::to_lower_copy(item->blueprintResRef()) == entry.resref) {
                        entry.item = item->id();
                        used.insert(entry.item);
                        break;
                    }
                }
            }
            entries.push_back(std::move(entry));
        }
        return;
    }
    for (const auto &item : container.items()) {
        if (!item || !item->isDropable()) {
            continue;
        }
        LootEntry entry;
        entry.item = item->id();
        entry.resref = boost::to_lower_copy(item->blueprintResRef());
        entry.tag = item->tag().substr(0, kMaxStringBytes);
        entry.stack = static_cast<uint16_t>(std::clamp(item->stackSize(), 1, 65535));
        entry.plot = item->plotFlag();
        entry.credits = item->isCredits();
        entries.push_back(std::move(entry));
    }
    std::vector<CoopLootLedger::Entry> lite;
    for (const auto &entry : entries) {
        lite.push_back({entry.resref, entry.tag, entry.stack, entry.plot, entry.credits});
    }
    _lootLedger.capture(module, key, lite);
}

std::vector<ContainerItem> CoopSession::hostContainerView(HostClient &client, const Object &container) {
    if (!freeMode() || !client.guestId) {
        return containerItems(container);
    }
    hostCaptureLoot(container);
    std::vector<ContainerItem> result;
    const auto &entries = _lootBaseline[container.id()];
    const auto module = lootModule();
    const auto key = lootKey(container);
    for (size_t i = 0; i < entries.size() && result.size() < kMaxContainerItems; ++i) {
        const auto &entry = entries[i];
        if (entry.resref.empty() || entry.resref.size() > 16 || _lootLedger.isTaken(module, key, client.name, i)) {
            continue;
        }
        if (entry.plot) {
            // Shared: listed while the real item is still there.
            bool there = std::any_of(container.items().begin(), container.items().end(), [&entry](const auto &item) {
                return item && item->id() == entry.item;
            });
            if (there) {
                result.push_back({entry.item, entry.resref, entry.tag, entry.stack});
            }
            continue;
        }
        result.push_back({kVirtualLoot + static_cast<uint32_t>(i), entry.resref, entry.tag, entry.stack});
    }
    return result;
}

CommandReject CoopSession::hostGuestTake(HostClient &client, Object &container, Creature &actor, int32_t param) {
    std::vector<uint32_t> ids;
    if (param == 0) {
        for (const auto &item : hostContainerView(client, container)) {
            ids.push_back(item.id);
        }
    } else {
        ids.push_back(static_cast<uint32_t>(param));
    }
    auto &entries = _lootBaseline[container.id()];
    const auto module = lootModule();
    const auto key = lootKey(container);
    auto taken = [&](size_t index) { return _lootLedger.isTaken(module, key, client.name, index); };
    auto record = _guests.find(client.name);
    auto host = _game.party().player();
    int count = 0;
    for (uint32_t id : ids) {
        if (id >= kVirtualLoot) {
            size_t index = id - kVirtualLoot;
            if (index >= entries.size() || taken(index) || entries[index].plot) {
                continue;
            }
            const auto &entry = entries[index];
            if (entry.credits) {
                if (record != _guests.end()) {
                    record->second.credits = std::min<int32_t>(record->second.credits + entry.stack, 10000000);
                }
            } else if (auto item = _game.newItemFromBlueprint(entry.resref)) {
                item->setStackSize(entry.stack);
                actor.addItem(item);
            } else {
                continue;
            }
            _lootLedger.markTaken(module, key, client.name, index);
            ++count;
            continue;
        }
        // A plot item: the real one, to the host (design, "Plot items always go to the host").
        auto live = std::find_if(entries.begin(), entries.end(), [id](const LootEntry &entry) { return entry.item == id && entry.plot; });
        if (live == entries.end() || !host || !container.moveDropableItemTo(id, *host)) {
            continue;
        }
        _lootLedger.markTaken(module, key, client.name, static_cast<size_t>(live - entries.begin()));
        ++count;
        if (auto creature = dynamic_cast<Creature *>(&container)) {
            creature->signalInventoryDisturbed(InventoryDisturbType::Removed, actor.id(), id);
        }
        if (auto placeable = dynamic_cast<Placeable *>(&container)) {
            placeable->runOnInvDisturbed(actor.id(), InventoryDisturbType::Removed, script::kObjectInvalid);
        }
        info("Co-op: " + client.name + " found a plot item in " + container.tag() + "; it goes to the host");
    }
    if (count == 0) {
        return CommandReject::ItemUnavailable;
    }
    info("Co-op: " + client.name + " took " + std::to_string(count) + " item(s) from their copy of " + container.tag());
    return CommandReject::None;
}

// Ready check (design item 8)

std::vector<CoopReadyCheck::Player> CoopSession::hostReadyPlayers() const {
    std::vector<CoopReadyCheck::Player> players;
    CoopReadyCheck::Player host;
    host.key = 0;
    host.name = _config.playerName;
    host.busy = _game.currentScreen() != Game::Screen::InGame;
    players.push_back(host);
    for (const auto &[connection, client] : _clients) {
        if (client.welcomed) {
            CoopReadyCheck::Player player;
            player.key = client.clientId;
            player.name = client.name;
            players.push_back(player);
        }
    }
    return players;
}

bool CoopSession::interceptTransition(const std::string &module, const std::string &entry, uint32_t clickTrigger, uint32_t triggerer) {
    if (!isHost() || _config.hostOnlyTransitions || module.empty()) {
        return false;
    }
    bool anyClient = std::any_of(_clients.begin(), _clients.end(), [](const auto &entry) {
        return entry.second.welcomed;
    });
    if (!anyClient) {
        return false;
    }
    if (_ready.active()) {
        // Using an exit again answers the running check.
        _ready.answer(0, true);
        return true;
    }
    PendingExit exit;
    exit.module = module;
    exit.entry = entry;
    exit.clickTrigger = clickTrigger;
    exit.triggerer = triggerer;
    hostStartReadyCheck(0, exit);
    return true;
}

void CoopSession::hostStartReadyCheck(uint32_t initiator, const PendingExit &exit) {
    if (_ready.active()) {
        _ready.answer(initiator, true);
        return;
    }
    _pendingExit = exit;
    _ready.start(_nextReadyId++, initiator, exit.module, hostNow(), hostReadyPlayers());
    _hostReadyPrompted = false;
    info("Co-op: ready check " + std::to_string(_ready.id()) + ": " + _ready.initiatorName() + " wants to leave for " + exit.module);
    for (auto &[connection, client] : _clients) {
        if (client.welcomed) {
            hostSendReady(client, true);
        }
    }
}

void CoopSession::hostSendReady(HostClient &client, bool force) {
    auto message = _ready.message(hostNow());
    if (!force && client.readySent == message.id && client.readyStateSent == message.state &&
        client.readySecondsSent == message.secondsLeft) {
        return;
    }
    client.readySent = message.id;
    client.readyStateSent = message.state;
    client.readySecondsSent = message.secondsLeft;
    send(client.connection, encode(message));
}

void CoopSession::hostPromptReady() {
    // The host's own prompt, when a client started the check. A host in a
    // menu or conversation gets it on returning to the game.
    if (_hostReadyPrompted || !_ready.active()) {
        return;
    }
    auto self = std::find_if(_ready.players().begin(), _ready.players().end(), [](const auto &player) {
        return player.key == 0;
    });
    if (self == _ready.players().end() || self->answer != ReadyAnswer::Pending) {
        return;
    }
    if (_game.currentScreen() != Game::Screen::InGame || !showsPrompts()) {
        return;
    }
    if (!_game._confirmPopup) {
        _game._confirmPopup = _game.tryLoadGUI<ConfirmPopup>();
    }
    if (!_game._confirmPopup || _game._confirmPopup->isVisible()) {
        return;
    }
    _hostReadyPrompted = true;
    uint32_t id = _ready.id();
    _game._confirmPopup->showChoices(
        _ready.initiatorName() + " wants to leave for " + _ready.destination() + ". Ready?",
        {{"Ready", [this, id]() { if (_ready.active() && _ready.id() == id) { _ready.answer(0, true); } }},
         {"Not ready", [this, id]() { if (_ready.active() && _ready.id() == id) { _ready.answer(0, false); } }},
         {"Go now", [this, id]() { if (_ready.active() && _ready.id() == id) { _ready.force(); } }}});
}

void CoopSession::hostUpdateReadyCheck() {
    if (!_ready.active() && _readyOutcomeSent == _ready.id()) {
        return;
    }
    if (_ready.active()) {
        _ready.setBusy(0, _game.currentScreen() != Game::Screen::InGame);
        hostPromptReady();
        auto outcome = _ready.evaluate(hostNow());
        if (outcome == CoopReadyCheck::Outcome::Pending) {
            for (auto &[connection, client] : _clients) {
                if (client.welcomed) {
                    hostSendReady(client, false);
                }
            }
            return;
        }
    }
    _readyOutcomeSent = _ready.id();
    bool passed = _ready.message(hostNow()).state == ReadyState::Passed;
    for (auto &[connection, client] : _clients) {
        if (client.welcomed) {
            hostSendReady(client, true);
        }
    }
    if (_game._confirmPopup && _game._confirmPopup->isVisible() && _hostReadyPrompted) {
        _game._confirmPopup->hide();
    }
    if (!passed) {
        info("Co-op: ready check " + std::to_string(_ready.id()) + " cancelled: not enough players ready");
        bark("Not everyone is ready to leave", 4.0f);
        return;
    }
    info("Co-op: ready check " + std::to_string(_ready.id()) + " passed; the party moves to " + _pendingExit.module);
    auto exit = _pendingExit;
    _pendingExit = PendingExit();
    if (exit.clickTrigger) {
        if (auto trigger = _game.getObjectById<Trigger>(exit.clickTrigger)) {
            trigger->runClickScript(exit.triggerer);
            return;
        }
    }
    _game.scheduleModuleTransition(exit.module, exit.entry);
}

void CoopSession::hostCheckClientExits() {
    // A client's creature walking into a transition trigger uses the exit,
    // as the leader does on the host (Area::checkTriggersIntersection).
    auto module = _game.module();
    if (!module || !module->area()) {
        return;
    }
    auto &area = *module->area();
    for (auto &[connection, client] : _clients) {
        if (!client.welcomed || !hostControlledId(client)) {
            client.insideExit = 0;
            continue;
        }
        auto actor = _game.getObjectById<Creature>(hostControlledId(client));
        if (!actor || actor->isDead()) {
            client.insideExit = 0;
            continue;
        }
        glm::vec2 position(actor->position());
        uint32_t inside = 0;
        std::shared_ptr<Trigger> exitTrigger;
        for (auto &object : area.getObjectsByType(ObjectType::Trigger)) {
            auto trigger = std::static_pointer_cast<Trigger>(object);
            if (trigger->linkedToModule().empty() || !trigger->isActive() || !trigger->isIn(position)) {
                continue;
            }
            inside = trigger->id();
            exitTrigger = trigger;
            break;
        }
        if (inside && inside != client.insideExit && exitTrigger) {
            if (_config.hostOnlyTransitions) {
                hostNotice(client, NoticeKind::Refusal, "Only the host can use exits in this session");
            } else if (area.refuseTransitionWhilePartyIsDown(*actor)) {
                // As on the host: nobody leaves while a party member is down.
            } else {
                PendingExit exit;
                exit.module = exitTrigger->linkedToModule();
                exit.entry = exitTrigger->linkedTo();
                exit.clickTrigger = exitTrigger->hasClickScript() ? exitTrigger->id() : 0;
                exit.triggerer = actor->id();
                hostStartReadyCheck(client.clientId, exit);
            }
        }
        client.insideExit = inside;
    }
}

bool CoopSession::answerReady(bool ready) {
    if (isHost()) {
        if (!_ready.active()) {
            return false;
        }
        return _ready.answer(0, ready);
    }
    if (_clientReady.state != ReadyState::Active) {
        return false;
    }
    _clientReadyAnswered = true;
    return submitRequest(CommandType::Ready, 0, ready ? 1 : 0) != 0;
}

bool CoopSession::forceTransition() {
    if (!isHost() || !_ready.active()) {
        return false;
    }
    _ready.force();
    return true;
}

// Pause (design, "Pause")

bool CoopSession::handlePauseKey() {
    if (isClient()) {
        submitRequest(CommandType::Pause, 0, _world.paused ? 0 : 1);
        return true;
    }
    if (!isHost() || !std::any_of(_clients.begin(), _clients.end(), [](const auto &entry) { return entry.second.welcomed; })) {
        return false; // alone: the single-player pause
    }
    if (_config.noPause) {
        bark("Pause is off in this session", 3.0f);
        return true;
    }
    bool pause = !_game.isPaused();
    if (pause) {
        _pausedBy = _config.playerName;
    }
    _game.setPaused(pause);
    _lastPaused = pause;
    return true;
}

void CoopSession::hostUpdatePause() {
    bool paused = _game.isPaused();
    if (paused != _lastPaused) {
        // Paused or resumed by a script (SetPause) or the single-player key.
        if (paused) {
            _pausedBy = _config.playerName;
        }
        _lastPaused = paused;
    }
    if (paused != _shownPause || (paused && _pausedBy != _shownPausedBy)) {
        _shownPause = paused;
        _shownPausedBy = _pausedBy;
        bark(paused ? "Paused by " + _pausedBy : std::string(), paused ? 1.0e6f : 0.0f);
    }
}

// Enemy scaling (design, "Enemy scaling per extra player")

void CoopSession::hostUpdateScaling() {
    auto module = _game.module();
    auto player = _game.party().player();
    if (!module || !module->area() || !player) {
        return;
    }
    int players = hostPlayerCount();
    if (players != _scalePlayers) {
        _scalePlayers = players;
        _scale = coopEnemyScale(players, _config.enemyScaling);
        info("Co-op: " + std::to_string(players) + " players: enemies at x" + std::to_string(_scale.hp) + " hit points, x" +
             std::to_string(_scale.damage) + " damage");
    }
    auto &reputes = _game.services().game.reputes;
    for (auto &object : module->area()->getObjectsByType(ObjectType::Creature)) {
        auto creature = std::static_pointer_cast<Creature>(object);
        if (creature->isDead() || !creature->isRuntimeLive() || _game.party().isMember(*creature) || isStandIn(creature->id())) {
            continue;
        }
        auto it = _scaled.find(creature->id());
        float want = _scale.hp;
        if (it == _scaled.end()) {
            if (want == 1.0f || !reputes.getIsEnemy(*creature, *player)) {
                continue;
            }
            it = _scaled.emplace(creature->id(), Scaled {creature->hitPoints(), 1.0f}).first;
        }
        auto &scaled = it->second;
        if (scaled.factor == want) {
            continue;
        }
        // A new base pool: the maximum moves by the difference and the current
        // hit points with it, so a wounded enemy keeps its wounds. A player
        // leaving never kills an enemy outright.
        int oldBase = creature->hitPoints();
        int newBase = std::max(1, static_cast<int>(std::lround(scaled.base * want)));
        int current = creature->currentHitPoints();
        if (current + (newBase - oldBase) < 1) {
            creature->setCurrentHitPoints(1 - (newBase - oldBase));
        }
        creature->setMaxHitPoints(newBase);
        scaled.factor = want;
    }
}

int CoopSession::scaleEnemyDamage(const Object *attacker, const Object &target, int amount) const {
    if (!isHost() || amount <= 0 || !attacker || _scale.damage == 1.0f ||
        !(_game.party().isMember(target) || isStandIn(target.id()))) {
        return amount;
    }
    if (!_scaled.count(attacker->id())) {
        return amount;
    }
    return std::max(1, static_cast<int>(std::lround(amount * _scale.damage)));
}

// Client

uint32_t CoopSession::submitRequest(CommandType type, uint32_t target, int32_t param, const std::string &item) {
    Command command;
    command.type = type;
    command.target = target;
    command.param = param;
    command.item = item.substr(0, 32);
    command.actor = controlledHostId();
    return submitCommand(command);
}

uint32_t CoopSession::hostItemId(uint32_t localItemId) const {
    auto it = _clientItems.find(localItemId);
    return it != _clientItems.end() ? it->second : 0;
}

void CoopSession::clientHandleContainer(const ContainerView &view) {
    auto local = localObjectForHost(view.container);
    if (!view.open) {
        if (_clientViewing == view.container) {
            _clientViewing = 0;
            clientCloseContainerScreen(true);
        }
        return;
    }
    if (!local) {
        warn("Co-op: the host opened container " + std::to_string(view.container) + ", which this client has no copy of");
        submitRequest(CommandType::CloseContainer, view.container, 0);
        return;
    }
    // The host owns the contents: the local copy holds exactly its list,
    // built from the client's own install.
    std::vector<std::shared_ptr<Item>> old(local->items().begin(), local->items().end());
    for (auto &item : old) {
        _clientItems.erase(item->id());
        if (local->removeItemStack(item)) {
            _game.destroyRuntimeObjectGraph(item);
        }
    }
    for (const auto &entry : view.items) {
        std::shared_ptr<Item> item;
        try {
            item = local->addItem(entry.resref, entry.stack, true);
        } catch (const std::exception &e) {
            warn("Co-op: cannot build item " + entry.resref + ": " + e.what());
        }
        if (item) {
            _clientItems[item->id()] = entry.id;
        }
    }
    bool wasViewing = _clientViewing == view.container;
    _clientViewing = view.container;
    if (!wasViewing && _game._screen != Game::Screen::Conversation && !_game._movie && _game._container) {
        _game.openContainer(local);
    }
}

void CoopSession::clientCloseContainerScreen(bool silent) {
    if (!silent && _clientViewing) {
        submitRequest(CommandType::CloseContainer, _clientViewing, 0);
    }
    _clientViewing = 0;
    if (_game._screen == Game::Screen::Container) {
        _game.openInGame();
    }
}

bool CoopSession::clientContainerTakeAll() {
    if (!isClient()) {
        return false;
    }
    if (_clientViewing) {
        // First request wins on the host; the list updates from its answer.
        submitRequest(CommandType::TakeItem, _clientViewing, 0);
    }
    clientCloseContainerScreen(false);
    return true;
}

bool CoopSession::clientContainerClose() {
    if (!isClient()) {
        return false;
    }
    clientCloseContainerScreen(false);
    return true;
}

void CoopSession::clientHandleReady(const ReadyCheck &check) {
    bool fresh = check.id != _clientReady.id;
    _clientReady = check;
    if (fresh) {
        _clientReadyAnswered = false;
    }
    switch (check.state) {
    case ReadyState::Active:
        if (fresh) {
            showNotice(NoticeKind::Info, check.initiator + " wants to leave for " + check.destination);
        }
        clientPromptReady();
        break;
    case ReadyState::Passed:
        if (_game._confirmPopup && _game._confirmPopup->isVisible()) {
            _game._confirmPopup->hide();
        }
        showNotice(NoticeKind::Info, "The party moves to " + check.destination);
        break;
    case ReadyState::Cancelled:
        if (_game._confirmPopup && _game._confirmPopup->isVisible()) {
            _game._confirmPopup->hide();
        }
        showNotice(NoticeKind::Info, "Not everyone is ready to leave");
        break;
    default:
        break;
    }
}

void CoopSession::clientPromptReady() {
    // A player in a menu or a conversation gets the prompt on returning.
    if (_clientReady.state != ReadyState::Active || _clientReadyAnswered || _clientReadyPrompted == _clientReady.id) {
        return;
    }
    if (_game.currentScreen() != Game::Screen::InGame || !showsPrompts()) {
        return;
    }
    if (!_game._confirmPopup) {
        _game._confirmPopup = _game.tryLoadGUI<ConfirmPopup>();
    }
    if (!_game._confirmPopup || _game._confirmPopup->isVisible()) {
        return;
    }
    _clientReadyPrompted = _clientReady.id;
    uint32_t id = _clientReady.id;
    _game._confirmPopup->showConfirm(
        _clientReady.initiator + " wants to leave for " + _clientReady.destination + ". Ready?",
        [this, id]() { if (_clientReady.id == id) { answerReady(true); } },
        [this, id]() { if (_clientReady.id == id) { answerReady(false); } });
}

void CoopSession::clientHandleNotice(const Notice &notice) {
    if (notice.kind == NoticeKind::TerminalRead || notice.kind == NoticeKind::TerminalControl) {
        clientHandleTerminalNotice(notice);
        return;
    }
    showNotice(notice.kind, notice.text);
}

void CoopSession::clientShowRejection(const CommandResult &result) {
    if (_benchScreen.tracks(result.seq)) {
        // A bench request: the reason goes on the bench screen and into the feedback log.
        _benchScreen.reject(result.seq, result.reason);
        auto text = GuestBenchScreen::rejectionText(result.reason);
        _game.messageLog().add(MessageLog::kFeedbackMessageType, MessageLog::Style::Normal, text);
        showNotice(NoticeKind::Refusal, text);
        if (_game.currentScreen() == Game::Screen::Upgrade) {
            _game.showGuestBench();
        }
        return;
    }
    switch (result.reason) {
    case CommandReject::HostLeadsConversation:
        showNotice(NoticeKind::Refusal, "The host leads conversations");
        break;
    case CommandReject::StoreUnavailable:
        showNotice(NoticeKind::Refusal,
                   freeMode() ? "Guests cannot trade here yet: ask the host to open the store"
                              : "Only the host may trade here");
        break;
    case CommandReject::PauseDisabled:
        showNotice(NoticeKind::Refusal, "Pause is off in this session");
        break;
    case CommandReject::PauseCooldown:
        showNotice(NoticeKind::Refusal, "Wait a moment before pausing again");
        break;
    case CommandReject::HostOnlyTransitions:
        showNotice(NoticeKind::Refusal, "Only the host can use exits in this session");
        break;
    case CommandReject::ItemUnavailable:
        showNotice(NoticeKind::Refusal, "Someone else took it first");
        break;
    case CommandReject::NotHandedOver:
        showNotice(NoticeKind::Refusal, "The host operates this: ask the host to hand it over");
        break;
    case CommandReject::EquipRejected:
        showNotice(NoticeKind::Refusal, "That does not go there");
        break;
    case CommandReject::LevelUpUnavailable:
        showNotice(NoticeKind::Refusal, "No level-up now: a companion never out-levels the host");
        break;
    case CommandReject::LevelUpInvalid:
        showNotice(NoticeKind::Refusal, "The host could not apply that level-up");
        break;
    case CommandReject::ClassLocked:
        showNotice(NoticeKind::Refusal, "That class is not unlocked yet");
        break;
    case CommandReject::ClassAlignment:
        showNotice(NoticeKind::Refusal, "That class does not fit your alignment");
        break;
    case CommandReject::ClassSlotsFull:
        showNotice(NoticeKind::Refusal, "No class slot is free for that class");
        break;
    default:
        break;
    }
}

} // namespace game

} // namespace reone
