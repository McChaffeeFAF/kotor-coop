/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#include "reone/game/effect/forcebody.h"
#include "reone/game/object/creature.h"

namespace reone {

namespace game {

// K2. Force powers are paid partly from hit points: Creature::forcePowerCost
// and Creature::spendForcePoints read the level (0, 1 or 2).
bool ForceBodyEffect::onApply(Object &object, const EffectInstance &) {
    return dyn_cast<Creature>(&object) && _level >= 0 && _level <= 2;
}

} // namespace game

} // namespace reone
