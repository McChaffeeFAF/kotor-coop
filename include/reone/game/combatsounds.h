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

#include <string>

namespace reone {

namespace resource {
class ITwoDAs;
}

namespace game {

class Creature;
class Object;

/**
 * Combat and object foley chosen from the game tables (design note c12_audio_cine).
 * Everything here is presentation: variant draws use the cosmetic stream.
 */
namespace combatsounds {

/** weaponsounds.2da row for the weapon a creature swings (baseitems weaponmattype). */
int weaponSoundRow(resource::ITwoDAs &twoDas, const Creature &attacker);

/** weaponsounds.2da material family hit on the target: cloth, leather, armor, forcefield, metal, wood, stone. */
std::string targetMaterial(resource::ITwoDAs &twoDas, const Object &target);

/** The weaponsounds.2da cell "<family><variant>" of a row; empty when absent. */
std::string weaponSound(resource::ITwoDAs &twoDas, int row, const std::string &family, int variant);

/**
 * Swing, clash and parry model events ("swingshort", "swinglong",
 * "swingtwirl", "clash", "hitparry"): the column family and its variant
 * count, or an empty family for any other event.
 */
std::string eventFamily(const std::string &event, int &variants);

/** appearancesndset body-fall column for a walkmesh surface sound code (DT, MT, WT, ...). */
std::string fallColumn(const std::string &surfaceSound);

/** placeableobjsnds.2da cell of a door or placeable ("opened", "closed", "destroyed", "locked", "armortype"). */
std::string objectSound(resource::ITwoDAs &twoDas, const Object &object, const std::string &column);

} // namespace combatsounds

} // namespace game

} // namespace reone
