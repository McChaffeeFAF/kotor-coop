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

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "reone/net/protocol.h"

namespace reone {

namespace game {

class Creature;
class Game;
class Object;
struct ServicesView;

/**
 * A workbench or lab station used by a co-op guest, run on the host (Free Mode: the guest's
 * own bag, equipment and crafting material; Story Mode: the shared inventory, the party's
 * equipment and the party's material). The upgrade and crafting rules are the single-player
 * bench's (UpgradeBench, CraftingBench); this only chooses the inventory, checks the guest's
 * request against it and the guest's character, and describes the result for the guest's
 * screen. A rejected command changes nothing.
 */
class GuestBench {
public:
    struct Context {
        std::shared_ptr<Creature> crafter; ///< the guest's character: its skills decide what can be made
        /// Free Mode: the guest's own inventory, its equipped items' owners and its crafting material.
        bool own {false};
        std::shared_ptr<Object> inventory;
        std::vector<uint32_t> wearers;
        int *components {nullptr};
        int *chemicals {nullptr};
    };

    GuestBench(Game &game, ServicesView &services, net::BenchKind kind, Context context) :
        _game(game),
        _services(services),
        _kind(kind),
        _context(std::move(context)) {
    }

    /** What this guest may do here now. */
    net::BenchView view(uint32_t benchId) const;

    /** Install the upgrade part (an inventory item) in the first slot of the item it fits. */
    net::CommandReject upgrade(uint32_t itemId, uint32_t partId);
    /** Empty a slot of an item; the part comes back to the inventory. */
    net::CommandReject remove(uint32_t itemId, int slotIndex);
    /** Make one item of a recipe (material 0 components, 1 chemicals). */
    net::CommandReject craft(const std::string &resref, int material);
    /** Break one of an inventory item down into material. */
    net::CommandReject breakDown(uint32_t itemId);

private:
    Game &_game;
    ServicesView &_services;
    net::BenchKind _kind;
    Context _context;
};

} // namespace game

} // namespace reone
