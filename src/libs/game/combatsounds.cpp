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

#include "reone/game/combatsounds.h"

#include <boost/algorithm/string.hpp>

#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"

#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/game/types.h"

using namespace reone::resource;

namespace reone {

namespace game {

namespace combatsounds {

static int appearanceSoundSet(ITwoDAs &twoDas, const Creature &creature) {
    auto appearance = twoDas.get("appearance");
    if (!appearance || creature.appearance() < 0 || creature.appearance() >= appearance->getRowCount()) {
        return -1;
    }
    return appearance->getInt(creature.appearance(), "soundapptype", -1);
}

int weaponSoundRow(ITwoDAs &twoDas, const Creature &attacker) {
    auto weapon = attacker.getEquippedItem(InventorySlots::rightWeapon);
    if (!weapon) {
        weapon = attacker.getEquippedItem(InventorySlots::leftWeapon);
    }
    if (weapon) {
        auto baseItems = twoDas.get("baseitems");
        if (baseItems && weapon->baseItemType() >= 0 && weapon->baseItemType() < baseItems->getRowCount()) {
            if (auto row = baseItems->getIntOpt(weapon->baseItemType(), "weaponmattype")) {
                return *row;
            }
        }
        return 0;
    }
    // Unarmed: the appearance's sound set names the natural weapon row.
    int soundSet = appearanceSoundSet(twoDas, attacker);
    auto sets = twoDas.get("appearancesndset");
    if (sets && soundSet >= 0 && soundSet < sets->getRowCount()) {
        if (auto row = sets->getIntOpt(soundSet, "weapon")) {
            return *row;
        }
    }
    return 0;
}

std::string targetMaterial(ITwoDAs &twoDas, const Object &target) {
    if (auto creature = dyn_cast<Creature>(&target)) {
        auto armour = creature->getEquippedItem(InventorySlots::body);
        auto baseItems = twoDas.get("baseitems");
        if (armour && baseItems && armour->baseItemType() >= 0 && armour->baseItemType() < baseItems->getRowCount()) {
            auto type = boost::to_lower_copy(baseItems->getString(armour->baseItemType(), "armortype"));
            if (!type.empty()) {
                return type;
            }
        }
        int soundSet = appearanceSoundSet(twoDas, *creature);
        auto sets = twoDas.get("appearancesndset");
        if (sets && soundSet >= 0 && soundSet < sets->getRowCount()) {
            auto type = boost::to_lower_copy(sets->getString(soundSet, "armortype"));
            if (!type.empty()) {
                return type;
            }
        }
        return "leather";
    }
    // Doors and placeables: their placeableobjsnds armortype, else stone.
    auto type = objectSound(twoDas, target, "armortype");
    return type.empty() ? "stone" : type;
}

std::string weaponSound(ITwoDAs &twoDas, int row, const std::string &family, int variant) {
    auto table = twoDas.get("weaponsounds");
    if (!table || row < 0 || row >= table->getRowCount()) {
        return "";
    }
    return boost::to_lower_copy(table->getString(row, family + std::to_string(variant)));
}

std::string eventFamily(const std::string &event, int &variants) {
    std::string name(boost::to_lower_copy(event));
    if (name == "swingshort" || name == "swinglong" || name == "swingtwirl") {
        variants = 3;
        return name;
    }
    if (name == "clash") {
        variants = 2;
        return "clash";
    }
    if (name == "hitparry" || name == "parry") {
        variants = 2;
        return "parry";
    }
    variants = 0;
    return "";
}

std::string fallColumn(const std::string &surfaceSound) {
    if (surfaceSound == "WT") {
        return "fallwater";
    }
    if (surfaceSound == "MT") {
        return "fallmetal";
    }
    if (surfaceSound == "ST" || surfaceSound == "WD") {
        return "fallhard";
    }
    return "falldirt";
}

std::string objectSound(ITwoDAs &twoDas, const Object &object, const std::string &column) {
    int soundAppType = -1;
    if (auto door = dyn_cast<Door>(&object)) {
        auto doors = twoDas.get("genericdoors");
        if (doors && door->genericType() < doors->getRowCount()) {
            soundAppType = doors->getInt(door->genericType(), "soundapptype", -1);
        }
    } else if (auto placeable = dyn_cast<Placeable>(&object)) {
        auto placeables = twoDas.get("placeables");
        if (placeables && placeable->appearance() >= 0 && placeable->appearance() < placeables->getRowCount()) {
            soundAppType = placeables->getInt(placeable->appearance(), "soundapptype", -1);
        }
    }
    auto sounds = twoDas.get("placeableobjsnds");
    if (!sounds || soundAppType < 0 || soundAppType >= sounds->getRowCount()) {
        return "";
    }
    return boost::to_lower_copy(sounds->getString(soundAppType, column));
}

} // namespace combatsounds

} // namespace game

} // namespace reone
