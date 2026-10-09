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

#include "reone/game/effect/choke.h"

#include "reone/game/object/creature.h"

namespace reone {

namespace game {

void ChokeEffect::applyTo(Object &object) {
    // Held in the looping choke pose: no movement and no actions while the
    // effect lasts (Creature::canExecuteActions, updateModelAnimation).
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->setMovementType(Creature::MovementType::None);
        creature->resumeStateDrivenAnimation();
    }
}

void ChokeEffect::onRemove(Object &object, const EffectInstance &) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        if (!creature->isChoked()) {
            creature->resumeStateDrivenAnimation();
        }
    }
}

} // namespace game

} // namespace reone
