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

#include <array>
#include <map>
#include <optional>

#include "../effect.h"

namespace reone {

namespace resource {
class TwoDA;
}

namespace game {

/** One row of poison.2da. */
struct PoisonProfile {
    int saveDC {0};         // dc_save: Fortitude DC that resists the poison
    float duration {0.0f};  // duration: seconds the poison lasts
    float period {0.0f};    // period: seconds between doses
    int hitPointDamage {0}; // dam_hp: hit points lost per dose
    int forceDamage {0};    // dam_fp: force points lost per dose
    std::array<int, 6> abilityDamage {}; // dam_str..dam_chr, by Ability, per dose
    float abilityDuration {0.0f}; // abil_dur: seconds each ability loss lasts
};

class PoisonEffect : public Effect {
public:
    PoisonEffect(Poison poisonType, std::optional<PoisonProfile> profile = std::nullopt) :
        Effect(EffectType::Poison),
        _poisonType(poisonType),
        _profile(std::move(profile)) {
        setSaveFacingInteger(0, static_cast<int>(poisonType));
    }

    bool onApply(Object &object, const EffectInstance &instance) override;
    bool onUpdate(Object &object, const EffectInstance &instance, float dt) override;
    void onRemove(Object &object, const EffectInstance &instance) override;

    Poison poisonType() const { return _poisonType; }
    const std::optional<PoisonProfile> &profile() const { return _profile; }

private:
    struct Progress {
        float elapsed {0.0f};
        float sinceDose {0.0f};
        int doses {0};
    };

    Poison _poisonType;
    std::optional<PoisonProfile> _profile;
    // One effect value can be applied to several objects (area grenades).
    std::map<EffectId, Progress> _progress;

    void dose(Object &object, const EffectInstance &instance);
};

/** Reads a poison.2da row; nullopt when the row is out of range. */
std::optional<PoisonProfile> loadPoisonProfile(const resource::TwoDA &poison, int row);

} // namespace game

} // namespace reone
