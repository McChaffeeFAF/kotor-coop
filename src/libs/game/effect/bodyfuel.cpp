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

#include "reone/game/effect/bodyfuel.h"
#include "reone/game/object/creature.h"

namespace reone {

namespace game {

// While on, force points are refilled every round; every Force power the
// creature pays for also costs it that many hit points (Creature::spendForcePoints).
bool BodyFuelEffect::onApply(Object &object, const EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature || creature->isDead()) {
        return false;
    }
    _elapsed[instance.id] = 0.0f;
    return true;
}

void BodyFuelEffect::onRemove(Object &, const EffectInstance &instance) {
    _elapsed.erase(instance.id);
}

bool BodyFuelEffect::onUpdate(Object &object, const EffectInstance &instance, float dt) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return false;
    }
    float &elapsed = _elapsed[instance.id];
    elapsed += dt;
    while (elapsed >= kRefillInterval) {
        elapsed -= kRefillInterval;
        if (!creature->isDead()) {
            creature->setCurrentForcePoints(creature->maxForcePoints() + creature->temporaryForcePoints());
        }
    }
    return true;
}

} // namespace game

} // namespace reone
