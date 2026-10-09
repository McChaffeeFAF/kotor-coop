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

#include "reone/game/effect/poison.h"

#include "reone/resource/2da.h"

#include "reone/game/effect/abilitydecrease.h"
#include "reone/game/object.h"
#include "reone/game/object/creature.h"

namespace reone {

namespace game {

std::optional<PoisonProfile> loadPoisonProfile(const resource::TwoDA &poison, int row) {
    if (row < 0 || row >= poison.getRowCount()) {
        return std::nullopt;
    }
    static const char *kAbilityColumns[] = {"dam_str", "dam_dex", "dam_con", "dam_int", "dam_wis", "dam_chr"};
    PoisonProfile profile;
    profile.saveDC = poison.getInt(row, "dc_save", 0);
    profile.duration = poison.getFloat(row, "duration", 0.0f);
    profile.period = poison.getFloat(row, "period", 0.0f);
    profile.hitPointDamage = poison.getInt(row, "dam_hp", 0);
    profile.forceDamage = poison.getInt(row, "dam_fp", 0);
    for (size_t i = 0; i < profile.abilityDamage.size(); ++i) {
        profile.abilityDamage[i] = poison.getInt(row, kAbilityColumns[i], 0);
    }
    profile.abilityDuration = poison.getFloat(row, "abil_dur", 0.0f);
    return profile;
}

bool PoisonEffect::onApply(Object &object, const EffectInstance &instance) {
    auto creature = dynamic_cast<Creature *>(&object);
    if (!creature || !_profile || creature->isDead()) {
        return false;
    }
    // A successful Fortitude save (vs poison) resists the poison entirely.
    if (creature->rollFortitudeSave(_profile->saveDC, SavingThrowType::Poison)) {
        return false;
    }
    _progress[instance.id] = Progress();
    return true;
}

bool PoisonEffect::onUpdate(Object &object, const EffectInstance &instance, float dt) {
    if (!_profile) {
        return false;
    }
    auto &progress = _progress[instance.id];
    progress.elapsed += dt;
    progress.sinceDose += dt;
    // A dose lands at the end of each full period inside the duration.
    int maxDoses = _profile->period > 0.0f
                       ? static_cast<int>(_profile->duration / _profile->period + 1e-4f)
                       : 0;
    while (_profile->period > 0.0f &&
           progress.sinceDose >= _profile->period &&
           progress.doses < maxDoses) {
        progress.sinceDose -= _profile->period;
        ++progress.doses;
        dose(object, instance);
        auto creature = dynamic_cast<Creature *>(&object);
        if (creature && creature->isDead()) {
            return false;
        }
    }
    return progress.elapsed < _profile->duration;
}

void PoisonEffect::onRemove(Object &object, const EffectInstance &instance) {
    _progress.erase(instance.id);
}

void PoisonEffect::dose(Object &object, const EffectInstance &instance) {
    auto creature = dynamic_cast<Creature *>(&object);
    if (!creature) {
        return;
    }
    if (_profile->hitPointDamage > 0) {
        creature->damage(_profile->hitPointDamage, instance.boundCreator());
    }
    if (_profile->forceDamage > 0) {
        creature->drainForcePoints(_profile->forceDamage);
    }
    for (size_t i = 0; i < _profile->abilityDamage.size(); ++i) {
        int amount = _profile->abilityDamage[i];
        if (amount <= 0 || _profile->abilityDuration <= 0.0f) {
            continue;
        }
        auto decrease = std::make_shared<AbilityDecreaseEffect>(static_cast<Ability>(i), amount);
        object.applyEffect(decrease, DurationType::Temporary, _profile->abilityDuration);
    }
}

} // namespace game

} // namespace reone
