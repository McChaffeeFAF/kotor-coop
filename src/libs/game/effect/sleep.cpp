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

#include "reone/game/effect/sleep.h"

#include "reone/game/object/creature.h"

namespace reone {

namespace game {

bool SleepEffect::onApply(Object &object, const EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature || creature->isDead()) {
        return false;
    }
    auto creator = instance.boundCreator();
    if (creature->hasEffectImmunity(
            ImmunityType::Sleep,
            creator ? dyn_cast<Creature>(creator.get()) : nullptr)) {
        return false;
    }
    creature->enterHeldPose();
    return true;
}

void SleepEffect::onRemove(Object &object, const EffectInstance &) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->leaveHeldPose();
    }
}

} // namespace game

} // namespace reone
