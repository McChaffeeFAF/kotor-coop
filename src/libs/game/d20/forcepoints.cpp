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

#include "reone/game/d20/forcepoints.h"

#include <algorithm>

#include "reone/game/d20/attributes.h"
#include "reone/game/d20/class.h"
#include "reone/game/d20/spell.h"
#include "reone/resource/2da.h"

namespace reone {

namespace game {

namespace {

// A surcharge (multiplier above 1) shrinks by this much per Charisma point.
constexpr float kCharismaSurchargeRelief = 0.05f;

bool hasForceClass(const CreatureAttributes &attributes) {
    for (const auto &[clazz, level] : attributes.classLevels()) {
        if (clazz && clazz->isSpellCaster()) {
            return true;
        }
    }
    return false;
}

} // namespace

int computeMaxForcePoints(const CreatureAttributes &attributes, const ForcePointSubject &subject) {
    if (subject.droid) {
        return 0;
    }
    const auto &classes = attributes.classLevels();
    const int wisdom = attributes.getAbilityModifier(Ability::Wisdom);

    if (subject.playerProgression) {
        if (classes.empty() || !classes.back().first || !classes.back().first->isSpellCaster()) {
            return 0;
        }
        int result = 0;
        for (const auto &[clazz, level] : classes) {
            if (!clazz || !clazz->isSpellCaster()) {
                continue;
            }
            result += level * std::max(1, clazz->forcePointsPerLevel() + wisdom);
        }
        return std::max(0, result);
    }

    if (!hasForceClass(attributes)) {
        return 0;
    }
    const int level = attributes.getAggregateLevel();
    return std::max(0, std::max(level, subject.basePoints + wisdom * level));
}

int computeForcePowerCost(const Spell &spell, const ForcePowerPayer &payer, const resource::TwoDA *forceAdjust) {
    if (spell.forcePoints <= 0) {
        return 0;
    }
    float multiplier = 1.0f;
    if (payer.playerCharacter && forceAdjust && (spell.goodEvil == 'G' || spell.goodEvil == 'E')) {
        int row = std::clamp(payer.goodEvil, 0, 100) / 10;
        if (row < forceAdjust->getRowCount()) {
            multiplier = forceAdjust->getFloat(row, spell.goodEvil == 'G' ? "goodcost" : "evilcost", 1.0f);
        }
        if (multiplier > 1.0f && payer.waiveOppositeSurcharge) {
            multiplier = 1.0f;
        }
        if (multiplier > 1.0f && payer.charismaModifier > 0) {
            multiplier -= static_cast<float>(payer.charismaModifier) * kCharismaSurchargeRelief;
            if (multiplier < 1.0f) {
                multiplier = 1.0f;
            }
        }
    }
    multiplier *= payer.formMultiplier;
    int cost = static_cast<int>(static_cast<float>(spell.forcePoints) * multiplier);
    return std::max(1, cost);
}

} // namespace game

} // namespace reone
