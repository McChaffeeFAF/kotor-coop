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

/**
 * The K2 systems merge, game rules (design note k2_merge): K2 feat
 * effects, the prestige classes, Force forms and the level-50 table.
 */

#include <gtest/gtest.h>

#include "reone/game/d20/k2feats.h"

#include <set>

using namespace reone;
using namespace reone::game;

namespace {

k2feats::HasFeat owning(std::set<int> feats) {
    return [feats](FeatType feat) { return feats.count(static_cast<int>(feat)) > 0; };
}

} // namespace

TEST(K2Feats, a_creature_without_k2_feats_gets_nothing) {
    auto none = owning({});
    EXPECT_EQ(0, k2feats::targetingAttackBonus(none));
    EXPECT_EQ(0, k2feats::closeCombatAttackBonus(none));
    EXPECT_EQ(0, k2feats::superiorLightsaberFocusBonus(none));
    EXPECT_EQ(0, k2feats::superiorTwoWeaponBonus(none, false));
    EXPECT_EQ(0, k2feats::dualStrikeAttackBonus(none));
    EXPECT_EQ(0, k2feats::combatDamageBonus(none, false));
    EXPECT_EQ(0, k2feats::preciseShotDamageBonus(none));
    EXPECT_EQ(0, k2feats::unarmedSpecialistDice(none));
    EXPECT_EQ(40, k2feats::reduceDamage(none, 40));
    EXPECT_FALSE(k2feats::hasEvasion(none));
    EXPECT_EQ(0, k2feats::targetingAttackBonus(nullptr));
}

TEST(K2Feats, targeting_takes_the_highest_tier) {
    EXPECT_EQ(1, k2feats::targetingAttackBonus(owning({126})));
    EXPECT_EQ(5, k2feats::targetingAttackBonus(owning({126, 127, 128, 129, 130})));
    // A higher tier alone counts in full, as the K2 engine checks tiers top down.
    EXPECT_EQ(8, k2feats::targetingAttackBonus(owning({133})));
}

TEST(K2Feats, close_combat_adds_one_per_feat) {
    EXPECT_EQ(1, k2feats::closeCombatAttackBonus(owning({139})));
    EXPECT_EQ(2, k2feats::closeCombatAttackBonus(owning({139, 140})));
}

TEST(K2Feats, melee_on_ranged_bonus_shrinks_with_close_combat) {
    EXPECT_EQ(6, k2feats::meleeOnRangedAttackBonus(owning({})));
    EXPECT_EQ(6, k2feats::meleeOnRangedAttackBonus(nullptr));
    EXPECT_EQ(4, k2feats::meleeOnRangedAttackBonus(owning({139})));
    EXPECT_EQ(2, k2feats::meleeOnRangedAttackBonus(owning({139, 140})));
    EXPECT_EQ(2, k2feats::meleeOnRangedAttackBonus(owning({140})));
}

TEST(K2Feats, superior_two_weapon_fighting_main_and_off_hand) {
    EXPECT_EQ(1, k2feats::superiorTwoWeaponBonus(owning({159}), false));
    EXPECT_EQ(2, k2feats::superiorTwoWeaponBonus(owning({159, 160}), false));
    EXPECT_EQ(2, k2feats::superiorTwoWeaponBonus(owning({159, 160, 161}), false));
    EXPECT_EQ(0, k2feats::superiorTwoWeaponBonus(owning({159, 160}), true));
    EXPECT_EQ(1, k2feats::superiorTwoWeaponBonus(owning({159, 160, 161}), true));
}

TEST(K2Feats, dual_strike_is_cumulative) {
    EXPECT_EQ(2, k2feats::dualStrikeAttackBonus(owning({190})));
    EXPECT_EQ(6, k2feats::dualStrikeAttackBonus(owning({190, 191, 192})));
}

TEST(K2Feats, combat_and_melee_damage_chains) {
    EXPECT_EQ(4, k2feats::combatDamageBonus(owning({153, 154}), true));
    EXPECT_EQ(6, k2feats::combatDamageBonus(owning({174}), false));
    // Increase Melee Damage does not apply to ranged attacks.
    EXPECT_EQ(0, k2feats::combatDamageBonus(owning({172, 173, 174}), true));
    EXPECT_EQ(8, k2feats::preciseShotDamageBonus(owning({240, 241, 242, 243, 244})));
    EXPECT_EQ(4, k2feats::preciseShotDamageBonus(owning({242})));
}

TEST(K2Feats, unarmed_specialist_dice) {
    EXPECT_EQ(1, k2feats::unarmedSpecialistDice(owning({212})));
    EXPECT_EQ(8, k2feats::unarmedSpecialistDice(owning({219})));
}

TEST(K2Feats, damage_reduction_feats) {
    // Ignore Pain III / Inner Strength III: 15% off, truncated.
    EXPECT_EQ(85, k2feats::reduceDamage(owning({152}), 100));
    EXPECT_EQ(90, k2feats::reduceDamage(owning({170}), 100));
    EXPECT_EQ(7, k2feats::reduceDamage(owning({169}), 7)); // 5% of 7 truncates to 0
    // Wookiee Toughness II: 5 plus 5% of the rest; III: 8 plus 10% of the rest.
    EXPECT_EQ(100 - (5 + 95 * 5 / 100), k2feats::reduceDamage(owning({224}), 100));
    EXPECT_EQ(100 - (8 + 92 * 10 / 100), k2feats::reduceDamage(owning({224, 225}), 100));
    EXPECT_EQ(0, k2feats::reduceDamage(owning({225}), 6));
}

TEST(K2Feats, reflex_adjusted_damage_and_evasion) {
    EXPECT_EQ(20, k2feats::reflexAdjustedDamage(20, false, true));
    EXPECT_EQ(10, k2feats::reflexAdjustedDamage(20, true, false));
    EXPECT_EQ(0, k2feats::reflexAdjustedDamage(20, true, true));
    EXPECT_TRUE(k2feats::hasEvasion(owning({125})));
}

TEST(K2Feats, class_skill_feats) {
    EXPECT_EQ(static_cast<FeatType>(183), k2feats::classSkillFeat(SkillType::Awareness));
    EXPECT_EQ(static_cast<FeatType>(189), k2feats::classSkillFeat(SkillType::TreatInjury));
    EXPECT_EQ(FeatType::Invalid, k2feats::classSkillFeat(SkillType::Persuade));
}
