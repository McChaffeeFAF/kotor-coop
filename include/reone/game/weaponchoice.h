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
#include <optional>

namespace reone {

namespace game {

class Creature;
class Item;

enum class WeaponKind {
    Melee,
    Ranged
};

struct WeaponChoice {
    std::shared_ptr<Item> item;
    int slot {0};
    float expectedDamage {0.0f};
};

/**
 * Expected damage of one hit with `weapon` in `creature`'s hands: the average
 * of the damage dice plus the ability modifier the weapon uses (DEX for
 * ranged, STR for melee).
 */
float expectedWeaponDamage(const Creature &creature, const Item &weapon);

/**
 * ActionEquipMostDamagingMelee/Ranged: the most damaging weapon of the
 * preferred kind among the creature's equipped weapons and inventory, or of
 * the other kind when it has none. The off hand takes melee weapons only.
 * Ties keep the weapon already in the slot, then the first found.
 */
std::optional<WeaponChoice> chooseMostDamagingWeapon(const Creature &creature, WeaponKind preferred, bool offHand);

} // namespace game

} // namespace reone
