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

// Campaign batch C08: combat effects.
// Behaviour notes: design note c08_effects.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../fixtures/scene.h"

#include "reone/game/action/lockobject.h"
#include "reone/game/action/wait.h"
#include "reone/game/effect/abilitydecrease.h"
#include "reone/game/effect/abilityincrease.h"
#include "reone/game/effect/attackincrease.h"
#include "reone/game/effect/cutsceneparalyze.h"
#include "reone/game/effect/cutscenestunned.h"
#include "reone/game/effect/damage.h"
#include "reone/game/effect/damageforcepoints.h"
#include "reone/game/effect/damageincrease.h"
#include "reone/game/effect/damageresistance.h"
#include "reone/game/effect/disguise.h"
#include "reone/game/effect/droidstun.h"
#include "reone/game/effect/entangle.h"
#include "reone/game/effect/forceshield.h"
#include "reone/game/effect/forcepushed.h"
#include "reone/game/effect/forcepushtargeted.h"
#include "reone/game/effect/healforcepoints.h"
#include "reone/game/effect/horrified.h"
#include "reone/game/effect/immunity.h"
#include "reone/game/effect/knockdown.h"
#include "reone/game/effect/linkeffects.h"
#include "reone/game/effect/movementspeeddecrease.h"
#include "reone/game/effect/movementspeedincrease.h"
#include "reone/game/effect/paralyze.h"
#include "reone/game/effect/regenerate.h"
#include "reone/game/effect/skilldecrease.h"
#include "reone/game/effect/skillincrease.h"
#include "reone/game/effect/sleep.h"
#include "reone/game/effect/stunned.h"
#include "reone/game/effect/temporaryforcepoints.h"
#include "reone/game/effect/whirlwind.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/script/routines.h"
#include "reone/resource/2da.h"
#include "reone/scene/collision.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;
using namespace testing;

namespace {

constexpr float kTick = 1.0f / 30.0f;
using Phase = Creature::KnockdownPhase;

/** A game with no module: creatures move over a flat, open plane. */
class C08Test : public Test {
protected:
    void SetUp() override {
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _routines = std::make_unique<Routines>(GameID::KotOR, _game.get(), &_engine.services());
        _routines->init();
        // Enough of appearance.2da for a disguise to swap appearances.
        TwoDA::Builder appearance;
        appearance.columns({"label", "modeltype"});
        for (int row = 0; row < 32; ++row) {
            appearance.row({"row" + std::to_string(row), "B"});
        }
        _appearance = std::shared_ptr<TwoDA>(appearance.build());
        ON_CALL(_engine.resourceModule().twoDas(), get("appearance")).WillByDefault(Return(_appearance));
    }

    std::shared_ptr<Creature> creature(glm::vec3 position = glm::vec3(0.0f)) {
        auto result = _game->newCreature();
        result->setPosition(position);
        result->setMaxHitPoints(100);
        result->setCurrentHitPoints(100);
        return result;
    }

    std::shared_ptr<Effect> pushFrom(const std::shared_ptr<Object> &caster) {
        auto effect = std::make_shared<ForcePushedEffect>();
        effect->setSaveFacingCreator(caster);
        return effect;
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

    TestEngine _engine;
    StubConsole _console;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
    std::shared_ptr<TwoDA> _appearance;
};

/**
 * An area whose walkmesh is walkable for x < meshEdge, with a wall at
 * x = wallX that blocks straight walks crossing it.
 */
class C08AreaTest : public C08Test {
protected:
    void SetUp() override {
        C08Test::SetUp();
        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(_sceneGraph));
        ON_CALL(_sceneGraph, testElevation(_, _)).WillByDefault(Invoke([this](const glm::vec3 &position, Collision &collision) {
            if (position.x >= _meshEdge) {
                return false;
            }
            collision.intersection = glm::vec3(position.x, position.y, 0.5f);
            collision.user = nullptr;
            return true;
        }));
        ON_CALL(_sceneGraph, testWalk(_, _, _, _)).WillByDefault(Invoke([this](const glm::vec3 &origin, const glm::vec3 &dest, const IUser *, Collision &) {
            return (origin.x < _wallX) != (dest.x < _wallX);
        }));
        _area = _game->newArea();
        TestGameModule::setActiveModuleArea(*_game, _area);
    }

    std::shared_ptr<Creature> placed(glm::vec3 position) {
        auto result = creature(position);
        _area->add(result);
        return result;
    }

    NiceMock<MockSceneGraph> _sceneGraph;
    std::shared_ptr<Area> _area;
    float _meshEdge {100.0f};
    float _wallX {100.0f};
};

} // namespace

// Force push: the throw.

TEST_F(C08Test, force_pushed_throws_the_target_away_from_its_creator_over_ticks) {
    auto caster = creature(glm::vec3(0.0f));
    auto target = creature(glm::vec3(1.0f, 0.0f, 0.0f));
    target->applyEffect(pushFrom(caster), DurationType::Instant);

    // Not teleported: the throw starts on the next tick.
    EXPECT_FLOAT_EQ(1.0f, target->position().x);
    EXPECT_NEAR(ForcePushedEffect::kPushDistance, target->knockbackRemaining(), 1e-4f);

    target->update(kTick);
    EXPECT_NEAR(1.0f + Creature::kKnockbackSpeed * kTick, target->position().x, 1e-4f);

    run(*target, 1.0f);
    EXPECT_NEAR(1.0f + ForcePushedEffect::kPushDistance, target->position().x, 1e-3f);
    EXPECT_NEAR(0.0f, target->position().y, 1e-4f);
    EXPECT_FLOAT_EQ(0.0f, target->knockbackRemaining());

    // It faces the caster: (-sin f, cos f) points back at the origin.
    float facing = target->getFacing();
    EXPECT_NEAR(-1.0f, -std::sin(facing), 1e-4f);
}

TEST_F(C08Test, force_pushed_throws_5_m_in_a_fifth_of_a_second) {
    // K2 behaviour: 5 m at 25 m/s.
    auto caster = creature(glm::vec3(0.0f));
    auto target = creature(glm::vec3(0.0f, -2.0f, 0.0f));
    target->applyEffect(pushFrom(caster), DurationType::Instant);
    run(*target, 0.1f);
    EXPECT_GT(target->knockbackRemaining(), 0.0f);
    EXPECT_NEAR(-2.0f - 0.1f * Creature::kKnockbackSpeed, target->position().y, 0.05f);
    run(*target, 0.2f);
    EXPECT_NEAR(-2.0f - ForcePushedEffect::kPushDistance, target->position().y, 1e-3f);
}

TEST_F(C08Test, force_pushed_without_a_creator_goes_straight_backwards) {
    auto target = creature(glm::vec3(0.0f));
    target->setFacing(0.0f); // facing +y
    target->applyEffect(std::make_shared<ForcePushedEffect>(), DurationType::Instant);
    run(*target, 1.0f);
    EXPECT_NEAR(0.0f, target->position().x, 1e-4f);
    EXPECT_NEAR(-ForcePushedEffect::kPushDistance, target->position().y, 1e-3f);
}

TEST_F(C08Test, force_pushed_created_by_the_target_itself_goes_backwards) {
    auto target = creature(glm::vec3(5.0f, 5.0f, 0.0f));
    target->setFacing(0.0f);
    target->applyEffect(pushFrom(target), DurationType::Instant);
    run(*target, 1.0f);
    EXPECT_NEAR(5.0f - ForcePushedEffect::kPushDistance, target->position().y, 1e-3f);
}

TEST_F(C08Test, force_pushed_routine_stamps_the_caller_as_the_source) {
    auto caster = creature(glm::vec3(0.0f));
    auto target = creature(glm::vec3(-1.0f, 0.0f, 0.0f));
    auto value = call("EffectForcePushed", {}, caster);
    auto effect = std::dynamic_pointer_cast<Effect>(value.engineType);
    ASSERT_TRUE(effect);
    target->applyEffect(effect, DurationType::Temporary, 0.1f);
    run(*target, 1.0f);
    EXPECT_NEAR(-1.0f - ForcePushedEffect::kPushDistance, target->position().x, 1e-3f);
}

TEST_F(C08Test, force_pushed_leaves_the_dead_and_the_immune) {
    auto caster = creature(glm::vec3(0.0f));
    auto corpse = creature(glm::vec3(1.0f, 0.0f, 0.0f));
    corpse->damage(1000, nullptr);
    ASSERT_TRUE(corpse->isDead());
    corpse->applyEffect(pushFrom(caster), DurationType::Instant);
    run(*corpse, 1.0f);
    EXPECT_FLOAT_EQ(1.0f, corpse->position().x);
    EXPECT_FALSE(corpse->isKnockedDown());

    auto rancor = creature(glm::vec3(1.0f, 0.0f, 0.0f));
    rancor->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::Knockdown), DurationType::Permanent);
    rancor->applyEffect(pushFrom(caster), DurationType::Temporary, 0.25f);
    run(*rancor, 1.0f);
    EXPECT_FLOAT_EQ(1.0f, rancor->position().x);
    EXPECT_FALSE(rancor->isKnockedDown());
    EXPECT_FALSE(rancor->hasEffect(EffectType::ForcePushed));
}

// Force push: the knockdown.

TEST_F(C08Test, instant_push_falls_then_gets_up_without_lying) {
    auto caster = creature(glm::vec3(0.0f));
    auto target = creature(glm::vec3(1.0f, 0.0f, 0.0f));
    ASSERT_FALSE(target->isMovementRestricted());
    target->applyEffect(pushFrom(caster), DurationType::Instant);
    EXPECT_EQ(Phase::Falling, target->knockdownPhase());
    EXPECT_TRUE(target->isHeldInPose());
    EXPECT_TRUE(target->isMovementRestricted());

    // No model: the fall is 1.0 s and the get-up 1.5 s.
    run(*target, 0.9f);
    EXPECT_EQ(Phase::Falling, target->knockdownPhase());
    run(*target, 0.2f);
    EXPECT_EQ(Phase::GettingUp, target->knockdownPhase());
    EXPECT_TRUE(target->isMovementRestricted());
    run(*target, 1.5f);
    EXPECT_EQ(Phase::None, target->knockdownPhase());
    EXPECT_FALSE(target->isHeldInPose());
    EXPECT_FALSE(target->isMovementRestricted());
}

TEST_F(C08Test, a_long_push_effect_keeps_the_target_down_until_it_ends) {
    auto caster = creature(glm::vec3(0.0f));
    auto target = creature(glm::vec3(1.0f, 0.0f, 0.0f));
    target->applyEffect(pushFrom(caster), DurationType::Temporary, 2.0f);
    run(*target, 1.1f);
    EXPECT_EQ(Phase::Down, target->knockdownPhase());
    run(*target, 0.8f);
    EXPECT_EQ(Phase::Down, target->knockdownPhase());
    run(*target, 0.2f);
    EXPECT_EQ(Phase::GettingUp, target->knockdownPhase());
}

TEST_F(C08Test, a_second_push_while_down_throws_again_without_restarting_the_fall) {
    auto caster = creature(glm::vec3(0.0f));
    auto target = creature(glm::vec3(1.0f, 0.0f, 0.0f));
    target->applyEffect(pushFrom(caster), DurationType::Instant);
    run(*target, 0.6f);
    target->applyEffect(pushFrom(caster), DurationType::Instant);
    EXPECT_NEAR(ForcePushedEffect::kPushDistance, target->knockbackRemaining(), 1e-4f);
    // The fall carries on from where it was: it ends 1.0 s after the first push.
    run(*target, 0.5f);
    EXPECT_EQ(Phase::GettingUp, target->knockdownPhase());
    run(*target, 0.5f);
    EXPECT_NEAR(1.0f + 2.0f * ForcePushedEffect::kPushDistance, target->position().x, 1e-3f);
}

TEST_F(C08Test, knocked_down_target_keeps_its_action_queue_suspended) {
    auto caster = creature(glm::vec3(0.0f));
    auto target = creature(glm::vec3(1.0f, 0.0f, 0.0f));
    target->addAction(_game->newAction<WaitAction>(0.5f));
    target->applyEffect(pushFrom(caster), DurationType::Instant);

    // Down for 2.5 s: longer than the wait, which still has not run.
    run(*target, 2.4f);
    ASSERT_EQ(1u, target->actions().size());
    EXPECT_EQ(ActionType::Wait, target->actions().front()->type());

    // Up again: the queue resumes where it was.
    run(*target, 0.2f);
    EXPECT_EQ(Phase::None, target->knockdownPhase());
    run(*target, 0.6f);
    EXPECT_TRUE(target->actions().empty());
}

TEST_F(C08Test, dying_while_down_ends_the_knockdown) {
    auto caster = creature(glm::vec3(0.0f));
    auto target = creature(glm::vec3(1.0f, 0.0f, 0.0f));
    target->applyEffect(pushFrom(caster), DurationType::Instant);
    run(*target, 0.1f);
    target->damage(1000, nullptr);
    target->update(kTick);
    EXPECT_EQ(Phase::None, target->knockdownPhase());
    EXPECT_FLOAT_EQ(0.0f, target->knockbackRemaining());
}

// Knockdown.

TEST_F(C08Test, knockdown_holds_the_creature_down_for_its_duration_without_moving_it) {
    auto target = creature(glm::vec3(2.0f, 3.0f, 0.0f));
    target->applyEffect(std::make_shared<KnockdownEffect>(), DurationType::Temporary, 3.0f);
    EXPECT_EQ(Phase::Falling, target->knockdownPhase());
    run(*target, 1.1f);
    EXPECT_EQ(Phase::Down, target->knockdownPhase());
    run(*target, 2.0f);
    EXPECT_EQ(Phase::GettingUp, target->knockdownPhase());
    run(*target, 1.5f);
    EXPECT_EQ(Phase::None, target->knockdownPhase());
    EXPECT_FLOAT_EQ(2.0f, target->position().x);
    EXPECT_FLOAT_EQ(3.0f, target->position().y);
}

TEST_F(C08Test, knockdown_respects_immunity) {
    auto target = creature();
    target->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::Knockdown), DurationType::Permanent);
    target->applyEffect(std::make_shared<KnockdownEffect>(), DurationType::Temporary, 3.0f);
    EXPECT_FALSE(target->isKnockedDown());
    EXPECT_FALSE(target->hasEffect(EffectType::Knockdown));
}

// Force push targeted.

TEST_F(C08Test, force_push_targeted_throws_from_the_location_and_knocks_down) {
    auto target = creature(glm::vec3(0.0f, 1.0f, 0.0f));
    auto centre = std::make_shared<Location>(glm::vec3(0.0f), 0.0f);
    target->applyEffect(std::make_shared<ForcePushTargetedEffect>(centre, false), DurationType::Temporary, 0.5f);
    EXPECT_EQ(Phase::Falling, target->knockdownPhase());
    run(*target, 1.0f);
    EXPECT_NEAR(1.0f + ForcePushTargetedEffect::kPushDistance, target->position().y, 1e-3f);
}

TEST_F(C08Test, cutscene_push_ignores_knockdown_immunity) {
    auto target = creature(glm::vec3(1.0f, 0.0f, 0.0f));
    target->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::Knockdown), DurationType::Permanent);
    auto centre = std::make_shared<Location>(glm::vec3(0.0f), 0.0f);
    target->applyEffect(std::make_shared<ForcePushTargetedEffect>(centre, true), DurationType::Instant);
    EXPECT_TRUE(target->isKnockedDown());
    target->applyEffect(std::make_shared<ForcePushTargetedEffect>(centre, false), DurationType::Instant);
    run(*target, 1.0f);
    EXPECT_NEAR(1.0f + ForcePushTargetedEffect::kPushDistance, target->position().x, 1e-3f);
}

// The throw along the walkmesh.

TEST_F(C08AreaTest, push_lands_on_the_walkmesh_height) {
    auto caster = placed(glm::vec3(0.0f));
    auto target = placed(glm::vec3(1.0f, 0.0f, 0.5f));
    target->applyEffect(pushFrom(caster), DurationType::Instant);
    run(*target, 1.0f);
    EXPECT_NEAR(1.0f + ForcePushedEffect::kPushDistance, target->position().x, 1e-3f);
    EXPECT_FLOAT_EQ(0.5f, target->position().z);
}

TEST_F(C08AreaTest, push_stops_at_the_edge_of_the_walkmesh) {
    _meshEdge = 2.5f;
    auto caster = placed(glm::vec3(0.0f));
    auto target = placed(glm::vec3(1.0f, 0.0f, 0.5f));
    target->applyEffect(pushFrom(caster), DurationType::Instant);
    run(*target, 1.0f);
    EXPECT_LT(target->position().x, _meshEdge);
    EXPECT_GT(target->position().x, _meshEdge - Creature::kKnockbackSpeed * kTick - 1e-4f);
    EXPECT_FLOAT_EQ(0.0f, target->knockbackRemaining());
    // Still knocked down: the wall ends the throw, not the fall.
    EXPECT_TRUE(target->isKnockedDown());
}

TEST_F(C08AreaTest, push_never_passes_through_a_wall) {
    _wallX = 2.0f;
    auto caster = placed(glm::vec3(0.0f));
    auto target = placed(glm::vec3(1.0f, 0.0f, 0.5f));
    target->applyEffect(pushFrom(caster), DurationType::Instant);
    run(*target, 1.0f);
    EXPECT_LT(target->position().x, _wallX);
    EXPECT_GT(target->position().x, _wallX - Creature::kKnockbackSpeed * kTick - 1e-4f);
}

TEST_F(C08AreaTest, push_stops_against_another_creature) {
    auto caster = placed(glm::vec3(0.0f));
    auto target = placed(glm::vec3(1.0f, 0.0f, 0.5f));
    auto bystander = placed(glm::vec3(2.5f, 0.0f, 0.5f));
    target->applyEffect(pushFrom(caster), DurationType::Instant);
    run(*target, 1.0f);
    EXPECT_LT(target->position().x, 2.5f);
    EXPECT_FLOAT_EQ(2.5f, bystander->position().x);
}

TEST_F(C08AreaTest, cutscene_push_passes_walls_but_not_the_mesh_edge) {
    _wallX = 2.0f;
    _meshEdge = 3.5f;
    auto target = placed(glm::vec3(1.0f, 0.0f, 0.5f));
    auto centre = std::make_shared<Location>(glm::vec3(0.0f), 0.0f);
    target->applyEffect(std::make_shared<ForcePushTargetedEffect>(centre, true), DurationType::Instant);
    run(*target, 1.0f);
    EXPECT_GT(target->position().x, _wallX);
    EXPECT_LT(target->position().x, _meshEdge);
}

// Held states.

TEST_F(C08Test, held_state_effects_hold_the_pose_and_release_it) {
    struct Case {
        std::shared_ptr<Effect> effect;
        std::string pose;
    };
    std::vector<Case> cases {
        {std::make_shared<WhirlWindEffect>(), "whirlwind"},
        {std::make_shared<ParalyzeEffect>(), "paralyzed"},
        {std::make_shared<CutsceneParalyzeEffect>(), "paralyzed"},
        {std::make_shared<SleepEffect>(), "sleep"},
        {std::make_shared<HorrifiedEffect>(), "horror"},
    };
    for (const auto &c : cases) {
        auto target = creature();
        target->addAction(_game->newAction<WaitAction>(0.1f));
        target->applyEffect(c.effect, DurationType::Permanent);
        EXPECT_EQ(c.pose, target->getHeldPoseAnimation());
        EXPECT_TRUE(target->isHeldInPose());
        EXPECT_TRUE(target->isMovementRestricted());
        run(*target, 0.5f);
        EXPECT_EQ(1u, target->actions().size()) << c.pose;

        target->removeEffect(c.effect);
        EXPECT_EQ("", target->getHeldPoseAnimation());
        EXPECT_FALSE(target->isMovementRestricted());
        run(*target, 0.2f);
        EXPECT_TRUE(target->actions().empty()) << c.pose;
    }
}

TEST_F(C08Test, a_held_conversation_participant_keeps_the_conversation_s_time) {
    // k_ptar_calofade: Calo is horrified for good, then waits and resumes
    // the conversation. The wait runs; a bodily action after it does not.
    auto footlocker = _game->newPlaceable();
    auto calo = creature();
    calo->setIsInConversation(true);
    calo->addAction(_game->newAction<WaitAction>(0.1f));
    calo->addAction(_game->newAction<LockObjectAction>(footlocker));
    calo->applyEffect(std::make_shared<HorrifiedEffect>(), DurationType::Permanent);
    run(*calo, 0.5f);
    ASSERT_EQ(1u, calo->actions().size());
    EXPECT_EQ(ActionType::Lock, calo->actions().front()->type());
    EXPECT_FALSE(footlocker->isLocked());
}

TEST_F(C08Test, a_delayed_command_fires_on_time_while_its_owner_is_held) {
    // k_ptar_calocut: DelayCommand(0.5, death) on a horrified Calo.
    auto footlocker = _game->newPlaceable();
    auto calo = creature();
    calo->applyEffect(std::make_shared<HorrifiedEffect>(), DurationType::Permanent);
    calo->delayAction(_game->newAction<LockObjectAction>(footlocker), 0.2f);
    run(*calo, 0.1f);
    EXPECT_FALSE(footlocker->isLocked());
    run(*calo, 0.2f);
    EXPECT_TRUE(footlocker->isLocked());
    EXPECT_TRUE(calo->actions().empty());
}

TEST_F(C08Test, a_delayed_command_fires_after_its_owner_has_died) {
    // k_pend_ud_cut10: a soldier's death event delays SetDialogPlaceableCamera
    // by 0.5 s; the cut must still happen with the soldier dead
    // (design note cutscene_timing).
    auto footlocker = _game->newPlaceable();
    auto soldier = creature();
    soldier->delayAction(_game->newAction<LockObjectAction>(footlocker), 0.5f);
    TestGameModule::setDead(*soldier, true);
    run(*soldier, 0.4f);
    EXPECT_FALSE(footlocker->isLocked());
    run(*soldier, 0.2f);
    EXPECT_TRUE(footlocker->isLocked());
    EXPECT_TRUE(soldier->actions().empty());
}

TEST_F(C08Test, a_dead_owner_s_queued_actions_are_still_dropped) {
    auto footlocker = _game->newPlaceable();
    auto soldier = creature();
    soldier->addAction(_game->newAction<LockObjectAction>(footlocker));
    TestGameModule::setDead(*soldier, true);
    run(*soldier, 0.2f);
    EXPECT_FALSE(footlocker->isLocked());
    EXPECT_TRUE(soldier->actions().empty());
}

TEST_F(C08Test, held_states_expire_with_their_duration) {
    auto target = creature();
    target->applyEffect(std::make_shared<WhirlWindEffect>(), DurationType::Temporary, 1.0f);
    run(*target, 0.9f);
    EXPECT_TRUE(target->isHeldInPose());
    run(*target, 0.2f);
    EXPECT_FALSE(target->isHeldInPose());
}

TEST_F(C08Test, paralyze_sleep_and_horror_respect_immunity_but_cutscene_forms_do_not) {
    auto target = creature();
    target->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::Paralysis), DurationType::Permanent);
    target->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::Sleep), DurationType::Permanent);
    target->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::Fear), DurationType::Permanent);
    target->applyEffect(std::make_shared<ParalyzeEffect>(), DurationType::Permanent);
    target->applyEffect(std::make_shared<SleepEffect>(), DurationType::Permanent);
    target->applyEffect(std::make_shared<HorrifiedEffect>(), DurationType::Permanent);
    EXPECT_FALSE(target->isHeldInPose());
    target->applyEffect(std::make_shared<CutsceneParalyzeEffect>(), DurationType::Permanent);
    EXPECT_EQ("paralyzed", target->getHeldPoseAnimation());
}

TEST_F(C08Test, droid_stun_holds_droids_only) {
    auto human = creature();
    human->setRacialType(RacialType::Human);
    human->applyEffect(std::make_shared<DroidStunEffect>(), DurationType::Permanent);
    EXPECT_FALSE(human->hasEffect(EffectType::DroidStun));
    EXPECT_FALSE(human->isHeldInPose());

    auto droid = creature();
    droid->setRacialType(RacialType::Droid);
    droid->applyEffect(std::make_shared<DroidStunEffect>(), DurationType::Temporary, 1.0f);
    EXPECT_EQ("disabled", droid->getHeldPoseAnimation());
    EXPECT_TRUE(droid->isMovementRestricted());
    run(*droid, 1.1f);
    EXPECT_FALSE(droid->isHeldInPose());
}

TEST_F(C08Test, cutscene_stun_stops_actions) {
    auto target = creature();
    auto stun = std::make_shared<CutsceneStunnedEffect>();
    target->applyEffect(stun, DurationType::Permanent);
    EXPECT_TRUE(target->isMovementRestricted());
    target->removeEffect(stun);
    EXPECT_FALSE(target->isMovementRestricted());
}

TEST_F(C08Test, knockdown_takes_the_pose_over_a_held_state) {
    auto target = creature();
    target->applyEffect(std::make_shared<ParalyzeEffect>(), DurationType::Permanent);
    target->applyEffect(std::make_shared<KnockdownEffect>(), DurationType::Instant);
    EXPECT_TRUE(target->isKnockedDown());
    run(*target, 3.0f);
    EXPECT_FALSE(target->isKnockedDown());
    EXPECT_TRUE(target->isHeldInPose());
}

// Force points.

TEST_F(C08Test, heal_and_damage_force_points_stay_within_zero_and_the_maximum) {
    auto jedi = creature();
    // No classes here: the pool is all temporary points.
    jedi->applyEffect(std::make_shared<TemporaryForcePointsEffect>(40), DurationType::Permanent);
    // The pool is not part of the maximum; the current value may use it.
    EXPECT_EQ(0, jedi->maxForcePoints());
    int maximum = jedi->maxForcePoints() + jedi->temporaryForcePoints();
    ASSERT_EQ(40, maximum);
    jedi->setCurrentForcePoints(10);
    jedi->applyEffect(std::make_shared<HealForcePointsEffect>(5), DurationType::Instant);
    EXPECT_EQ(15, jedi->currentForce());
    jedi->applyEffect(std::make_shared<HealForcePointsEffect>(500), DurationType::Instant);
    EXPECT_EQ(maximum, jedi->currentForce());
    jedi->applyEffect(std::make_shared<DamageForcePointsEffect>(maximum - 3), DurationType::Instant);
    EXPECT_EQ(3, jedi->currentForce());
    jedi->applyEffect(std::make_shared<DamageForcePointsEffect>(100), DurationType::Instant);
    EXPECT_EQ(0, jedi->currentForce());
}

TEST_F(C08Test, temporary_force_points_raise_current_but_not_maximum_while_applied) {
    auto boss = creature();
    boss->addBaseForcePoints(20);
    int maximum = boss->maxForcePoints();
    int current = boss->currentForce();
    // k_def_buff links it with ability increases.
    auto buff = std::make_shared<LinkEffectsEffect>(
        std::make_shared<AbilityIncreaseEffect>(Ability::Wisdom, 4),
        std::make_shared<TemporaryForcePointsEffect>(50));
    boss->applyEffect(buff, DurationType::Permanent);
    EXPECT_EQ(maximum, boss->maxForcePoints());
    EXPECT_EQ(50, boss->temporaryForcePoints());
    EXPECT_EQ(current + 50, boss->currentForce());
    boss->removeEffect(buff);
    EXPECT_EQ(0, boss->temporaryForcePoints());
    EXPECT_EQ(maximum, boss->maxForcePoints());
    EXPECT_EQ(current, boss->currentForce());
}

// Disguise.

TEST_F(C08Test, disguise_shows_another_appearance_until_removed) {
    auto pc = creature();
    pc->setAppearance(6);
    auto disguise = std::make_shared<DisguiseEffect>(28);
    pc->applyEffect(disguise, DurationType::Permanent);
    EXPECT_EQ(28, pc->appearance());
    pc->removeEffect(disguise);
    EXPECT_EQ(6, pc->appearance());
}

// Regenerate.

TEST_F(C08Test, regenerate_heals_every_interval_up_to_the_maximum) {
    auto canderous = creature();
    canderous->setCurrentHitPoints(50);
    auto stims = std::make_shared<LinkEffectsEffect>(
        std::make_shared<RegenerateEffect>(5, 1.0f),
        std::make_shared<AbilityIncreaseEffect>(Ability::Constitution, 2));
    canderous->applyEffect(stims, DurationType::Temporary, 42.0f);
    run(*canderous, 0.9f);
    EXPECT_EQ(50, canderous->currentHitPoints());
    run(*canderous, 0.2f);
    EXPECT_EQ(55, canderous->currentHitPoints());
    run(*canderous, 2.0f);
    EXPECT_EQ(65, canderous->currentHitPoints());
    run(*canderous, 20.0f);
    EXPECT_EQ(100, canderous->currentHitPoints());
}

// Movement speed.

TEST_F(C08Test, movement_speed_effects_follow_the_nwscript_rules) {
    auto runner = creature();
    EXPECT_FLOAT_EQ(1.0f, runner->getMovementSpeedFactor());
    auto speed = std::make_shared<MovementSpeedIncreaseEffect>(99);
    runner->applyEffect(speed, DurationType::Permanent);
    EXPECT_FLOAT_EQ(1.99f, runner->getMovementSpeedFactor());
    runner->applyEffect(std::make_shared<MovementSpeedIncreaseEffect>(150), DurationType::Permanent);
    EXPECT_FLOAT_EQ(1.99f, runner->getMovementSpeedFactor());
    runner->removeEffect(speed);
    EXPECT_FLOAT_EQ(1.5f, runner->getMovementSpeedFactor());

    auto slowed = creature();
    slowed->applyEffect(std::make_shared<MovementSpeedDecreaseEffect>(50), DurationType::Permanent);
    EXPECT_FLOAT_EQ(0.5f, slowed->getMovementSpeedFactor());
    slowed->applyEffect(std::make_shared<MovementSpeedDecreaseEffect>(100), DurationType::Permanent);
    EXPECT_FLOAT_EQ(0.5f, slowed->getMovementSpeedFactor());
    slowed->applyEffect(std::make_shared<MovementSpeedDecreaseEffect>(-20), DurationType::Permanent);
    EXPECT_FLOAT_EQ(1.2f * 0.5f, slowed->getMovementSpeedFactor());
}

// Skills.

TEST_F(C08Test, skill_increase_and_decrease_change_the_skill_value) {
    auto droid = creature();
    int base = droid->getSkillValue(SkillType::Persuade);
    ASSERT_GE(base, 0);
    auto increase = std::make_shared<SkillIncreaseEffect>(SkillType::Persuade, 3);
    droid->applyEffect(increase, DurationType::Permanent);
    EXPECT_EQ(base + 3, droid->getSkillValue(SkillType::Persuade));
    EXPECT_EQ(droid->getSkillValue(SkillType::Repair), droid->getSkillValue(SkillType::Repair));
    droid->applyEffect(std::make_shared<SkillDecreaseEffect>(SkillType::Persuade, 50), DurationType::Permanent);
    EXPECT_EQ(0, droid->getSkillValue(SkillType::Persuade));
}

// Entangle.

TEST_F(C08Test, entangle_costs_two_attack_and_four_defense) {
    auto target = creature();
    int attack = target->getAttackBonus(false);
    int defense = target->getDefense();
    auto entangle = std::make_shared<EntangleEffect>();
    target->applyEffect(entangle, DurationType::Temporary, 6.0f);
    EXPECT_TRUE(target->isEntangled());
    EXPECT_EQ(attack - 2, target->getAttackBonus(false));
    EXPECT_EQ(defense - 4, target->getDefense());
    // Still able to act, unlike the held states.
    EXPECT_FALSE(target->isHeldInPose());
    target->removeEffect(entangle);
    EXPECT_EQ(attack, target->getAttackBonus(false));
}

TEST_F(C08Test, entangle_respects_immunity) {
    auto target = creature();
    target->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::Entangle), DurationType::Permanent);
    target->applyEffect(std::make_shared<EntangleEffect>(), DurationType::Temporary, 6.0f);
    EXPECT_FALSE(target->isEntangled());
}

TEST_F(C08AreaTest, entangled_creature_cannot_walk) {
    auto target = placed(glm::vec3(0.0f, 0.0f, 0.5f));
    target->applyEffect(std::make_shared<EntangleEffect>(), DurationType::Permanent);
    EXPECT_FALSE(_area->moveCreature(target, glm::vec2(1.0f, 0.0f), true, 0.1f));
    EXPECT_FLOAT_EQ(0.0f, target->position().x);
}

// Force shields.

class C08ShieldTest : public C08Test {
protected:
    void SetUp() override {
        C08Test::SetUp();
        TwoDA::Builder builder;
        builder.columns({"label", "visualeffectdef", "damageflags", "vulnerflags", "resistance", "amount",
                         "appearance_01", "visualeffect_01"});
        builder.row({"SHIELD_ENERGY", "-1", "6208", "2048", "300", "20", "", ""});
        builder.row({"SHIELD_MANDALORIAN_MELEE", "-1", "2055", "2048", "300", "20", "", ""});
        builder.row({"SHIELD_CAPPED", "-1", "6208", "2048", "4", "20", "", ""});
        _shields = std::shared_ptr<TwoDA>(builder.build());
        ON_CALL(_engine.resourceModule().twoDas(), get("forceshields")).WillByDefault(Return(_shields));
    }

    int hit(Creature &target, int amount, DamageType type) {
        int before = target.currentHitPoints();
        target.applyEffect(std::make_shared<DamageEffect>(amount, type, DamagePower::Normal), DurationType::Instant);
        return before - target.currentHitPoints();
    }

    std::shared_ptr<TwoDA> _shields;
};

TEST_F(C08ShieldTest, shield_stops_whole_hits_until_its_pool_runs_out) {
    // K2 behaviour: resistance 300 is a per-hit cap, in effect the whole hit.
    auto target = creature();
    auto shield = std::make_shared<ForceShieldEffect>(0, _engine.services());
    target->applyEffect(shield, DurationType::Temporary, 200.0f);
    ASSERT_TRUE(target->hasEffect(EffectType::ForceShield));

    EXPECT_EQ(0, hit(*target, 12, DamageType::Blaster));
    EXPECT_EQ(8, shield->remaining(*target));
    // Not a shielded type: straight through.
    EXPECT_EQ(5, hit(*target, 5, DamageType::Slashing));
    EXPECT_EQ(8, shield->remaining(*target));
    // The last hit is stopped in full and the shield goes.
    EXPECT_EQ(0, hit(*target, 10, DamageType::Blaster));
    EXPECT_FALSE(target->hasEffect(EffectType::ForceShield));
    EXPECT_EQ(10, hit(*target, 10, DamageType::Blaster));
}

TEST_F(C08ShieldTest, resistance_caps_what_one_hit_loses) {
    auto target = creature();
    auto shield = std::make_shared<ForceShieldEffect>(2, _engine.services());
    target->applyEffect(shield, DurationType::Temporary, 200.0f);
    EXPECT_EQ(6, hit(*target, 10, DamageType::Blaster));
    EXPECT_EQ(16, shield->remaining(*target));
}

TEST_F(C08ShieldTest, ion_wears_a_shield_down_twice_as_fast) {
    auto target = creature();
    auto shield = std::make_shared<ForceShieldEffect>(1, _engine.services());
    target->applyEffect(shield, DurationType::Temporary, 200.0f);
    EXPECT_EQ(0, hit(*target, 5, DamageType::Ion));
    EXPECT_EQ(10, shield->remaining(*target));
    EXPECT_EQ(0, hit(*target, 4, DamageType::Ion));
    EXPECT_EQ(2, shield->remaining(*target));
    // Less than 1 left: the shield goes.
    EXPECT_EQ(0, hit(*target, 1, DamageType::Ion));
    EXPECT_FALSE(target->hasEffect(EffectType::ForceShield));
}

TEST_F(C08ShieldTest, an_unknown_row_does_not_apply) {
    auto target = creature();
    target->applyEffect(std::make_shared<ForceShieldEffect>(40, _engine.services()), DurationType::Temporary, 200.0f);
    EXPECT_FALSE(target->hasEffect(EffectType::ForceShield));
}

// Effect routines.

TEST_F(C08Test, remove_effect_removes_the_value_get_first_effect_returned) {
    auto pc = creature();
    pc->setAppearance(6);
    pc->applyEffect(std::make_shared<DisguiseEffect>(28), DurationType::Permanent);
    auto first = call("GetFirstEffect", {Variable::ofObject(pc->id())});
    EXPECT_EQ(static_cast<int>(EffectType::Disguise), call("GetEffectType", {first}).intValue);
    call("RemoveEffect", {Variable::ofObject(pc->id()), first});
    EXPECT_TRUE(pc->effects().empty());
    EXPECT_EQ(6, pc->appearance());
}

TEST_F(C08Test, get_effect_creator_names_the_script_caller) {
    auto caster = creature();
    auto value = call("EffectForcePushed", {}, caster);
    EXPECT_EQ(caster->id(), call("GetEffectCreator", {value}).objectId);
    auto orphan = call("EffectForcePushed", {});
    EXPECT_EQ(kObjectInvalid, call("GetEffectCreator", {orphan}).objectId);
}

TEST_F(C08Test, get_is_immune_reads_immunity_effects) {
    auto droid = creature();
    droid->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::Poison), DurationType::Permanent);
    EXPECT_EQ(1, call("GetIsImmune", {Variable::ofObject(droid->id()), Variable::ofInt(2)}).intValue);
    EXPECT_EQ(0, call("GetIsImmune", {Variable::ofObject(droid->id()), Variable::ofInt(12)}).intValue);
}

TEST_F(C08Test, surrender_retain_buffs_keeps_own_effects_but_the_plain_form_strips_all) {
    // K2 behaviour: the plain form strips every temporary and permanent
    // effect; the retain form keeps what the creature made on itself.
    auto enemy = creature();
    auto malak = creature();
    malak->setFaction(Faction::Hostile1);
    auto own = std::make_shared<AbilityIncreaseEffect>(Ability::Strength, 2);
    own->setSaveFacingCreator(malak);
    auto stun = std::make_shared<StunnedEffect>();
    stun->setSaveFacingCreator(enemy);
    malak->applyEffect(own, DurationType::Permanent);
    malak->applyEffect(stun, DurationType::Permanent);
    malak->addAction(_game->newAction<WaitAction>(5.0f));

    call("SurrenderRetainBuffs", {}, malak);
    EXPECT_EQ(Faction::Invalid, malak->faction());
    EXPECT_TRUE(malak->actions().empty());
    ASSERT_EQ(1u, malak->effects().size());
    EXPECT_EQ(EffectType::AbilityIncrease, malak->effects().front().type());

    malak->setFaction(Faction::Hostile1);
    malak->applyEffect(stun, DurationType::Permanent);
    call("SurrenderToEnemies", {}, malak);
    EXPECT_EQ(Faction::Invalid, malak->faction());
    EXPECT_TRUE(malak->effects().empty());
}

TEST_F(C08Test, lock_object_locks_a_placeable) {
    auto footlocker = _game->newPlaceable();
    auto thief = creature();
    thief->addAction(_game->newAction<LockObjectAction>(footlocker));
    thief->update(kTick);
    EXPECT_TRUE(footlocker->isLocked());
}

// Linked modifiers count like plain ones (k_hen_canduser, k_def_buff).

TEST_F(C08Test, linked_ability_and_attack_increases_apply) {
    auto canderous = creature();
    int strength = canderous->getEffectiveAbilityScore(Ability::Strength);
    int attack = canderous->getAttackBonus(false);
    auto stims = std::make_shared<LinkEffectsEffect>(
        std::make_shared<AbilityIncreaseEffect>(Ability::Strength, 4),
        std::make_shared<AttackIncreaseEffect>(2, AttackBonus::Misc));
    canderous->applyEffect(stims, DurationType::Temporary, 42.0f);
    EXPECT_EQ(strength + 4, canderous->getEffectiveAbilityScore(Ability::Strength));
    EXPECT_GE(canderous->getAttackBonus(false), attack + 2);
    canderous->removeEffect(stims);
    EXPECT_EQ(strength, canderous->getEffectiveAbilityScore(Ability::Strength));
    EXPECT_EQ(attack, canderous->getAttackBonus(false));
}

TEST_F(C08Test, plain_ability_decrease_and_attack_increase_apply) {
    auto target = creature();
    int dexterity = target->getEffectiveAbilityScore(Ability::Dexterity);
    int attack = target->getAttackBonus(false);
    target->applyEffect(std::make_shared<AbilityDecreaseEffect>(Ability::Dexterity, 2), DurationType::Permanent);
    target->applyEffect(std::make_shared<AttackIncreaseEffect>(3, AttackBonus::Misc), DurationType::Permanent);
    EXPECT_EQ(dexterity - 2, target->getEffectiveAbilityScore(Ability::Dexterity));
    EXPECT_GE(target->getAttackBonus(false), attack + 3 - 1);
}

TEST_F(C08Test, linked_damage_resistance_reduces_damage) {
    auto target = creature();
    auto link = std::make_shared<LinkEffectsEffect>(
        std::make_shared<DamageResistanceEffect>(DamageType::Fire, 5, 0),
        std::make_shared<ImmunityEffect>(ImmunityType::Poison));
    target->applyEffect(link, DurationType::Permanent);
    target->applyEffect(std::make_shared<DamageEffect>(12, DamageType::Fire, DamagePower::Normal), DurationType::Instant);
    EXPECT_EQ(93, target->currentHitPoints());
    EXPECT_TRUE(target->hasEffectImmunity(ImmunityType::Poison, nullptr));
    EXPECT_EQ(1, call("GetIsImmune", {Variable::ofObject(target->id()), Variable::ofInt(2)}).intValue);
}

TEST_F(C08Test, linked_damage_increase_adds_to_the_attack_damage) {
    auto canderous = creature();
    DamagePacket before(DamagePower::Normal);
    before.add(1, DamageType::Bludgeoning);
    before.setDamageFlags(static_cast<int>(DamageType::Bludgeoning));
    canderous->addPhysicalDamageModifiers(before, nullptr, nullptr, false, 1);

    canderous->applyEffect(std::make_shared<LinkEffectsEffect>(
                               std::make_shared<DamageIncreaseEffect>(2, DamageType::Universal),
                               std::make_shared<AbilityIncreaseEffect>(Ability::Constitution, 2)),
                           DurationType::Temporary, 42.0f);
    DamagePacket after(DamagePower::Normal);
    after.add(1, DamageType::Bludgeoning);
    after.setDamageFlags(static_cast<int>(DamageType::Bludgeoning));
    canderous->addPhysicalDamageModifiers(after, nullptr, nullptr, false, 1);
    EXPECT_EQ(before.total() + 2, after.total());
}

// GetIsLinkImmune (autoplay D1: the grenade scripts test it).

TEST_F(C08Test, link_immune_when_any_linked_effect_is_blocked) {
    auto target = creature();
    target->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::Entangle), DurationType::Permanent);
    auto entangle = std::make_shared<EntangleEffect>();
    auto slowed = std::make_shared<MovementSpeedDecreaseEffect>(50);
    auto link = std::make_shared<LinkEffectsEffect>(slowed, entangle);
    EXPECT_EQ(1, call("GetIsLinkImmune", {Variable::ofObject(target->id()), Variable::ofEffect(link)}).intValue);
    EXPECT_EQ(0, call("GetIsLinkImmune", {Variable::ofObject(target->id()), Variable::ofEffect(slowed)}).intValue);
}

TEST_F(C08Test, link_not_immune_without_a_matching_immunity) {
    auto target = creature();
    target->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::Poison), DurationType::Permanent);
    auto link = std::make_shared<LinkEffectsEffect>(std::make_shared<ParalyzeEffect>(), std::make_shared<SleepEffect>());
    EXPECT_EQ(0, call("GetIsLinkImmune", {Variable::ofObject(target->id()), Variable::ofEffect(link)}).intValue);
    target->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::Sleep), DurationType::Permanent);
    EXPECT_EQ(1, call("GetIsLinkImmune", {Variable::ofObject(target->id()), Variable::ofEffect(link)}).intValue);
}
