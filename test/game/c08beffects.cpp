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

// Batch C08b: the remaining effect bodies (design note c08b_effects).

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/effect/acincrease.h"
#include "reone/game/effect/blind.h"
#include "reone/game/effect/bodyfuel.h"
#include "reone/game/effect/crush.h"
#include "reone/game/effect/damage.h"
#include "reone/game/effect/damagedecrease.h"
#include "reone/game/effect/damageresistance.h"
#include "reone/game/effect/entangle.h"
#include "reone/game/effect/factionmodifier.h"
#include "reone/game/effect/forcebody.h"
#include "reone/game/effect/forcefizzle.h"
#include "reone/game/effect/forceresistancedecrease.h"
#include "reone/game/effect/forceresistanceincrease.h"
#include "reone/game/effect/fury.h"
#include "reone/game/effect/immunity.h"
#include "reone/game/effect/lightsaberthrow.h"
#include "reone/game/effect/linkeffects.h"
#include "reone/game/effect/mindtrick.h"
#include "reone/game/effect/savingthrowdecrease.h"
#include "reone/game/effect/savingthrowincrease.h"
#include "reone/game/effect/temporaryforcepoints.h"
#include "reone/game/effect/temporaryhitpoints.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/script/routines.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

constexpr float kTick = 1.0f / 30.0f;

class C08bTest : public Test {
protected:
    void SetUp() override {
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _routines = std::make_unique<Routines>(GameID::KotOR, _game.get(), &_engine.services());
        _routines->init();
    }

    std::shared_ptr<Creature> creature(glm::vec3 position = glm::vec3(0.0f)) {
        auto result = _game->newCreature();
        result->setPosition(position);
        result->setMaxHitPoints(100);
        result->setCurrentHitPoints(100);
        return result;
    }

    void run(Creature &target, float seconds) {
        int ticks = static_cast<int>(std::lround(seconds / kTick));
        for (int i = 0; i < ticks; ++i) {
            target.update(kTick);
        }
    }

    Variable call(const std::string &name, std::vector<Variable> args, std::shared_ptr<Object> caller = nullptr) {
        Routine &routine = _routines->get(_routines->getIndexByName(name));
        ExecutionContext execution;
        execution.routines = _routines.get();
        if (caller) {
            execution.args.push_back(Argument(ArgKind::Caller, Variable::ofObject(caller->id())));
        }
        return routine.invoke(args, execution);
    }

    void hit(Creature &target, int amount, DamageType type = DamageType::Fire) {
        target.applyEffect(std::make_shared<DamageEffect>(amount, type, DamagePower::Normal), DurationType::Instant);
    }

    TestEngine _engine;
    StubConsole _console;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
};

} // namespace

// Links: a refused child is not applied.

TEST_F(C08bTest, a_linked_child_the_target_is_immune_to_does_not_count) {
    auto target = creature();
    target->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::Entangle), DurationType::Permanent);
    int defense = target->getDefense();
    auto link = std::make_shared<LinkEffectsEffect>(
        std::make_shared<EntangleEffect>(),
        std::make_shared<ACIncreaseEffect>(2, ACBonus::Dodge, kAllDamageTypeFlags));
    target->applyEffect(link, DurationType::Permanent);
    EXPECT_FALSE(target->hasEffect(EffectType::Entangle));
    EXPECT_FALSE(target->isEntangled());
    EXPECT_TRUE(target->hasEffect(EffectType::ACIncrease));
    EXPECT_EQ(defense + 2, target->getDefense());
    target->removeEffect(link);
    EXPECT_FALSE(target->hasEffect(EffectType::ACIncrease));
    EXPECT_EQ(defense, target->getDefense());
}

TEST_F(C08bTest, a_link_whose_every_child_is_refused_is_refused) {
    auto target = creature();
    target->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::Entangle), DurationType::Permanent);
    size_t before = target->effects().size();
    auto link = std::make_shared<LinkEffectsEffect>(
        std::make_shared<EntangleEffect>(),
        std::make_shared<ACIncreaseEffect>(0, ACBonus::Dodge, kAllDamageTypeFlags));
    target->applyEffect(link, DurationType::Permanent);
    EXPECT_EQ(before, target->effects().size());
}

// Links: a total limit wears down.

TEST_F(C08bTest, a_linked_damage_resistance_wears_its_limit_down_and_ends_the_link) {
    auto target = creature();
    int defense = target->getDefense();
    auto link = std::make_shared<LinkEffectsEffect>(
        std::make_shared<DamageResistanceEffect>(DamageType::Fire, 10, 15),
        std::make_shared<ACIncreaseEffect>(2, ACBonus::Dodge, kAllDamageTypeFlags));
    target->applyEffect(link, DurationType::Permanent);
    hit(*target, 12);
    EXPECT_EQ(98, target->currentHitPoints()); // 10 absorbed, 3 of the limit left
    EXPECT_EQ(defense + 2, target->getDefense());
    hit(*target, 12);
    EXPECT_EQ(89, target->currentHitPoints()); // the last 3 absorbed
    EXPECT_TRUE(target->effects().empty());   // spent: the whole link is gone
    EXPECT_EQ(defense, target->getDefense());
    hit(*target, 12);
    EXPECT_EQ(77, target->currentHitPoints());
}

// Temporary hit points: a pool, not a maximum.

TEST_F(C08bTest, temporary_hit_points_raise_current_but_not_maximum) {
    auto target = creature();
    int saved = target->serializedCurrentHitPoints();
    auto pool = std::make_shared<TemporaryHitPointsEffect>(30);
    target->applyEffect(pool, DurationType::Temporary, 10.0f);
    EXPECT_EQ(100, target->maxHitPoints());
    EXPECT_EQ(130, target->currentHitPoints());
    EXPECT_EQ(30, target->temporaryHitPoints());
    // The pool is not saved.
    EXPECT_EQ(saved, target->serializedCurrentHitPoints());
    // Healing cannot restore more than the maximum plus the pool.
    hit(*target, 20);
    target->heal(50);
    EXPECT_EQ(130, target->currentHitPoints());
    target->removeEffect(pool);
    EXPECT_EQ(100, target->currentHitPoints());
    EXPECT_EQ(0, target->temporaryHitPoints());
}

TEST_F(C08bTest, damage_taken_on_temporary_hit_points_is_real_when_the_pool_goes) {
    auto target = creature();
    auto pool = std::make_shared<TemporaryHitPointsEffect>(30);
    target->applyEffect(pool, DurationType::Temporary, 10.0f);
    hit(*target, 50);
    EXPECT_EQ(80, target->currentHitPoints());
    EXPECT_FALSE(target->isDead());
    target->removeEffect(pool);
    EXPECT_EQ(50, target->currentHitPoints());

    auto other = creature();
    auto big = std::make_shared<TemporaryHitPointsEffect>(30);
    other->applyEffect(big, DurationType::Temporary, 10.0f);
    hit(*other, 110); // 20 left, all of it from the pool
    EXPECT_FALSE(other->isDead());
    other->removeEffect(big);
    EXPECT_TRUE(other->isDead());
}

TEST_F(C08bTest, temporary_hit_points_end_with_their_duration) {
    auto target = creature();
    target->applyEffect(std::make_shared<TemporaryHitPointsEffect>(10), DurationType::Temporary, 1.0f);
    EXPECT_EQ(110, target->currentHitPoints());
    run(*target, 1.2f);
    EXPECT_EQ(100, target->currentHitPoints());
    EXPECT_EQ(0, target->temporaryHitPoints());
}

TEST_F(C08bTest, temporary_force_points_are_a_pool_that_power_costs_can_use) {
    auto jedi = creature();
    jedi->addBaseForcePoints(20);
    int maximum = jedi->maxForcePoints();
    jedi->setCurrentForcePoints(maximum);
    auto pool = std::make_shared<TemporaryForcePointsEffect>(50);
    jedi->applyEffect(pool, DurationType::Permanent);
    EXPECT_EQ(maximum, jedi->maxForcePoints());
    EXPECT_EQ(maximum + 50, jedi->currentForce());
    jedi->drainForcePoints(30);
    EXPECT_EQ(maximum + 20, jedi->currentForce());
    jedi->removeEffect(pool);
    EXPECT_EQ(std::max(0, maximum - 30), jedi->currentForce());
    EXPECT_EQ(0, jedi->temporaryForcePoints());
}

// Force resistance.

TEST_F(C08bTest, force_resistance_takes_the_best_increase_less_decreases) {
    auto target = creature();
    EXPECT_EQ(0, target->getForceResistance());
    target->applyEffect(std::make_shared<ForceResistanceIncreaseEffect>(15), DurationType::Permanent);
    target->applyEffect(std::make_shared<ForceResistanceIncreaseEffect>(25), DurationType::Permanent);
    EXPECT_EQ(25, target->getForceResistance());
    target->applyEffect(std::make_shared<ForceResistanceDecreaseEffect>(5), DurationType::Permanent);
    EXPECT_EQ(20, target->getForceResistance());
    target->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::ForceResistanceDecrease), DurationType::Permanent);
    target->applyEffect(std::make_shared<ForceResistanceDecreaseEffect>(5), DurationType::Permanent);
    EXPECT_EQ(20, target->getForceResistance());
}

TEST_F(C08bTest, resist_force_rolls_the_source_level_against_the_resistance) {
    auto caster = creature();
    auto target = creature(glm::vec3(2.0f, 0.0f, 0.0f));
    auto args = [&]() {
        return std::vector<Variable> {Variable::ofObject(caster->id()), Variable::ofObject(target->id())};
    };
    EXPECT_EQ(0, call("ResistForce", args()).intValue);
    // 40 cannot be reached by d20 + 0: always resisted.
    auto high = std::make_shared<ForceResistanceIncreaseEffect>(40);
    target->applyEffect(high, DurationType::Permanent);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(1, call("ResistForce", args()).intValue);
    }
    target->removeEffect(high);
    // 1 is always reached.
    target->applyEffect(std::make_shared<ForceResistanceIncreaseEffect>(1), DurationType::Permanent);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(0, call("ResistForce", args()).intValue);
    }
    EXPECT_EQ(0, call("ResistForce", {Variable::ofObject(caster->id()), Variable::ofObject(script::kObjectInvalid)}).intValue);
}

// Stat modifiers.

TEST_F(C08bTest, saving_throw_modifiers_apply_and_decreases_respect_immunity) {
    auto target = creature();
    int will = target->getSavingThrow(3, SavingThrowType::All);
    target->applyEffect(std::make_shared<SavingThrowIncreaseEffect>(0, 6, SavingThrowType::All), DurationType::Permanent);
    EXPECT_EQ(will + 6, target->getSavingThrow(3, SavingThrowType::All));
    target->applyEffect(std::make_shared<SavingThrowDecreaseEffect>(3, 2, SavingThrowType::All), DurationType::Permanent);
    EXPECT_EQ(will + 4, target->getSavingThrow(3, SavingThrowType::All));
    target->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::SavingThrowDecrease), DurationType::Permanent);
    target->applyEffect(std::make_shared<SavingThrowDecreaseEffect>(3, 2, SavingThrowType::All), DurationType::Permanent);
    EXPECT_EQ(will + 4, target->getSavingThrow(3, SavingThrowType::All));
    size_t before = target->effects().size();
    target->applyEffect(std::make_shared<SavingThrowIncreaseEffect>(0, 0, SavingThrowType::All), DurationType::Permanent);
    EXPECT_EQ(before, target->effects().size());
}

TEST_F(C08bTest, damage_decrease_is_refused_by_immunity_and_on_plot_creatures) {
    auto target = creature();
    target->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::DamageDecrease), DurationType::Permanent);
    target->applyEffect(std::make_shared<DamageDecreaseEffect>(2, DamageType::Universal), DurationType::Permanent);
    EXPECT_FALSE(target->hasEffect(EffectType::DamageDecrease));
    auto plot = creature();
    plot->setPlotFlag(true);
    plot->applyEffect(std::make_shared<DamageDecreaseEffect>(2, DamageType::Universal), DurationType::Permanent);
    EXPECT_FALSE(plot->hasEffect(EffectType::DamageDecrease));
    auto plain = creature();
    plain->applyEffect(std::make_shared<DamageDecreaseEffect>(2, DamageType::Universal), DurationType::Permanent);
    EXPECT_TRUE(plain->hasEffect(EffectType::DamageDecrease));
}

// Fizzle and resisted.

TEST_F(C08bTest, fizzle_and_resisted_are_instant_visuals) {
    auto caster = creature();
    auto target = creature(glm::vec3(2.0f, 0.0f, 0.0f));
    auto fizzle = call("EffectForceFizzle", {});
    call("ApplyEffectToObject", {Variable::ofInt(0), fizzle, Variable::ofObject(caster->id()), Variable::ofFloat(0.0f)}, caster);
    auto resisted = call("EffectForceResisted", {Variable::ofObject(caster->id())});
    call("ApplyEffectToObject", {Variable::ofInt(0), resisted, Variable::ofObject(target->id()), Variable::ofFloat(0.0f)}, caster);
    EXPECT_FALSE(caster->wasLastForcePowerSuccessful());
    EXPECT_TRUE(caster->effects().empty());
    EXPECT_TRUE(target->effects().empty());
}

// Lightsaber throw.

TEST_F(C08bTest, lightsaber_throw_hits_each_target_as_the_saber_reaches_it) {
    auto thrower = creature();
    auto first = creature(glm::vec3(5.0f, 0.0f, 0.0f));
    auto second = creature(glm::vec3(5.0f, 5.0f, 0.0f));
    auto thrown = std::make_shared<LightsaberThrowEffect>(first, second, nullptr, 0);
    thrower->applyEffect(thrown, DurationType::Temporary, 3.0f);
    EXPECT_TRUE(thrower->isHeldInPose());
    // Path: 5 m out, 5 m across, about 7.07 m back; the first hit comes at
    // 5 / 17.07 of the flight.
    run(*thrower, 0.4f);
    EXPECT_EQ(100, first->currentHitPoints());
    run(*thrower, 0.3f);
    EXPECT_LT(first->currentHitPoints(), 100);
    EXPECT_GE(first->currentHitPoints(), 94); // 1d6 at level 0
    EXPECT_EQ(100, second->currentHitPoints());
    run(*thrower, 0.8f);
    EXPECT_LT(second->currentHitPoints(), 100);
    int firstHp = first->currentHitPoints();
    run(*thrower, 2.0f); // one hit per target only; the effect ends
    EXPECT_EQ(firstHp, first->currentHitPoints());
    EXPECT_FALSE(thrower->isHeldInPose());
}

// Body fuel, Force Body.

TEST_F(C08bTest, body_fuel_refills_force_points_every_round) {
    auto bastila = creature();
    bastila->addBaseForcePoints(40);
    int maximum = bastila->maxForcePoints();
    bastila->setCurrentForcePoints(0);
    bastila->applyEffect(std::make_shared<BodyFuelEffect>(), DurationType::Temporary, 30.0f);
    EXPECT_TRUE(bastila->hasBodyFuel());
    run(*bastila, 2.9f);
    EXPECT_EQ(0, bastila->currentForce());
    run(*bastila, 0.2f);
    EXPECT_EQ(maximum, bastila->currentForce());
}

TEST_F(C08bTest, force_body_splits_power_costs_by_level) {
    auto jedi = creature();
    EXPECT_EQ(20, jedi->forceBodyShare(20));
    auto body = std::make_shared<ForceBodyEffect>(0);
    jedi->applyEffect(body, DurationType::Temporary, 30.0f);
    EXPECT_EQ(10, jedi->forceBodyShare(20));
    jedi->applyEffect(std::make_shared<ForceBodyEffect>(2), DurationType::Temporary, 30.0f);
    EXPECT_EQ(6, jedi->forceBodyShare(20)); // the best level wins: 30 %
    jedi->applyEffect(std::make_shared<ForceBodyEffect>(3), DurationType::Temporary, 30.0f);
    EXPECT_EQ(2, jedi->getForceBodyLevel());
}

// K2 effects.

TEST_F(C08bTest, fury_counts_kills_up_to_six_and_ends_with_the_effect) {
    auto marauder = creature();
    auto fury = std::make_shared<FuryEffect>();
    marauder->applyEffect(fury, DurationType::Temporary, 30.0f);
    EXPECT_TRUE(marauder->isInFury());
    for (int i = 0; i < 8; ++i) {
        auto victim = creature();
        victim->damage(1000, marauder);
        ASSERT_TRUE(victim->isDead());
    }
    EXPECT_EQ(Creature::kMaximumFuryDamageBonus, marauder->furyDamageBonus());
    marauder->removeEffect(fury);
    EXPECT_EQ(0, marauder->furyDamageBonus());
}

TEST_F(C08bTest, faction_modifier_changes_the_faction_until_it_ends) {
    auto target = creature();
    target->setFaction(Faction::Hostile1);
    auto charm = std::make_shared<FactionModifierEffect>(static_cast<int>(Faction::Friendly1));
    target->applyEffect(charm, DurationType::Temporary, 30.0f);
    EXPECT_EQ(Faction::Friendly1, target->faction());
    EXPECT_EQ(Faction::Hostile1, target->serializedFaction());
    target->removeEffect(charm);
    EXPECT_EQ(Faction::Hostile1, target->faction());
}

TEST_F(C08bTest, blind_and_mind_trick_respect_their_immunities) {
    auto target = creature();
    target->applyEffect(std::make_shared<BlindEffect>(), DurationType::Temporary, 5.0f);
    EXPECT_TRUE(target->isBlind());
    auto immune = creature();
    immune->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::Blindness), DurationType::Permanent);
    immune->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::MindSpells), DurationType::Permanent);
    immune->applyEffect(std::make_shared<BlindEffect>(), DurationType::Temporary, 5.0f);
    immune->applyEffect(std::make_shared<MindTrickEffect>(), DurationType::Temporary, 5.0f);
    EXPECT_FALSE(immune->isBlind());
    EXPECT_FALSE(immune->isDistracted());
    target->applyEffect(std::make_shared<MindTrickEffect>(), DurationType::Temporary, 5.0f);
    EXPECT_TRUE(target->isDistracted());
}

TEST_F(C08bTest, crush_holds_the_target_while_it_lasts) {
    auto target = creature();
    target->applyEffect(std::make_shared<CrushEffect>(), DurationType::Temporary, 2.0f);
    EXPECT_TRUE(target->isHeldInPose());
    EXPECT_EQ("forcecrush", target->getHeldPoseAnimation());
    run(*target, 2.1f);
    EXPECT_FALSE(target->isHeldInPose());
}
