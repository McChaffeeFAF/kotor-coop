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

#include "reone/game/effect/beam.h"

namespace reone {

namespace game {

// Presentation only: the beam's visual row plays its sound and impact models
// at the target, and its programmed beam model (progfx_duration) from the
// effector's body node to the target (design note v13_beams).
void BeamEffect::applyTo(Object &object) {
    if (!_impact) {
        return;
    }
    auto effector = _effector.resolve();
    _impact->setBeamSource(effector ? effector->id() : 0, static_cast<int>(_bodyPart));
    _impact->applyTo(object);
}

void BeamEffect::onRemove(Object &object, const EffectInstance &instance) {
    if (_impact) {
        _impact->onRemove(object, instance);
    }
}

} // namespace game

} // namespace reone
