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

#pragma once

#include <optional>

#include "../effect.h"

namespace reone {

namespace game {

class Creature;

/**
 * Force push: the target is thrown away from the effect's creator along the
 * walkmesh and knocked down (design note c08_effects).
 */
class ForcePushedEffect : public Effect {
public:
    ForcePushedEffect() :
        Effect(EffectType::ForcePushed) {
    }

    bool onApply(Object &object, const EffectInstance &instance) override;

    /** How far a push throws a creature, metres. */
    // K2 behaviour: a push throws the target 5 m.
    static constexpr float kPushDistance = 5.0f;

    /**
     * Throw and knock down a creature, away from source (straight backwards
     * when there is no usable source). Returns false when nothing happened:
     * a dead creature, or immunity to knockdown unless ignoreImmunity.
     */
    static bool push(
        Creature &creature,
        std::optional<glm::vec3> source,
        bool ignoreObstacles,
        bool ignoreImmunity,
        const Creature *versus);
};

} // namespace game

} // namespace reone
