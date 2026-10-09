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

#include "reone/game/effect/forceshield.h"

#include "reone/game/di/services.h"
#include "reone/game/effect/visual.h"
#include "reone/game/object/creature.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"

namespace reone {

namespace game {

ForceShieldEffect::ForceShieldEffect(int shield, ServicesView &services) :
    Effect(EffectType::ForceShield),
    _shield(shield),
    _services(services) {
    setSaveFacingInteger(0, shield);
    auto table = services.resource.twoDas.get("forceshields");
    if (!table || shield < 0 || shield >= table->getRowCount()) {
        return;
    }
    _damageFlags = table->getInt(shield, "damageflags", 0);
    _vulnerFlags = table->getInt(shield, "vulnerflags", 0);
    _amount = table->getInt(shield, "amount", 0);
    _hitCap = table->getInt(shield, "resistance", 300);
    _visualEffect = table->getInt(shield, "visualeffectdef", -1);
    for (int i = 1; i <= 4; ++i) {
        std::string suffix = "_0" + std::to_string(i);
        int appearance = table->getInt(shield, "appearance" + suffix, -1);
        int visual = table->getInt(shield, "visualeffect" + suffix, -1);
        if (appearance >= 0 && visual >= 0) {
            _appearanceVisuals.emplace_back(appearance, visual);
        }
    }
}

bool ForceShieldEffect::onApply(Object &object, const EffectInstance &instance) {
    if (_amount <= 0 || object.isDead()) {
        return false;
    }
    _remaining[object.id()] = _amount;

    int visual = _visualEffect;
    if (auto *creature = dyn_cast<Creature>(&object)) {
        for (const auto &[appearance, appearanceVisual] : _appearanceVisuals) {
            if (appearance == creature->appearance()) {
                visual = appearanceVisual;
                break;
            }
        }
    }
    if (visual >= 0) {
        auto effect = std::make_shared<VisualEffect>(visual, false, _services);
        effect->onApply(object, instance);
        _visuals[object.id()] = std::move(effect);
    }
    return true;
}

void ForceShieldEffect::onRemove(Object &object, const EffectInstance &instance) {
    _remaining.erase(object.id());
    auto visual = _visuals.find(object.id());
    if (visual != _visuals.end()) {
        visual->second->onRemove(object, instance);
        _visuals.erase(visual);
    }
}

void ForceShieldEffect::retireAreaRuntime(
    const std::set<const Object *> &retainedObjects) {
    for (auto &[id, visual] : _visuals) {
        visual->retireAreaRuntime(retainedObjects);
    }
}

ForceShieldEffect::Absorption ForceShieldEffect::absorb(const Object &object, int damageFlags, int damage) {
    auto it = _remaining.find(object.id());
    if (damage <= 0 || it == _remaining.end() || it->second <= 0) {
        return {};
    }
    bool vulnerable = (damageFlags & _vulnerFlags) != 0;
    if (!vulnerable && (damageFlags & _damageFlags) == 0) {
        return {};
    }
    // K2 behaviour, owner decision (design note c08_effects): `resistance`
    // caps what the shield stops from one hit; the pool pays for what is stopped, twice over
    // for a vulnerable type, and the shield goes once less than 1 is left.
    int prevented = _hitCap > 0 ? std::min(damage, _hitCap) : damage;
    int wear = vulnerable ? 2 : 1;
    it->second -= prevented * wear;
    bool exhausted = it->second < 1;
    if (exhausted) {
        it->second = 0;
    }
    return {prevented, exhausted};
}

int ForceShieldEffect::remaining(const Object &object) const {
    auto it = _remaining.find(object.id());
    return it != _remaining.end() ? it->second : 0;
}

} // namespace game

} // namespace reone
