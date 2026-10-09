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

#include <map>

#include "../effect.h"

namespace reone {

namespace game {

class BodyFuelEffect : public Effect {
public:
    BodyFuelEffect() :
        Effect(EffectType::BodyFuel) {
    }

    /** Force points are refilled every round (3 s) while it lasts. */
    static constexpr float kRefillInterval = 3.0f;

    bool onApply(Object &object, const EffectInstance &instance) override;
    void onRemove(Object &object, const EffectInstance &instance) override;
    bool onUpdate(Object &object, const EffectInstance &instance, float dt) override;

private:
    std::map<EffectId, float> _elapsed;
};

} // namespace game

} // namespace reone
