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

#include "reone/system/portablemath.h"
#include "reone/game/effect/forcepushed.h"

#include "reone/game/combatfeedback.h"
#include "reone/game/object/creature.h"

namespace reone {

namespace game {

bool ForcePushedEffect::push(
    Creature &creature,
    std::optional<glm::vec3> source,
    bool ignoreObstacles,
    bool ignoreImmunity,
    const Creature *versus) {

    if (creature.isDead()) {
        return false;
    }
    if (!ignoreImmunity && creature.hasEffectImmunity(ImmunityType::Knockdown, versus)) {
        addEffectImmunityFeedback(creature.game(), versus, creature, ImmunityType::Knockdown);
        return false;
    }
    const glm::vec3 origin(creature.position());
    glm::vec2 away(0.0f);
    if (source) {
        away = glm::vec2(origin.x - source->x, origin.y - source->y);
    }
    if (glm::length(away) < 1e-3f) {
        // No source, or the source on the spot: straight backwards.
        float facing = creature.getFacing();
        away = glm::vec2(portable::sinf(facing), -portable::cosf(facing));
    } else {
        // Face the source, so the backwards fall goes away from it.
        creature.face(*source);
    }
    creature.startKnockback(away, kPushDistance, ignoreObstacles);
    creature.knockDown();
    return true;
}

bool ForcePushedEffect::onApply(Object &object, const EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return false;
    }
    std::optional<glm::vec3> source;
    auto creator = instance.boundCreator();
    const Creature *versus = nullptr;
    if (creator && creator.get() != &object) {
        source = creator->position();
        versus = dyn_cast<Creature>(creator.get());
    }
    return push(*creature, source, false, false, versus);
}

} // namespace game

} // namespace reone
