/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <algorithm>

#include "reone/game/coopsession.h"

#include "reone/game/coopcommand.h"
#include "reone/game/game.h"
#include "reone/game/guestbench.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/store.h"
#include "reone/game/party.h"
#include "reone/game/action/opencontainer.h"
#include "reone/system/logutil.h"

using namespace reone::net;

namespace reone {

namespace game {

// Workbenches and lab stations for guests. In either mode a guest uses one through a view the
// host computes from the guest's inventory (Free Mode: its own bag, equipment and crafting
// material; Story Mode: the shared inventory and the party's material); every request is
// checked and carried out here (GuestBench), then the view is sent again.

void CoopSession::hostUseBench(HostClient &client, const std::shared_ptr<Creature> &actor, const std::shared_ptr<Object> &bench) {
    // Walk there as a click does, then open the guest's own bench view; the script is
    // never run, so the host's screen stays as it is.
    actor->clearAllActions();
    auto action = _game.newAction<OpenContainerAction>(bench);
    uint32_t clientId = client.clientId;
    uint32_t actorId = actor->id();
    action->setOnReached([this, clientId, actorId](const std::shared_ptr<Object> &object) {
        auto it = std::find_if(_clients.begin(), _clients.end(), [clientId](const auto &entry) {
            return entry.second.clientId == clientId;
        });
        auto user = _game.getObjectById<Creature>(actorId);
        if (it == _clients.end() || !object || !user || user->isDead()) {
            return;
        }
        hostOpenGuestBench(it->second, object);
    });
    actor->addAction(std::move(action));
}

bool CoopSession::hostOpenGuestBench(HostClient &client, const std::shared_ptr<Object> &bench) {
    auto placeable = std::dynamic_pointer_cast<Placeable>(bench);
    if (!placeable || !placeable->isRuntimeLive()) {
        return false;
    }
    auto kind = coopScriptBench(_game, placeable->getOnUsed());
    if (kind == CoopBench::None) {
        return false;
    }
    client.benchOpen = placeable->id();
    client.benchKind = kind == CoopBench::Lab ? BenchKind::Lab : BenchKind::Workbench;
    client.benchSent.clear();
    info("Co-op: " + client.name + " uses " + placeable->tag() + (kind == CoopBench::Lab ? " (lab)" : " (workbench)"));
    hostSendBench(client, true);
    return true;
}

namespace {

// The host-side context of one guest's bench: who crafts, and whose bag and material.
bool benchContext(Game &game, bool free, uint32_t controlled, int *components, int *chemicals, GuestBench::Context &out) {
    auto actor = controlled ? game.getObjectById<Creature>(controlled) : nullptr;
    if (!actor || actor->isDead()) {
        return false;
    }
    out.crafter = actor;
    if (free) {
        out.own = true;
        out.inventory = actor;
        out.wearers = {actor->id()};
        out.components = components;
        out.chemicals = chemicals;
    }
    return true;
}

} // namespace

void CoopSession::hostSendBench(HostClient &client, bool open) {
    BenchView view;
    view.bench = client.benchOpen;
    view.kind = client.benchKind;
    view.open = open;
    if (!view.bench) {
        return;
    }
    if (open) {
        auto record = _guests.find(client.name);
        bool free = freeMode();
        GuestBench::Context context;
        if (free && record == _guests.end()) {
            view.open = false;
        } else if (!benchContext(_game, free, hostControlledId(client), free ? &record->second.components : nullptr,
                                 free ? &record->second.chemicals : nullptr, context)) {
            view.open = false;
        } else {
            view = GuestBench(_game, _game.services(), client.benchKind, std::move(context)).view(client.benchOpen);
        }
    }
    auto bytes = encode(view);
    if (bytes != client.benchSent) {
        client.benchSent = bytes;
        send(client.connection, bytes);
    }
}

CommandReject CoopSession::hostBenchCommand(HostClient &client, const Command &command, bool &handled) {
    handled = true;
    switch (command.type) {
    case CommandType::BenchUpgrade:
    case CommandType::BenchRemove:
    case CommandType::BenchCraft:
    case CommandType::BenchBreak:
    case CommandType::BenchClose:
        break;
    default:
        handled = false;
        return CommandReject::None;
    }
    if (!client.benchOpen || command.target != client.benchOpen) {
        return CommandReject::BenchClosed;
    }
    if (command.type == CommandType::BenchClose) {
        hostSendBench(client, false);
        client.benchOpen = 0;
        return CommandReject::None;
    }
    bool free = freeMode();
    auto record = _guests.find(client.name);
    GuestBench::Context context;
    if ((free && record == _guests.end()) ||
        !benchContext(_game, free, hostControlledId(client), free ? &record->second.components : nullptr,
                      free ? &record->second.chemicals : nullptr, context)) {
        return CommandReject::BenchClosed;
    }
    auto placeable = _game.getObjectById<Placeable>(client.benchOpen);
    if (!placeable || !placeable->isRuntimeLive()) {
        client.benchOpen = 0;
        return CommandReject::BenchClosed;
    }
    if (command.param > 0 && command.type != CommandType::BenchCraft && isStandInItem(static_cast<uint32_t>(command.param))) {
        // A stand-in's gear is its own and goes with it: never upgraded, stripped or broken down.
        return CommandReject::ItemUnavailable;
    }
    if (!free && (command.type == CommandType::BenchUpgrade || command.type == CommandType::BenchRemove) && command.param > 0) {
        // Story Mode shares the inventory, not what another player wears now.
        std::string wearer;
        if (hostOtherPlayerWears(&client, static_cast<uint32_t>(command.param), wearer)) {
            info("Co-op: " + client.name + " may not change an item " + wearer + " has equipped");
            return CommandReject::ItemEquippedByPlayer;
        }
    }
    GuestBench bench(_game, _game.services(), client.benchKind, std::move(context));
    // A number in a command's text: digits only, so nothing else is read as one.
    auto number = [](const std::string &text, uint32_t &value) {
        if (text.empty() || text.size() > 9 || text.find_first_not_of("0123456789") != std::string::npos) {
            return false;
        }
        value = static_cast<uint32_t>(std::stoul(text));
        return true;
    };
    CommandReject reason = CommandReject::None;
    uint32_t value = 0;
    switch (command.type) {
    case CommandType::BenchUpgrade:
        reason = command.param > 0 && number(command.item, value) && value > 0
                     ? bench.upgrade(static_cast<uint32_t>(command.param), value)
                     : CommandReject::ItemUnavailable;
        break;
    case CommandType::BenchRemove:
        reason = command.param > 0 && number(command.item, value) && value < kMaxBenchSlots
                     ? bench.remove(static_cast<uint32_t>(command.param), static_cast<int>(value))
                     : CommandReject::UpgradeIncompatible;
        break;
    case CommandType::BenchCraft:
        reason = bench.craft(command.item, command.param);
        break;
    case CommandType::BenchBreak:
        reason = command.param > 0 ? bench.breakDown(static_cast<uint32_t>(command.param)) : CommandReject::NotBreakable;
        break;
    default:
        break;
    }
    if (reason != CommandReject::None) {
        return reason;
    }
    info("Co-op: " + client.name + " used the bench: " + commandTypeName(command.type));
    if (free) {
        // The guest's file follows what its character holds now.
        hostSyncGuest(client, true);
        hostSendGuestInventory(client);
    }
    hostSendBench(client, true);
    for (auto &[connection, other] : _clients) {
        // Another guest at the same bench in Story Mode shares the inventory it just changed.
        if (&other != &client && other.benchOpen && !free) {
            hostSendBench(other, true);
        }
    }
    return CommandReject::None;
}

bool CoopSession::hostOtherPlayerWears(const HostClient *client, uint32_t itemId, std::string &wearer) const {
    if (!isHost() || itemId == 0) {
        return false;
    }
    auto wears = [itemId](const Creature &creature) {
        for (const auto &[slot, item] : creature.equipment()) {
            if (item && item->id() == itemId) {
                return true;
            }
        }
        return false;
    };
    uint32_t own = client ? hostControlledId(*client) : 0;
    if (client && !_config.dedicated) {
        // The host plays its party's leader.
        auto leader = _game.party().getLeader();
        if (leader && leader->id() != own && !isPlayerControlled(leader->id()) && wears(*leader)) {
            wearer = _config.playerName.empty() ? std::string("the host") : _config.playerName;
            return true;
        }
    }
    for (const auto &[connection, other] : _clients) {
        if (&other == client || !other.welcomed) {
            continue;
        }
        uint32_t id = hostControlledId(other);
        auto creature = id && id != own ? _game.getObjectById<Creature>(id) : nullptr;
        if (creature && wears(*creature)) {
            wearer = other.name;
            return true;
        }
    }
    return false;
}

bool CoopSession::hostItemWornByGuest(uint32_t itemId, std::string &wearer) const {
    return isHost() && !freeMode() && hostOtherPlayerWears(nullptr, itemId, wearer);
}

void CoopSession::clientHandleBench(const BenchView &view) {
    _benchView = view;
    if (!view.open) {
        _benchView.open = false;
    }
    _benchScreen.setView(_benchView);
    auto store = std::dynamic_pointer_cast<Store>(_benchHolder);
    if (store && store->isRuntimeLive()) {
        // The stand-ins are rebuilt from every view: the host's inventory is the truth.
        std::vector<std::shared_ptr<Item>> old(store->items().begin(), store->items().end());
        for (auto &item : old) {
            store->removeItemStack(item);
            _game.destroyRuntimeObjectGraph(item);
        }
    }
    _benchScreen.clearStandIns();
    if (!_benchView.open) {
        _game.closeGuestBench();
        return;
    }
    if (!store || !store->isRuntimeLive()) {
        store = _game.newStore();
        store->setTag("coop_bench");
        _benchHolder = store;
    }
    // Local stand-ins of the host's items, from this install's templates, so the upgrade
    // screen can draw and stage them as it does its own.
    for (const auto &entry : _benchScreen.standIns()) {
        auto item = _game.newItemFromBlueprint(entry.resref);
        if (!item) {
            continue;
        }
        item->setStackSize(entry.stack);
        item->setUpgrades(entry.upgrades);
        if (entry.modelVariation >= 0) {
            item->setModelVariation(entry.modelVariation);
        }
        store->addItem(item);
        const auto &held = store->items();
        if (std::find(held.begin(), held.end(), item) != held.end()) {
            _benchScreen.mapStandIn(item->id(), entry.host);
        }
    }
    _game.showGuestBench();
}

std::shared_ptr<Object> CoopSession::benchHolder() const {
    return _benchHolder && _benchHolder->isRuntimeLive() ? _benchHolder : nullptr;
}

bool CoopSession::clientBenchRequest(const BenchRequest &request) {
    switch (request.kind) {
    case BenchRequest::Kind::Upgrade:
        return clientBenchUpgrade(request.item, request.part);
    case BenchRequest::Kind::Remove:
        return clientBenchRemove(request.item, request.slot);
    case BenchRequest::Kind::Craft:
        return clientBenchCraft(request.resref, request.material);
    case BenchRequest::Kind::Break:
        return clientBenchBreak(request.item);
    }
    return false;
}

bool CoopSession::clientBenchUpgrade(uint32_t itemId, uint32_t partId) {
    if (!clientBenchOpen()) {
        return false;
    }
    _benchScreen.clearRejection();
    _benchScreen.track(submitRequest(CommandType::BenchUpgrade, _benchView.bench, static_cast<int32_t>(itemId), std::to_string(partId)));
    return true;
}

bool CoopSession::clientBenchRemove(uint32_t itemId, int slotIndex) {
    if (!clientBenchOpen() || slotIndex < 0) {
        return false;
    }
    _benchScreen.clearRejection();
    _benchScreen.track(submitRequest(CommandType::BenchRemove, _benchView.bench, static_cast<int32_t>(itemId), std::to_string(slotIndex)));
    return true;
}

bool CoopSession::clientBenchCraft(const std::string &resref, int material) {
    if (!clientBenchOpen()) {
        return false;
    }
    _benchScreen.clearRejection();
    _benchScreen.track(submitRequest(CommandType::BenchCraft, _benchView.bench, material, resref));
    return true;
}

bool CoopSession::clientBenchBreak(uint32_t itemId) {
    if (!clientBenchOpen()) {
        return false;
    }
    _benchScreen.clearRejection();
    _benchScreen.track(submitRequest(CommandType::BenchBreak, _benchView.bench, static_cast<int32_t>(itemId)));
    return true;
}

bool CoopSession::clientBenchClose() {
    if (!clientBenchOpen()) {
        return false;
    }
    _benchView.open = false;
    submitRequest(CommandType::BenchClose, _benchView.bench, 0);
    _benchScreen.reset();
    return true;
}

} // namespace game

} // namespace reone
