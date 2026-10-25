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

#include "reone/game/effect/heal.h"

#include <algorithm>

#include "reone/game/combatfeedback.h"
#include "reone/game/object/creature.h"

namespace reone {

namespace game {

void HealEffect::applyTo(Object &object) {
    object.heal(_damageToHeal);
}

bool HealEffect::onApply(Object &object, const EffectInstance &instance) {
    const int before = object.currentHitPoints();
    applyTo(object);
    // The line names the hit points restored, not those asked for.
    const int healed = std::max(0, object.currentHitPoints() - before);
    if (const auto *creature = dyn_cast<Creature>(&object)) {
        auto creator = instance.boundCreator();
        addHitPointHealingFeedback(object.game(), creator.get(), *creature, healed);
    }
    return true;
}

} // namespace game

} // namespace reone
