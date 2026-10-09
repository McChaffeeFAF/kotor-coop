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

#include <map>
#include <memory>

#include "../types.h"

namespace reone {

namespace game {

class Item;
class Spell;
struct GameServices;

class ItemAttributes {
public:
    void addItem(std::shared_ptr<Item> item, GameServices &game);
    void removeItem(std::shared_ptr<Item> item);

    // Ordered by item object id, not pointer: the order must be the same on
    // every run and on every machine (action bar slots, harness, co-op).
    struct ItemById {
        bool operator()(const std::shared_ptr<Item> &a, const std::shared_ptr<Item> &b) const;
    };
    using ItemSpells = std::map<std::shared_ptr<Item>, std::shared_ptr<Spell>, ItemById>;

    const ItemSpells &healingSpells() const { return _healingSpells; }
    const ItemSpells &defensiveSpells() const { return _defensiveSpells; }
    const ItemSpells &attackingSpells() const { return _attackingSpells; }

    enum class Kind {
        Healing,
        Defensive,
        Attacking
    };
    const ItemSpells &spells(Kind kind) const;

    /** own and, when given, shared in one list (still ordered by item id). */
    static ItemSpells merge(const ItemSpells &own, const ItemSpells *shared);

private:
    ItemSpells _healingSpells;
    ItemSpells _defensiveSpells;
    ItemSpells _attackingSpells;
};

} // namespace game

} // namespace reone
