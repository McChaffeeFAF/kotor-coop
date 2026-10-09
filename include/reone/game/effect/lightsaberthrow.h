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
#include <vector>

#include "../effect.h"
#include "../object.h"

namespace reone {

namespace game {

class LightsaberThrowEffect : public Effect {
public:
    LightsaberThrowEffect(
        std::shared_ptr<Object> target1,
        std::shared_ptr<Object> target2,
        std::shared_ptr<Object> target3,
        int advancedDamage) :
        Effect(EffectType::LightsaberThrow),
        _target1(target1),
        _target2(target2),
        _target3(target3),
        _advancedDamage(advancedDamage) {
        // The game accepts the fourth argument but does not persist it in the
        // saved effect record. The three object slots are the complete wire payload.
        setSaveFacingObject(0, target1);
        setSaveFacingObject(1, target2);
        setSaveFacingObject(2, target3);
    }

    /**
     * The saber flies caster -> target 1 -> 2 -> 3 -> caster in kFlightTime
     * seconds at a constant speed; each target is hit as the saber reaches it
     * (design note c08b_effects).
     */
    static constexpr float kFlightTime = 1.94f;

    bool onApply(Object &object, const EffectInstance &instance) override;
    void onRemove(Object &object, const EffectInstance &instance) override;
    bool onUpdate(Object &object, const EffectInstance &instance, float dt) override;
    void retireAreaRuntime(
        const std::set<const Object *> &retainedObjects) override {
        for (auto *target : {&_target1, &_target2, &_target3}) {
            auto object = target->resolve();
            if (!object || retainedObjects.count(object.get()) == 0) {
                target->reset();
            }
        }
    }

private:
    RuntimeObjectRef<Object> _target1;
    RuntimeObjectRef<Object> _target2;
    RuntimeObjectRef<Object> _target3;
    int _advancedDamage;

    struct Hit {
        float time {0.0f};
        RuntimeObjectRef<Object> target;
    };
    struct Flight {
        float elapsed {0.0f};
        std::vector<Hit> hits; // in flight order
        size_t next {0};
    };
    std::map<EffectId, Flight> _flights;
};

} // namespace game

} // namespace reone
