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

#include "reone/game/effect/linkeffects.h"

namespace reone {

namespace game {

// A link applies, lasts and is removed as one: both effects go on with it
// and come off with it. Instant children (e.g. damage) take effect at apply.
void LinkEffectsEffect::applyTo(Object &object) {
    for (const auto &effect : {_childEffect, _parentEffect}) {
        if (effect) {
            effect->applyTo(object);
        }
    }
}

// A child the target refuses (immune to it, or an invalid value) stays off on
// its own; the others still apply. A link whose every child is refused is
// refused as a whole.
bool LinkEffectsEffect::onApply(Object &object, const EffectInstance &instance) {
    auto &refused = _refused[instance.id];
    refused.clear();
    bool any = false;
    for (const auto &effect : {_childEffect, _parentEffect}) {
        if (!effect) {
            continue;
        }
        if (effect->onApply(object, instance)) {
            any = true;
        } else {
            refused.insert(effect.get());
        }
    }
    if (!any) {
        _refused.erase(instance.id);
    }
    return any;
}

void LinkEffectsEffect::onRemove(Object &object, const EffectInstance &instance) {
    for (const auto &effect : {_parentEffect, _childEffect}) {
        if (effect && accepts(instance.id, *effect)) {
            effect->onRemove(object, instance);
        }
    }
    _refused.erase(instance.id);
}

bool LinkEffectsEffect::onUpdate(Object &object, const EffectInstance &instance, float dt) {
    bool keep = true;
    for (const auto &effect : {_childEffect, _parentEffect}) {
        if (effect && accepts(instance.id, *effect) && !effect->onUpdate(object, instance, dt)) {
            keep = false;
        }
    }
    return keep;
}

bool LinkEffectsEffect::accepts(EffectId instanceId, const Effect &child) const {
    auto it = _refused.find(instanceId);
    return it == _refused.end() || it->second.count(&child) == 0;
}

bool LinkEffectsEffect::containsType(EffectType type, EffectId instanceId) const {
    for (const auto &effect : {_childEffect, _parentEffect}) {
        if (!effect || !accepts(instanceId, *effect)) {
            continue;
        }
        if (effect->type() == type) {
            return true;
        }
        if (auto link = std::dynamic_pointer_cast<LinkEffectsEffect>(effect);
            link && link->containsType(type, instanceId)) {
            return true;
        }
    }
    return false;
}

bool LinkEffectsEffect::containsType(EffectType type) const {
    for (const auto &effect : {_childEffect, _parentEffect}) {
        if (!effect) {
            continue;
        }
        if (effect->type() == type) {
            return true;
        }
        if (auto link = std::dynamic_pointer_cast<LinkEffectsEffect>(effect); link && link->containsType(type)) {
            return true;
        }
    }
    return false;
}

void LinkEffectsEffect::retireAreaRuntime(
    const std::set<const Object *> &retainedObjects) {
    if (_childEffect) _childEffect->retireAreaRuntime(retainedObjects);
    if (_parentEffect) _parentEffect->retireAreaRuntime(retainedObjects);
}

} // namespace game

} // namespace reone
