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

class RegenerateEffect : public Effect {
public:
    RegenerateEffect(int amount, float intervalSeconds) :
        Effect(EffectType::Regenerate),
        _amount(amount),
        _intervalSeconds(intervalSeconds) {
    }

    bool onApply(Object &object, const EffectInstance &instance) override;
    void onRemove(Object &object, const EffectInstance &instance) override;
    bool onUpdate(Object &object, const EffectInstance &instance, float dt) override;

private:
    int _amount;
    float _intervalSeconds;
    std::map<uint32_t, float> _elapsed; /**< per target, simulation seconds */
};

} // namespace game

} // namespace reone
