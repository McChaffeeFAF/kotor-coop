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

// Presentation only: the beam's visual row plays its impact model and sound
// at the target. The procedural beam between the two (progfx_duration) is
// not drawn yet.
void BeamEffect::applyTo(Object &object) {
    if (!_impact) {
        return;
    }
    // The impact row's models attach to the target like any visual effect.
    _impact->applyTo(object);
}

void BeamEffect::onRemove(Object &object, const EffectInstance &instance) {
    if (_impact) {
        _impact->onRemove(object, instance);
    }
}

} // namespace game

} // namespace reone
