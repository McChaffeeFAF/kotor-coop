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
#include <set>

#include "../effect.h"

namespace reone {

namespace game {

class LinkEffectsEffect : public Effect {
public:
    LinkEffectsEffect(std::shared_ptr<Effect> childEffect, std::shared_ptr<Effect> parentEffect) :
        Effect(EffectType::LinkEffects),
        _childEffect(std::move(childEffect)),
        _parentEffect(std::move(parentEffect)) {
    }

    void applyTo(Object &object) override;
    bool onApply(Object &object, const EffectInstance &instance) override;
    void onRemove(Object &object, const EffectInstance &instance) override;
    /** Periodic children (poison, regeneration) tick with the link; it ends when one of them ends. */
    bool onUpdate(Object &object, const EffectInstance &instance, float dt) override;
    void retireAreaRuntime(
        const std::set<const Object *> &retainedObjects) override;

    /** True when this link, or a link nested in it, carries an effect of `type`. */
    bool containsType(EffectType type) const;
    /**
     * As containsType, for the application `instanceId`: a child that the
     * target refused (immunity, invalid value) does not count.
     */
    bool containsType(EffectType type, EffectId instanceId) const;

    /** False when the target refused `child` when application `instanceId` went on. */
    bool accepts(EffectId instanceId, const Effect &child) const;

    const std::shared_ptr<Effect> &childEffect() const { return _childEffect; }
    const std::shared_ptr<Effect> &parentEffect() const { return _parentEffect; }

private:
    std::shared_ptr<Effect> _childEffect;
    std::shared_ptr<Effect> _parentEffect;
    // Per application (a link value can go on several targets): the direct
    // children that application's target refused.
    std::map<EffectId, std::set<const Effect *>> _refused;
};

} // namespace game

} // namespace reone
