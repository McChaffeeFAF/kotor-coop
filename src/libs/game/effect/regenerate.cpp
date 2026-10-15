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

#include "reone/game/effect/regenerate.h"

#include "reone/game/object/creature.h"
#include "reone/system/cast.h"

namespace reone {

namespace game {

bool RegenerateEffect::onApply(Object &object, const EffectInstance &) {
    if (_amount <= 0 || _intervalSeconds <= 0.0f) {
        return false;
    }
    _elapsed[object.id()] = 0.0f;
    return true;
}

void RegenerateEffect::onRemove(Object &object, const EffectInstance &) {
    _elapsed.erase(object.id());
}

bool RegenerateEffect::onUpdate(Object &object, const EffectInstance &, float dt) {
    float &elapsed = _elapsed[object.id()];
    elapsed += dt;
    while (elapsed >= _intervalSeconds) {
        elapsed -= _intervalSeconds;
        // Nothing regenerates on a party member that is down (no vitality, not dead).
        auto *creature = dyn_cast<Creature>(&object);
        if (creature && creature->isTemporarilyDead()) {
            continue;
        }
        if (!object.isDead() && object.currentHitPoints() < object.maxHitPoints()) {
            object.heal(std::min(_amount, object.maxHitPoints() - object.currentHitPoints()));
        }
    }
    return true;
}

} // namespace game

} // namespace reone
