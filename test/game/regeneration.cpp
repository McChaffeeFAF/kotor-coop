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

// Natural hit point and force point regeneration (design note regeneration).

#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/effect/fpregenmodifier.h"
#include "reone/game/effect/temporaryforcepoints.h"
#include "reone/game/effect/temporaryhitpoints.h"
#include "reone/game/effect/vpregenmodifier.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/game/regeneration.h"
#include "reone/resource/2da.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

constexpr float kTick = 1.0f / 30.0f;

class RegenerationTest : public Test {
protected:
    void SetUp() override {
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _engine.options().game.regenRate = 1.0f;
        _game->regeneration().setRules(RegenerationRules::k1Defaults());
    }

    void TearDown() override {
        _engine.options().game.regenRate = 1.0f;
    }

    // Abilities 10 (modifier 0), no skill ranks: the bare base rate.
    std::shared_ptr<Creature> hurt(int current = 50, int maximum = 100) {
        auto result = _game->newCreature();
        for (auto ability : {Ability::Strength, Ability::Dexterity, Ability::Constitution,
                             Ability::Intelligence, Ability::Wisdom, Ability::Charisma}) {
            result->attributes().setAbilityScore(ability, 10);
        }
        result->setMaxHitPoints(maximum);
        result->setCurrentHitPoints(current);
        return result;
    }

    void run(Creature &creature, int ticks, bool partyMember = true) {
        for (int i = 0; i < ticks; ++i) {
            _game->regeneration().updateCreature(creature, kTick, partyMember);
        }
    }

    TestEngine _engine;
    StubConsole _console;
    std::unique_ptr<Game> _game;
};

} // namespace

TEST_F(RegenerationTest, out_of_combat_a_bare_party_member_gains_one_hit_point_per_six_seconds) {
    auto pc = hurt();
    run(*pc, 179);
    EXPECT_EQ(50, pc->currentHitPoints());
    run(*pc, 1);
    EXPECT_EQ(51, pc->currentHitPoints());
    run(*pc, 180 * 9);
    EXPECT_EQ(60, pc->currentHitPoints());
}

TEST_F(RegenerationTest, constitution_and_healing_skill_raise_the_hit_point_rate) {
    auto pc = hurt();
    pc->attributes().setAbilityScore(Ability::Constitution, 14); // +2
    pc->attributes().setSkillRank(SkillType::TreatInjury, 4);
    // (1.0 + 0.3 * 2 + 0.1 * skill) / 6 per second.
    const float skill = static_cast<float>(pc->getSkillValue(SkillType::TreatInjury));
    const float perSecond = (1.0f + 0.6f + 0.1f * skill) / 6.0f;
    run(*pc, 30 * 60);
    EXPECT_EQ(50 + static_cast<int>(std::floor(perSecond * 60.0f + 1e-4f)), pc->currentHitPoints());
}

TEST_F(RegenerationTest, the_pure_rate_matches_the_k2_rule) {
    RegenerationRules rules = RegenerationRules::k2Defaults();
    RegenerationSubject subject;
    subject.constitutionModifier = 2;
    subject.healingSkill = 4;
    EXPECT_NEAR((1.0f + 0.6f + 0.4f) / 6.0f, naturalHitPointsPerSecond(rules, subject), 1e-6f);
    subject.inCombat = true;
    EXPECT_NEAR(1.0f / 6.0f, naturalHitPointsPerSecond(rules, subject), 1e-6f);
    subject.vitalityModifierEffects = 1;
    EXPECT_NEAR(2.0f / 6.0f, naturalHitPointsPerSecond(rules, subject), 1e-6f);
    subject = RegenerationSubject();
    subject.constitutionModifier = -5;
    EXPECT_EQ(0.0f, naturalHitPointsPerSecond(rules, subject));
}

TEST_F(RegenerationTest, in_combat_a_bare_party_member_gains_no_hit_points) {
    auto pc = hurt();
    pc->activateCombat();
    ASSERT_TRUE(pc->isInCombat());
    run(*pc, 30 * 60);
    EXPECT_EQ(50, pc->currentHitPoints());
}

TEST_F(RegenerationTest, only_party_members_regenerate_naturally) {
    auto npc = hurt();
    run(*npc, 30 * 60, false);
    EXPECT_EQ(50, npc->currentHitPoints());
}

TEST_F(RegenerationTest, the_disable_flag_stops_hit_points) {
    auto pc = hurt();
    _game->party().setRegenerationDisabled(true);
    run(*pc, 30 * 60);
    EXPECT_EQ(50, pc->currentHitPoints());
    _game->party().setRegenerationDisabled(false);
    run(*pc, 180);
    EXPECT_EQ(51, pc->currentHitPoints());
}

TEST_F(RegenerationTest, hit_points_stop_at_the_maximum) {
    auto pc = hurt(98, 100);
    run(*pc, 30 * 60);
    EXPECT_EQ(100, pc->currentHitPoints());
}

TEST_F(RegenerationTest, the_dead_do_not_regenerate) {
    auto pc = hurt(0, 100);
    ASSERT_TRUE(pc->isDead());
    run(*pc, 30 * 60);
    EXPECT_EQ(0, pc->currentHitPoints());
}

TEST_F(RegenerationTest, temporary_hit_points_do_not_count_against_the_cap) {
    auto pc = hurt(90, 100);
    pc->applyEffect(std::make_shared<TemporaryHitPointsEffect>(20), DurationType::Permanent);
    ASSERT_EQ(110, pc->currentHitPoints());
    run(*pc, 30 * 600);
    // Permanent part refills to 100, plus the 20-point pool.
    EXPECT_EQ(120, pc->currentHitPoints());
}

TEST_F(RegenerationTest, the_rate_option_multiplies_out_of_combat_regeneration) {
    _engine.options().game.regenRate = 2.0f;
    auto pc = hurt();
    run(*pc, 89);
    EXPECT_EQ(50, pc->currentHitPoints());
    run(*pc, 1);
    EXPECT_EQ(51, pc->currentHitPoints());
    run(*pc, 90 * 9);
    EXPECT_EQ(60, pc->currentHitPoints());
}

TEST_F(RegenerationTest, the_rate_option_does_not_touch_in_combat_regeneration) {
    _engine.options().game.regenRate = 4.0f;
    auto pc = hurt();
    pc->activateCombat();
    run(*pc, 30 * 60);
    EXPECT_EQ(50, pc->currentHitPoints());
}

// Force point pools are driven through the same step the engine uses; a test
// creature has no Force class, so its derived maximum is zero.
static int simulateForce(const RegenerationRules &rules, RegenerationSubject subject, int start, int cap, int ticks, float speed = 1.0f) {
    RegenerationAccumulator accumulator;
    int current = start;
    const double perSecond = (subject.inCombat ? 1.0f : speed) * naturalForcePointsPerSecond(rules, subject);
    for (int i = 0; i < ticks; ++i) {
        current = regenerateTowards(accumulator, perSecond, kTick, current, cap);
    }
    return current;
}

TEST_F(RegenerationTest, k1_force_points_refill_in_ninety_seconds_out_of_combat) {
    RegenerationSubject subject;
    subject.maxForcePoints = 45;
    auto rules = RegenerationRules::k1Defaults();
    EXPECT_EQ(22, simulateForce(rules, subject, 0, 45, 30 * 45));
    EXPECT_EQ(45, simulateForce(rules, subject, 0, 45, 30 * 90));
    EXPECT_EQ(45, simulateForce(rules, subject, 0, 45, 30 * 200));
    // Twice as fast at 2x.
    EXPECT_EQ(45, simulateForce(rules, subject, 0, 45, 30 * 45, 2.0f));
}

TEST_F(RegenerationTest, k1_force_points_do_not_regenerate_in_combat) {
    RegenerationSubject subject;
    subject.maxForcePoints = 45;
    subject.inCombat = true;
    EXPECT_EQ(0, simulateForce(RegenerationRules::k1Defaults(), subject, 0, 45, 30 * 60));
}

TEST_F(RegenerationTest, force_regeneration_stops_at_maximum_plus_the_temporary_pool) {
    RegenerationSubject subject;
    subject.maxForcePoints = 45;
    // Cap = maximum + a 10-point temporary pool.
    EXPECT_EQ(55, simulateForce(RegenerationRules::k1Defaults(), subject, 0, 55, 30 * 200));
    // A value already above the cap is left alone.
    RegenerationAccumulator accumulator;
    EXPECT_EQ(60, regenerateTowards(accumulator, 1.0, kTick, 60, 55));
}

TEST_F(RegenerationTest, fp_regen_modifier_effects_speed_force_regeneration) {
    RegenerationSubject subject;
    subject.maxForcePoints = 45;
    auto rules = RegenerationRules::k1Defaults();
    int slow = simulateForce(rules, subject, 0, 45, 30 * 20);
    subject.forceModifierPercent = 100;
    int fast = simulateForce(rules, subject, 0, 45, 30 * 20);
    EXPECT_EQ(10, slow);
    EXPECT_EQ(20, fast);
}

TEST_F(RegenerationTest, k2_force_points_regenerate_at_a_tenth_in_combat) {
    _game->regeneration().setRules(RegenerationRules::k2Defaults());
    RegenerationSubject subject;
    subject.maxForcePoints = 90;
    EXPECT_NEAR(1.0f, naturalForcePointsPerSecond(RegenerationRules::k2Defaults(), subject), 1e-6f);
    subject.inCombat = true;
    EXPECT_NEAR(0.1f, naturalForcePointsPerSecond(RegenerationRules::k2Defaults(), subject), 1e-6f);
    subject.forceModifierPercent = 50;
    EXPECT_NEAR(0.6f, naturalForcePointsPerSecond(RegenerationRules::k2Defaults(), subject), 1e-6f);
}

TEST_F(RegenerationTest, vp_regen_modifier_effects_double_hit_point_regeneration) {
    auto pc = hurt();
    pc->applyEffect(std::make_shared<VPRegenModifierEffect>(25), DurationType::Permanent);
    run(*pc, 90);
    EXPECT_EQ(51, pc->currentHitPoints());
}

TEST_F(RegenerationTest, fractional_accumulation_is_exact_and_repeatable) {
    RegenerationAccumulator accumulator;
    int whole = 0;
    for (int i = 0; i < 180 * 1000; ++i) {
        accumulator.add((1.0 / 6.0) * kTick);
        whole += accumulator.take();
    }
    EXPECT_EQ(1000, whole);

    RegenerationAccumulator negative;
    negative.add(-1.75);
    EXPECT_EQ(-1, negative.take());
    EXPECT_NEAR(-0.75, negative.value(), 1e-12);

    // Two identical creatures, ticked the same, end identical at every step.
    auto a = hurt(10, 500);
    auto b = hurt(10, 500);
    a->attributes().setAbilityScore(Ability::Constitution, 13);
    b->attributes().setAbilityScore(Ability::Constitution, 13);
    for (int i = 0; i < 30 * 300; ++i) {
        _game->regeneration().updateCreature(*a, kTick, true);
        _game->regeneration().updateCreature(*b, kTick, true);
        ASSERT_EQ(a->currentHitPoints(), b->currentHitPoints()) << "tick " << i;
    }
    EXPECT_GT(a->currentHitPoints(), 10);
}

TEST_F(RegenerationTest, k1_table_force_values_are_read_and_zero_health_keeps_the_defaults) {
    auto table = TwoDA::Builder()
                     .columns({"label", "healthregen", "forceregen"})
                     .row({"InCombat", "0.0", "0.0"})
                     .row({"OutOfCombat", "0.0", "1.0"})
                     .build();
    auto rules = RegenerationRules::fromTable(table.get(), false);
    EXPECT_EQ(1.0f, rules.outOfCombatHitPointBase);
    EXPECT_EQ(0.0f, rules.inCombatHitPointBase);
    EXPECT_EQ(6.0f, rules.timePerHitPoint);
    EXPECT_EQ(1.0f, rules.outOfCombatForceBase);
    EXPECT_EQ(0.0f, rules.inCombatForceBase);
}

TEST_F(RegenerationTest, k2_table_values_are_read_by_row_label) {
    auto table = TwoDA::Builder()
                     .columns({"value"})
                     .row("timeperhp", {"3.0"})
                     .row("incombatfpbase", {"0.2"})
                     .build();
    auto rules = RegenerationRules::fromTable(table.get(), true);
    EXPECT_EQ(3.0f, rules.timePerHitPoint);
    EXPECT_NEAR(0.2f, rules.inCombatForceBase, 1e-6f);
    EXPECT_EQ(0.3f, rules.constitutionBonus);
}
