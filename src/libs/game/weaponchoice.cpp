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

#include "reone/game/weaponchoice.h"

#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"

namespace reone {

namespace game {

float expectedWeaponDamage(const Creature &creature, const Item &weapon) {
    float dice = weapon.numDice() * (weapon.dieToRoll() + 1) * 0.5f;
    Ability ability = weapon.isRanged() ? Ability::Dexterity : Ability::Strength;
    return dice + creature.getEffectiveAbilityModifier(ability);
}

static bool isKind(const Item &item, WeaponKind kind) {
    switch (kind) {
    case WeaponKind::Ranged:
        return item.weaponType() == WeaponType::Ranged;
    case WeaponKind::Melee:
        return item.weaponType() == WeaponType::Melee;
    }
    return false;
}

static std::optional<WeaponChoice> bestOfKind(const Creature &creature, WeaponKind kind, int slot) {
    std::shared_ptr<Item> current = creature.getEquippedItem(slot);
    std::optional<WeaponChoice> best;
    auto consider = [&](const std::shared_ptr<Item> &item) {
        if (!item || !isKind(*item, kind) || !item->isEquippable(slot)) {
            return;
        }
        float damage = expectedWeaponDamage(creature, *item);
        bool better = !best ||
                      damage > best->expectedDamage ||
                      (damage == best->expectedDamage && item == current && best->item != current);
        if (better) {
            best = WeaponChoice {item, slot, damage};
        }
    };
    // Equipped weapons first, so a tie keeps what the creature holds.
    for (const auto &entry : creature.equipment()) {
        consider(entry.second);
    }
    for (const auto &item : creature.items()) {
        consider(item);
    }
    return best;
}

std::optional<WeaponChoice> chooseMostDamagingWeapon(const Creature &creature, WeaponKind preferred, bool offHand) {
    if (offHand) {
        return bestOfKind(creature, WeaponKind::Melee, InventorySlots::leftWeapon);
    }
    auto choice = bestOfKind(creature, preferred, InventorySlots::rightWeapon);
    if (!choice) {
        WeaponKind other = preferred == WeaponKind::Melee ? WeaponKind::Ranged : WeaponKind::Melee;
        choice = bestOfKind(creature, other, InventorySlots::rightWeapon);
    }
    return choice;
}

} // namespace game

} // namespace reone
