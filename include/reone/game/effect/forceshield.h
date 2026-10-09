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
#include <utility>
#include <vector>

#include "../effect.h"

namespace reone {

namespace game {

class ServicesView;
class VisualEffect;

/**
 * An energy shield from forceshields.2da: absorbs a total amount of damage
 * of its listed types, then collapses (design note c08_effects).
 */
class ForceShieldEffect : public Effect {
public:
    ForceShieldEffect(int shield, ServicesView &services);

    bool onApply(Object &object, const EffectInstance &instance) override;
    void onRemove(Object &object, const EffectInstance &instance) override;
    void retireAreaRuntime(
        const std::set<const Object *> &retainedObjects) override;

    struct Absorption {
        int prevented {0};
        bool exhausted {false};
    };

    /** Take a hit of damageFlags on the shield on this object. */
    Absorption absorb(const Object &object, int damageFlags, int damage);

    /** Damage the shield on this object can still absorb (0 when not up). */
    int remaining(const Object &object) const;

    int damageFlags() const { return _damageFlags; }
    int amount() const { return _amount; }

private:
    int _shield;
    ServicesView &_services;
    int _damageFlags {0};
    int _vulnerFlags {0};
    int _amount {0};
    int _hitCap {300}; // most the shield stops from one hit (forceshields.2da `resistance`)
    int _visualEffect {-1};
    std::vector<std::pair<int, int>> _appearanceVisuals; /**< appearance row, visual */

    std::map<uint32_t, int> _remaining;
    std::map<uint32_t, std::shared_ptr<VisualEffect>> _visuals;
};

} // namespace game

} // namespace reone
