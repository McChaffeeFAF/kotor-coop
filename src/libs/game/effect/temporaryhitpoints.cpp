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

#include "reone/game/effect/temporaryhitpoints.h"

#include "reone/game/object/creature.h"

namespace reone {

namespace game {

// A separate pool, as for temporary force points: the current hit points
// include it, the maximum does not. Damage lowers the current value as usual;
// when the effect ends the pool is taken away again, and a creature left
// below 1 hit point dies (design note c08b_effects).

bool TemporaryHitPointsEffect::onApply(Object &object, const EffectInstance &) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature || creature->isDead() || _hitPoints <= 0) {
        return false;
    }
    creature->addTemporaryHitPoints(_hitPoints);
    return true;
}

void TemporaryHitPointsEffect::onRemove(Object &object, const EffectInstance &) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->removeTemporaryHitPoints(_hitPoints);
    }
}

} // namespace game

} // namespace reone
