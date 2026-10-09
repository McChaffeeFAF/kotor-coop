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

#include "reone/game/d20/k2feats.h"

namespace reone {

namespace game {

namespace k2feats {

// K2 feat.2da rows (the merge keeps K2's indices for them).
static constexpr int kTargeting1 = 126;      // .. Targeting X = 135
static constexpr int kCloseCombat = 139;     // Improved = 140
static constexpr int kIgnorePain1 = 150;     // .. III = 152
static constexpr int kIncreaseCombatDamage1 = 153; // .. III = 155
static constexpr int kSuperiorLightsaber1 = 156;   // .. III = 158
static constexpr int kSuperiorTwoWeapon1 = 159;    // .. III = 161
static constexpr int kInnerStrength1 = 169;        // .. III = 171
static constexpr int kIncreaseMeleeDamage1 = 172;  // .. III = 174
static constexpr int kDualStrike = 190;            // Improved 191, Master 192
static constexpr int kStealthRun = 197;
static constexpr int kUnarmedSpecialist1 = 212;    // .. VIII = 219
static constexpr int kWookieeToughness2 = 224;
static constexpr int kWookieeToughness3 = 225;
static constexpr int kPreciseShot1 = 240;          // .. V = 244
static constexpr int kEvasion = 125;

static bool has(const HasFeat &hasFeat, int feat) {
    return hasFeat && hasFeat(static_cast<FeatType>(feat));
}

// The highest tier owned of a chain of `count` consecutive rows, 1-based;
// 0 when none is owned.
static int highestTier(const HasFeat &hasFeat, int first, int count) {
    for (int tier = count; tier >= 1; --tier) {
        if (has(hasFeat, first + tier - 1)) {
            return tier;
        }
    }
    return 0;
}

int targetingAttackBonus(const HasFeat &hasFeat) {
    return highestTier(hasFeat, kTargeting1, 10);
}

int closeCombatAttackBonus(const HasFeat &hasFeat) {
    return (has(hasFeat, kCloseCombat) ? 1 : 0) + (has(hasFeat, kCloseCombat + 1) ? 1 : 0);
}

int superiorLightsaberFocusBonus(const HasFeat &hasFeat) {
    return highestTier(hasFeat, kSuperiorLightsaber1, 3);
}

int superiorTwoWeaponBonus(const HasFeat &hasFeat, bool offHand) {
    int tier = highestTier(hasFeat, kSuperiorTwoWeapon1, 3);
    if (offHand) {
        return tier == 3 ? 1 : 0;
    }
    static constexpr int kMainHand[] {0, 1, 2, 2};
    return kMainHand[tier];
}

int dualStrikeAttackBonus(const HasFeat &hasFeat) {
    int result = 0;
    for (int feat = kDualStrike; feat <= kDualStrike + 2; ++feat) {
        if (has(hasFeat, feat)) {
            result += 2;
        }
    }
    return result;
}

int combatDamageBonus(const HasFeat &hasFeat, bool ranged) {
    int tier = highestTier(hasFeat, kIncreaseCombatDamage1, 3);
    if (!ranged) {
        tier = std::max(tier, highestTier(hasFeat, kIncreaseMeleeDamage1, 3));
    }
    return 2 * tier;
}

int preciseShotDamageBonus(const HasFeat &hasFeat) {
    static constexpr int kBonus[] {0, 1, 2, 4, 6, 8};
    return kBonus[highestTier(hasFeat, kPreciseShot1, 5)];
}

int unarmedSpecialistDice(const HasFeat &hasFeat) {
    return highestTier(hasFeat, kUnarmedSpecialist1, 8);
}

int reduceDamage(const HasFeat &hasFeat, int damage) {
    if (damage <= 0) {
        return std::max(damage, 0);
    }
    int painTier = std::max(highestTier(hasFeat, kIgnorePain1, 3), highestTier(hasFeat, kInnerStrength1, 3));
    if (painTier > 0) {
        damage -= damage * 5 * painTier / 100;
    }
    if (has(hasFeat, kWookieeToughness3)) {
        int reduction = 8;
        if (damage - 8 > 0) {
            reduction += (damage - 8) * 10 / 100;
        }
        damage -= reduction;
    } else if (has(hasFeat, kWookieeToughness2)) {
        int reduction = 5;
        if (damage - 5 > 0) {
            reduction += (damage - 5) * 5 / 100;
        }
        damage -= reduction;
    }
    return std::max(damage, 0);
}

bool hasEvasion(const HasFeat &hasFeat) {
    return has(hasFeat, kEvasion);
}

int reflexAdjustedDamage(int damage, bool saved, bool evasion) {
    if (!saved) {
        return damage;
    }
    return evasion ? 0 : damage / 2;
}

FeatType classSkillFeat(SkillType skill) {
    switch (skill) {
    case SkillType::Awareness:
        return static_cast<FeatType>(183);
    case SkillType::ComputerUse:
        return static_cast<FeatType>(184);
    case SkillType::Demolitions:
        return static_cast<FeatType>(185);
    case SkillType::Repair:
        return static_cast<FeatType>(186);
    case SkillType::Security:
        return static_cast<FeatType>(187);
    case SkillType::Stealth:
        return static_cast<FeatType>(188);
    case SkillType::TreatInjury:
        return static_cast<FeatType>(189);
    default:
        return FeatType::Invalid;
    }
}

bool hasStealthRun(const HasFeat &hasFeat) {
    return has(hasFeat, kStealthRun);
}

} // namespace k2feats

} // namespace game

} // namespace reone
