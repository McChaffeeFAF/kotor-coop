/*
 * Copyright (c) 2026 The reone project contributors
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

#include "reone/game/d20/forms.h"

#include <algorithm>

namespace reone {

namespace game {

namespace forms {

bool isForm(int spell) {
    return spell >= kShiiCho && spell <= kMastery;
}

bool isSaberForm(int spell) {
    return spell >= kShiiCho && spell <= kJuyo;
}

bool isForceForm(int spell) {
    return spell >= kChannel && spell <= kMastery;
}

bool isInEffect(int activeForm, int form, bool wieldsLightsaber) {
    if (activeForm != form || !isForm(form)) {
        return false;
    }
    return !isSaberForm(form) || wieldsLightsaber;
}

int saberFormInEffect(int activeForm, bool wieldsLightsaber) {
    return isSaberForm(activeForm) && wieldsLightsaber ? activeForm : kNoForm;
}

int attackModifier(int saberForm, bool targetWieldsLightsaber) {
    switch (saberForm) {
    case kShiiCho:
        return 1;
    case kMakashi:
        return targetWieldsLightsaber ? 3 : 0;
    case kShien:
        return 2;
    case kNiman:
        return 1;
    default:
        return 0;
    }
}

int damageModifier(int saberForm) {
    return saberForm == kMakashi ? 3 : 0;
}

int defenseModifier(int saberForm, bool attackerIsCurrentTarget) {
    switch (saberForm) {
    case kShiiCho:
        return attackerIsCurrentTarget ? 0 : 3;
    case kSoresu:
        return attackerIsCurrentTarget ? 2 : 0;
    case kAtaru:
        return attackerIsCurrentTarget ? 3 : -2;
    case kShien:
        return attackerIsCurrentTarget ? -5 : 0;
    case kNiman:
        return 1;
    case kJuyo:
        return attackerIsCurrentTarget ? -2 : -4;
    default:
        return 0;
    }
}

int threatRangeModifier(int saberForm) {
    switch (saberForm) {
    case kSoresu:
        return -1;
    case kAtaru:
        return 1;
    default:
        return 0;
    }
}

int criticalMultiplierModifier(int saberForm) {
    return saberForm == kShien ? 1 : 0;
}

int criticalConfirmationBonus(int saberForm) {
    return saberForm == kJuyo ? 4 : 0;
}

int extraAttacks(int saberForm) {
    return saberForm == kJuyo ? 1 : 0;
}

int forceSaveModifier(int activeForm, bool wieldsLightsaber) {
    switch (saberFormInEffect(activeForm, wieldsLightsaber)) {
    case kMakashi:
        return 2;
    case kNiman:
        return 1;
    case kJuyo:
        return -4;
    default:
        break;
    }
    switch (activeForm) {
    case kChannel:
        return 2;
    case kMastery:
        return -4;
    default:
        return 0;
    }
}

int opponentForceSaveModifier(int activeForm) {
    return activeForm == kMastery ? -2 : 0;
}

float forcePointCostMultiplier(int activeForm) {
    return activeForm == kPotency || activeForm == kMastery ? 1.2f : 1.0f;
}

float inCombatForceRegenBase(int activeForm, bool wieldsLightsaber, float inCombatBase, float outOfCombatBase) {
    if (activeForm == kNiman && wieldsLightsaber) {
        return outOfCombatBase * 0.3f;
    }
    if (activeForm == kAffinity) {
        return outOfCombatBase * 0.6f;
    }
    return inCombatBase;
}

float forceRegenBonus(int activeForm) {
    return activeForm == kChannel ? 0.5f : 0.0f;
}

enum class FormLine {
    None,
    Guardian,
    Consular,
    Sentinel
};

static FormLine lineOf(ClassType type) {
    switch (type) {
    case ClassType::JediGuardian:
    case ClassType::JediWeaponMaster:
    case ClassType::SithMarauder:
        return FormLine::Guardian;
    case ClassType::JediConsular:
    case ClassType::JediMaster:
    case ClassType::SithLord:
        return FormLine::Consular;
    case ClassType::JediSentinel:
    case ClassType::JediWatchman:
    case ClassType::SithAssassin:
        return FormLine::Sentinel;
    default:
        return FormLine::None;
    }
}

std::vector<int> formsDue(ClassType lineClass, int forceLevel, int goodEvil) {
    std::vector<int> result;
    auto line = lineOf(lineClass);
    if (line == FormLine::None || forceLevel < 1) {
        return result;
    }
    auto add = [&result](int form) {
        if (std::find(result.begin(), result.end(), form) == result.end()) {
            result.push_back(form);
        }
    };
    // K2's teachers' order, story steps 0, 1 and 2.
    int darkFirst = goodEvil < 50 ? kPotency : kAffinity;
    int lightSecond = goodEvil < 50 ? kAffinity : kPotency;
    int steps[3] {kNoForm, kNoForm, kNoForm};
    switch (line) {
    case FormLine::Guardian:
        steps[0] = kAtaru;
        steps[1] = kShien;
        steps[2] = kJuyo;
        break;
    case FormLine::Consular:
        steps[0] = darkFirst;
        steps[1] = lightSecond;
        steps[2] = kMastery;
        break;
    case FormLine::Sentinel:
        steps[0] = kShien;
        steps[1] = kAtaru;
        steps[2] = kNiman;
        break;
    default:
        break;
    }
    struct Grant {
        int level;
        int form;
    };
    const Grant schedule[] {
        {1, kShiiCho},
        {1, kChannel},
        {2, kSoresu},
        {4, steps[0]},
        {6, kMakashi},
        {8, steps[1]},
        {10, kNiman},
        {12, steps[2]}};
    for (const auto &grant : schedule) {
        if (grant.level <= forceLevel && grant.form != kNoForm) {
            add(grant.form);
        }
    }
    return result;
}

} // namespace forms

} // namespace game

} // namespace reone
