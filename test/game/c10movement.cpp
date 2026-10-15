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

// C10 movement and pathing: deterministic unstuck, local avoidance, the
// give-up rule, door barriers, party following and the friendly-collision
// option. See design note c10_movement.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"

#include "reone/game/action/attackobject.h"
#include "reone/game/action/follow.h"
#include "reone/game/action/followleader.h"
#include "reone/game/action/movetolocation.h"
#include "reone/game/action/movetopoint.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/party.h"
#include "reone/game/pathfinder.h"
#include "reone/graphics/walkmesh.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/game/object/item.h"
#include "reone/scene/collision.h"
#include "reone/system/randomutil.h"

using namespace reone;
using namespace reone::game;
using namespace testing;

namespace {

constexpr float kTick = 1.0f / 30.0f;

// A straight corridor, x in [-20, 20] and y in [-2, 2], as one room.
void loadCorridor(Pathfinder &pf) {
    graphics::Walkmesh walkmesh;
    walkmesh.vertices = {
        glm::vec3(-20.0f, -2.0f, 0.0f),
        glm::vec3(20.0f, -2.0f, 0.0f),
        glm::vec3(20.0f, 2.0f, 0.0f),
        glm::vec3(-20.0f, 2.0f, 0.0f)};
    walkmesh.faces = {{0, 1, 2}, {0, 2, 3}};
    walkmesh.materials = {0, 0};
    std::set<uint32_t> walkable {0};
    uniwalkLoadRoom(pf.uni, walkmesh, walkable);
    uniwalkFinalize(pf.uni);
    pf.paths.resize(64);
}

std::shared_ptr<resource::TwoDA> creatureSpeeds() {
    resource::TwoDA::Builder builder;
    builder.columns({"label", "2daname", "walkrate", "runrate"});
    builder.row({"PC_Movement", "PLAYER", "1.75", "5.40"});
    return std::shared_ptr<resource::TwoDA>(builder.build());
}

struct MovementHarness {
    TestEngine engine;
    NiceMock<scene::MockSceneGraph> sceneGraph;
    StubConsole console;
    std::unique_ptr<Game> game;
    std::shared_ptr<Area> area;
    std::shared_ptr<Creature> pc;

    explicit MovementHarness(bool corridor = true) {
        engine.init();
        ON_CALL(engine.sceneModule().graphs(), get(_))
            .WillByDefault(ReturnRef(sceneGraph));
        ON_CALL(sceneGraph, testElevation(_, _))
            .WillByDefault(Invoke([](const glm::vec3 &position, scene::Collision &collision) {
                // Ground exists only over the corridor.
                if (glm::abs(position.x) > 20.0f || glm::abs(position.y) > 2.0f) {
                    return false;
                }
                collision.intersection = position;
                collision.intersection.z = 0.0f;
                collision.material = 0;
                collision.user = nullptr;
                return true;
            }));
        ON_CALL(engine.resourceModule().twoDas(), get("creaturespeed"))
            .WillByDefault(Return(creatureSpeeds()));
        game = std::make_unique<Game>(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
        area = game->newArea();
        TestGameModule::setActiveModuleArea(*game, area);
        if (corridor) {
            loadCorridor(area->pathfinder());
        }
        pc = spawn(glm::vec3(0.0f));
        game->party().addMember(kNpcPlayer, pc);
        game->party().setPlayer(pc);
    }

    std::shared_ptr<Creature> spawn(const glm::vec3 &position) {
        auto creature = game->newCreature();
        creature->setWalkRate(0);
        creature->setPosition(position);
        area->add(creature);
        return creature;
    }
};

// --- 1. Deterministic unstuck ------------------------------------------------

glm::vec3 steerWithoutProgress(MovementHarness &harness, const glm::vec3 &position, const glm::vec3 &next) {
    harness.pc->setPosition(position);
    const Uniwalk &uni = harness.area->pathfinder().uni;
    harness.pc->computeSteeringForce(uni, next, kTick);
    // The creature did not move since the previous tick.
    return harness.pc->computeSteeringForce(uni, next, kTick);
}

TEST(C10Unstuck, side_step_goes_to_the_open_side_of_the_walkmesh_and_stays_planar) {
    {
        MovementHarness harness;
        // Near the +y wall, heading +x: the open side is -y (the right).
        glm::vec3 force = steerWithoutProgress(harness, glm::vec3(0.0f, 1.5f, 0.0f), glm::vec3(5.0f, 1.5f, 0.0f));
        EXPECT_LT(force.y, -0.5f);
        EXPECT_FLOAT_EQ(0.0f, force.z);
    }
    {
        MovementHarness harness;
        // Near the -y wall: the open side is +y (the left).
        glm::vec3 force = steerWithoutProgress(harness, glm::vec3(0.0f, -1.5f, 0.0f), glm::vec3(5.0f, -1.5f, 0.0f));
        EXPECT_GT(force.y, 0.5f);
        EXPECT_FLOAT_EQ(0.0f, force.z);
    }
}

TEST(C10Unstuck, side_step_avoids_a_creature_on_the_open_side) {
    MovementHarness harness;
    // Mid-corridor both sides are on the walkmesh; a creature stands on the
    // right, so the side-step goes left.
    harness.spawn(glm::vec3(0.0f, -1.0f, 0.0f));
    glm::vec3 force = steerWithoutProgress(harness, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(5.0f, 0.0f, 0.0f));
    EXPECT_GT(force.y, 0.5f);
}

TEST(C10Unstuck, side_step_is_identical_across_runs_and_draws_no_gameplay_randomness) {
    glm::vec3 first;
    glm::vec3 second;
    uint64_t drawsFirst = 0;
    uint64_t drawsSecond = 0;
    {
        MovementHarness harness;
        uint64_t before = gameplayRandom().draws();
        first = steerWithoutProgress(harness, glm::vec3(0.0f, 0.3f, 0.0f), glm::vec3(5.0f, 0.3f, 0.0f));
        drawsFirst = gameplayRandom().draws() - before;
    }
    {
        MovementHarness harness;
        uint64_t before = gameplayRandom().draws();
        second = steerWithoutProgress(harness, glm::vec3(0.0f, 0.3f, 0.0f), glm::vec3(5.0f, 0.3f, 0.0f));
        drawsSecond = gameplayRandom().draws() - before;
    }
    EXPECT_EQ(first, second);
    EXPECT_EQ(0u, drawsFirst);
    EXPECT_EQ(0u, drawsSecond);
}

TEST(C10Unstuck, progress_means_no_side_step) {
    MovementHarness harness;
    const Uniwalk &uni = harness.area->pathfinder().uni;
    harness.pc->setPosition(glm::vec3(0.0f, 1.5f, 0.0f));
    harness.pc->computeSteeringForce(uni, glm::vec3(5.0f, 1.5f, 0.0f), kTick);
    harness.pc->setPosition(glm::vec3(0.1f, 1.5f, 0.0f));
    glm::vec3 force = harness.pc->computeSteeringForce(uni, glm::vec3(5.0f, 1.5f, 0.0f), kTick);
    // Only the desired direction and the wall keep-out remain.
    EXPECT_GT(force.x, 0.9f);
    EXPECT_GT(force.y, -0.5f);
}

// --- 2. Creature-aware local avoidance ----------------------------------------

TEST(C10Avoidance, creature_ahead_is_passed_on_the_side_away_from_it) {
    MovementHarness harness;
    harness.pc->setPosition(glm::vec3(-10.0f, 0.0f, 0.0f));
    const glm::vec3 forward(1.0f, 0.0f, 0.0f);

    auto blocker = harness.spawn(glm::vec3(-8.5f, 0.3f, 0.0f));
    // The blocker is a little to the left: steer right (-y).
    EXPECT_LT(harness.pc->computeCreatureAvoidance(forward).y, 0.0f);

    blocker->setPosition(glm::vec3(-8.5f, -0.3f, 0.0f));
    EXPECT_GT(harness.pc->computeCreatureAvoidance(forward).y, 0.0f);

    // Dead ahead: keep right.
    blocker->setPosition(glm::vec3(-8.5f, 0.0f, 0.0f));
    EXPECT_LT(harness.pc->computeCreatureAvoidance(forward).y, 0.0f);
}

TEST(C10Avoidance, creatures_behind_beside_or_far_ahead_are_ignored) {
    MovementHarness harness;
    harness.pc->setPosition(glm::vec3(-10.0f, 0.0f, 0.0f));
    const glm::vec3 forward(1.0f, 0.0f, 0.0f);
    harness.spawn(glm::vec3(-11.0f, 0.0f, 0.0f));  // behind
    harness.spawn(glm::vec3(-9.0f, 1.8f, 0.0f));   // beside, clear of the route
    harness.spawn(glm::vec3(-3.0f, 0.0f, 0.0f));   // beyond the look-ahead
    EXPECT_EQ(glm::vec3(0.0f), harness.pc->computeCreatureAvoidance(forward));
}

TEST(C10Avoidance, a_dead_creature_is_not_an_obstacle) {
    MovementHarness harness;
    harness.pc->setPosition(glm::vec3(-10.0f, 0.0f, 0.0f));
    auto body = harness.spawn(glm::vec3(-8.5f, 0.2f, 0.0f));
    body->damage(100000, nullptr);
    ASSERT_TRUE(body->isDead());
    EXPECT_EQ(glm::vec3(0.0f), harness.pc->computeCreatureAvoidance(glm::vec3(1.0f, 0.0f, 0.0f)));
}

TEST(C10Avoidance, result_does_not_depend_on_the_order_creatures_were_added) {
    // The same two blockers, spawned in opposite orders so the area lists
    // them differently; ids are the same in both runs.
    glm::vec3 forces[2];
    for (int run = 0; run < 2; ++run) {
        MovementHarness harness;
        harness.pc->setPosition(glm::vec3(-10.0f, 0.0f, 0.0f));
        auto a = harness.game->newCreature();
        auto b = harness.game->newCreature();
        a->setPosition(glm::vec3(-8.6f, 0.25f, 0.0f));
        b->setPosition(glm::vec3(-8.0f, -0.4f, 0.0f));
        if (run == 0) {
            harness.area->add(a);
            harness.area->add(b);
        } else {
            harness.area->add(b);
            harness.area->add(a);
        }
        forces[run] = harness.pc->computeCreatureAvoidance(glm::vec3(1.0f, 0.0f, 0.0f));
    }
    EXPECT_NE(glm::vec3(0.0f), forces[0]);
    EXPECT_EQ(forces[0], forces[1]);
}

TEST(C10Avoidance, walking_down_a_corridor_goes_around_a_standing_creature) {
    MovementHarness harness;
    harness.pc->setPosition(glm::vec3(-10.0f, 0.0f, 0.0f));
    auto blocker = harness.spawn(glm::vec3(-5.0f, 0.0f, 0.0f));
    const glm::vec3 dest(0.0f, 0.0f, 0.0f);
    bool reached = false;
    float minClearance = 100.0f;
    for (int tick = 0; tick < 30 * 15 && !reached; ++tick) {
        reached = harness.pc->navigateTo(dest, false, 0.5f, kTick);
        minClearance = std::min(minClearance, glm::distance(glm::vec2(harness.pc->position()), glm::vec2(blocker->position())));
    }
    EXPECT_TRUE(reached);
    EXPECT_FALSE(harness.pc->navigationFailed());
    // Never walked through the blocker's personal space.
    EXPECT_GE(minClearance, 1.1f);
}

// --- 3. Give-up rule for blocked routes --------------------------------------

// A line of creatures across the corridor at x = 0 that nothing can pass.
std::vector<std::shared_ptr<Creature>> wallOfCreatures(MovementHarness &harness) {
    std::vector<std::shared_ptr<Creature>> wall;
    for (float y = -2.0f; y <= 2.01f; y += 1.0f) {
        wall.push_back(harness.spawn(glm::vec3(0.0f, y, 0.0f)));
    }
    return wall;
}

bool anyPathActive(const Pathfinder &pf) {
    return std::any_of(pf.paths.begin(), pf.paths.end(), [](const AStarPath &p) { return p.active; });
}

TEST(C10GiveUp, a_move_blocked_by_creatures_gives_up_after_repeated_bumps) {
    MovementHarness harness;
    wallOfCreatures(harness);
    harness.pc->setPosition(glm::vec3(-10.0f, 0.0f, 0.0f));
    auto action = harness.game->newAction<MoveToPointAction>(glm::vec3(10.0f, 0.0f, 0.0f));
    harness.pc->addAction(action);

    int ticks = 0;
    float maxX = -100.0f;
    for (; ticks < 30 * 20 && !action->isCompleted(); ++ticks) {
        harness.pc->update(kTick);
        maxX = std::max(maxX, harness.pc->position().x);
    }
    ASSERT_TRUE(action->isCompleted());
    // K2 behaviour: walking up to the wall takes a few seconds, then the
    // bumps end the walk well before the no-progress backstop would.
    EXPECT_LT(ticks, static_cast<int>((Creature::kNavigationGiveUpSeconds + 8.0f) * 30));
    EXPECT_LT(maxX, 0.0f);

    // Nothing is left behind: queue empty, not moving, path slot released,
    // movement not restricted.
    harness.pc->update(kTick);
    EXPECT_TRUE(harness.pc->actions().empty());
    EXPECT_EQ(Creature::MovementType::None, harness.pc->movementType());
    EXPECT_FALSE(anyPathActive(harness.area->pathfinder()));
    EXPECT_FALSE(harness.pc->isMovementRestricted());
}

TEST(C10GiveUp, a_bump_counts_and_flips_the_remembered_pass_side) {
    MovementHarness harness;
    auto blocker = harness.spawn(glm::vec3(1.0f, 0.0f, 0.0f));
    blocker->setFacing(glm::radians(90.0f)); // facing the mover (-x)
    harness.pc->setFacing(glm::radians(-90.0f)); // facing +x
    ASSERT_FALSE(harness.pc->passesOnLeft());
    harness.pc->recordStepBlock(blocker->id(), false);
    steerWithoutProgress(harness, glm::vec3(0.0f), glm::vec3(5.0f, 0.0f, 0.0f));
    EXPECT_EQ(1, harness.pc->bumpCount());
    // K2 behaviour: the walker's side flips; facing each other, the blocker
    // takes the walker's new side.
    EXPECT_TRUE(harness.pc->passesOnLeft());
    EXPECT_TRUE(blocker->passesOnLeft());
}

TEST(C10GiveUp, a_step_nothing_blocked_is_not_a_bump) {
    MovementHarness harness;
    harness.pc->recordStepBlock(script::kObjectInvalid, false);
    steerWithoutProgress(harness, glm::vec3(0.0f), glm::vec3(5.0f, 0.0f, 0.0f));
    EXPECT_EQ(0, harness.pc->bumpCount());
}

TEST(C10GiveUp, a_blockage_that_clears_in_time_does_not_fail_the_move) {
    MovementHarness harness;
    auto wall = wallOfCreatures(harness);
    harness.pc->setPosition(glm::vec3(-3.0f, 0.0f, 0.0f));
    auto action = harness.game->newAction<MoveToPointAction>(glm::vec3(10.0f, 0.0f, 0.0f));
    harness.pc->addAction(action);
    // Blocked for about 3 s, then the creatures step aside.
    for (int tick = 0; tick < 90; ++tick) {
        harness.pc->update(kTick);
    }
    ASSERT_FALSE(action->isCompleted());
    for (auto &creature : wall) {
        creature->setPosition(glm::vec3(0.0f, creature->position().y > 0.0f ? 30.0f : -30.0f, 0.0f));
    }
    for (int tick = 0; tick < 30 * 20 && !action->isCompleted(); ++tick) {
        harness.pc->update(kTick);
    }
    EXPECT_TRUE(action->isCompleted());
    EXPECT_GT(harness.pc->position().x, 8.0f);
}

TEST(C10GiveUp, a_forced_move_keeps_trying_until_its_timeout) {
    MovementHarness harness;
    wallOfCreatures(harness);
    harness.pc->setPosition(glm::vec3(-3.0f, 0.0f, 0.0f));
    auto location = std::make_shared<Location>(glm::vec3(10.0f, 0.0f, 0.0f), 0.0f);
    auto action = harness.game->newAction<MoveToLocationAction>(location, false, true, 30.0f);
    harness.pc->addAction(action);
    for (int tick = 0; tick < static_cast<int>((Creature::kNavigationGiveUpSeconds * 2 + 2.0f) * 30); ++tick) {
        harness.pc->update(kTick);
    }
    // Past two give-up windows, still walking at the wall.
    EXPECT_FALSE(action->isCompleted());
}

TEST(C10GiveUp, an_ordinary_move_to_an_unreachable_point_finishes) {
    MovementHarness harness;
    harness.pc->setPosition(glm::vec3(-10.0f, 0.0f, 0.0f));
    auto location = std::make_shared<Location>(glm::vec3(50.0f, 0.0f, 0.0f), 0.0f);
    auto action = harness.game->newAction<MoveToLocationAction>(location, true);
    harness.pc->addAction(action);
    for (int tick = 0; tick < 60 && !action->isCompleted(); ++tick) {
        harness.pc->update(kTick);
    }
    EXPECT_TRUE(action->isCompleted());
}

// --- 4. Doors: barriers in path planning -------------------------------------

TEST(C10DoorBarriers, a_shut_doorway_blocks_planning_across_it) {
    MovementHarness harness;
    Pathfinder &pf = harness.area->pathfinder();
    const glm::vec3 from(-10.0f, 0.0f, 0.0f);
    const glm::vec3 to(10.0f, 0.0f, 0.0f);

    auto open = createPath(pf, from, to);
    ASSERT_TRUE(open);
    releasePath(pf, *open);

    setPathBarriers(pf, {PathBarrier {7, glm::vec2(0.0f, -3.0f), glm::vec2(0.0f, 3.0f)}});
    EXPECT_FALSE(createPath(pf, from, to));

    // Walking to the door itself is allowed: the barrier is at the
    // destination.
    auto toDoor = createPath(pf, from, glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_TRUE(toDoor);
    if (toDoor) {
        releasePath(pf, *toDoor);
    }

    setPathBarriers(pf, {});
    auto reopened = createPath(pf, from, to);
    EXPECT_TRUE(reopened);
}

TEST(C10DoorBarriers, the_revision_changes_only_when_the_barrier_set_does) {
    MovementHarness harness;
    Pathfinder &pf = harness.area->pathfinder();
    uint32_t start = pf.barrierRevision;
    std::vector<PathBarrier> set {PathBarrier {7, glm::vec2(0.0f, -3.0f), glm::vec2(0.0f, 3.0f)}};
    setPathBarriers(pf, set);
    EXPECT_EQ(start + 1, pf.barrierRevision);
    setPathBarriers(pf, set);
    EXPECT_EQ(start + 1, pf.barrierRevision);
    setPathBarriers(pf, {});
    EXPECT_EQ(start + 2, pf.barrierRevision);
}

TEST(C10DoorBarriers, a_mover_replans_when_a_doorway_shuts_and_then_gives_up) {
    MovementHarness harness;
    Pathfinder &pf = harness.area->pathfinder();
    harness.pc->setPosition(glm::vec3(-10.0f, 0.0f, 0.0f));
    auto action = harness.game->newAction<MoveToPointAction>(glm::vec3(10.0f, 0.0f, 0.0f));
    harness.pc->addAction(action);
    for (int tick = 0; tick < 10; ++tick) {
        harness.pc->update(kTick);
    }
    ASSERT_TRUE(anyPathActive(pf));
    // The door ahead shuts and locks.
    setPathBarriers(pf, {PathBarrier {7, glm::vec2(0.0f, -3.0f), glm::vec2(0.0f, 3.0f)}});
    for (int tick = 0; tick < 60 && !action->isCompleted(); ++tick) {
        harness.pc->update(kTick);
    }
    // The replan finds no route; the move ends instead of walking into the
    // door.
    EXPECT_TRUE(action->isCompleted());
    EXPECT_LT(harness.pc->position().x, -1.0f);
}

// --- 5. Party following -----------------------------------------------------

std::shared_ptr<Creature> addFollower(MovementHarness &harness, const glm::vec3 &position) {
    auto follower = harness.spawn(position);
    harness.game->party().addAvailableMember(0, follower);
    EXPECT_TRUE(harness.game->party().addMember(0, follower));
    return follower;
}

TEST(C10Follow, follow_leader_walks_to_the_follow_distance_and_completes) {
    MovementHarness harness;
    harness.pc->setPosition(glm::vec3(12.0f, 0.0f, 0.0f));
    auto follower = addFollower(harness, glm::vec3(-3.0f, 0.0f, 0.0f));
    auto action = harness.game->newAction<FollowLeaderAction>();
    follower->addAction(action);
    for (int tick = 0; tick < 30 * 8 && !action->isCompleted(); ++tick) {
        follower->update(kTick);
    }
    EXPECT_TRUE(action->isCompleted());
    float gap = glm::distance(glm::vec2(follower->position()), glm::vec2(harness.pc->position()));
    EXPECT_LE(gap, FollowLeaderAction::kPartyFollowDistance + 0.1f);
    EXPECT_GE(gap, FollowLeaderAction::kPartyFollowDistance - 1.0f);
}

TEST(C10Follow, follow_leader_on_the_leader_itself_ends) {
    // A leader switch clears every queue, but a follow queued on the leader
    // afterwards (a heartbeat racing the switch) must not wait forever.
    MovementHarness harness;
    auto action = harness.game->newAction<FollowLeaderAction>();
    harness.pc->addAction(action);
    harness.pc->update(kTick);
    EXPECT_TRUE(action->isCompleted());
}

TEST(C10Follow, an_unreachable_follower_out_of_sight_regroups_behind_the_leader) {
    MovementHarness harness;
    // The leader cannot see anything: the ray is blocked right away.
    ON_CALL(harness.sceneGraph, testLineOfSight(_, _, _))
        .WillByDefault(Invoke([](const glm::vec3 &origin, const glm::vec3 &, scene::Collision &collision) {
            collision.user = nullptr;
            collision.intersection = origin;
            return true;
        }));
    harness.pc->setPosition(glm::vec3(10.0f, 0.0f, 0.0f));
    // Off the walkmesh, so no path can be planned from there.
    auto follower = addFollower(harness, glm::vec3(-15.0f, 10.0f, 0.0f));
    auto action = harness.game->newAction<FollowLeaderAction>();
    follower->addAction(action);
    for (int tick = 0; tick < 90 && !action->isCompleted(); ++tick) {
        follower->update(kTick);
    }
    EXPECT_TRUE(action->isCompleted());
    EXPECT_LT(glm::distance(glm::vec2(follower->position()), glm::vec2(harness.pc->position())), 3.0f);
}

TEST(C10Follow, a_visible_or_nearby_unreachable_follower_is_not_moved) {
    MovementHarness harness;
    harness.pc->setPosition(glm::vec3(10.0f, 0.0f, 0.0f));
    // The default scene lets the leader see the follower.
    auto follower = addFollower(harness, glm::vec3(-15.0f, 10.0f, 0.0f));
    auto action = harness.game->newAction<FollowLeaderAction>();
    follower->addAction(action);
    for (int tick = 0; tick < 90; ++tick) {
        follower->update(kTick);
    }
    EXPECT_EQ(glm::vec3(-15.0f, 10.0f, 0.0f), follower->position());
}

TEST(C10Follow, force_follow_persists_at_rest_and_resumes_when_the_followee_moves) {
    MovementHarness harness;
    auto followee = harness.spawn(glm::vec3(0.0f, 0.0f, 0.0f));
    harness.pc->setPosition(glm::vec3(-8.0f, 0.0f, 0.0f));
    auto action = harness.game->newAction<FollowAction>(followee, 3.0f);
    harness.pc->addAction(action);
    for (int tick = 0; tick < 30 * 8; ++tick) {
        harness.pc->update(kTick);
    }
    // Arrived and resting, but still following (nss: until ClearAllActions).
    EXPECT_FALSE(action->isCompleted());
    EXPECT_FALSE(action->isMoving());
    float gap = glm::distance(glm::vec2(harness.pc->position()), glm::vec2(followee->position()));
    EXPECT_LE(gap, 3.1f);

    followee->setPosition(glm::vec3(12.0f, 0.0f, 0.0f));
    for (int tick = 0; tick < 30 * 10; ++tick) {
        harness.pc->update(kTick);
    }
    EXPECT_FALSE(action->isCompleted());
    gap = glm::distance(glm::vec2(harness.pc->position()), glm::vec2(followee->position()));
    EXPECT_LE(gap, 3.1f);

    harness.pc->clearAllActions();
    EXPECT_TRUE(harness.pc->actions().empty() || action->isCompleted());
}

// --- 6. Friendly collision option ---------------------------------------------

TEST(C10FriendlyCollision, retail_collision_by_default) {
    MovementHarness harness;
    auto companion = addFollower(harness, glm::vec3(3.0f, 0.0f, 0.0f));
    EXPECT_FALSE(harness.area->ignoresCreatureCollision(*harness.pc, *companion));
}

TEST(C10FriendlyCollision, option_off_lets_friendly_pairs_pass_and_keeps_others_solid) {
    MovementHarness harness;
    harness.engine.options().game.friendlyCollisionOff = true;
    auto companion = addFollower(harness, glm::vec3(3.0f, 0.0f, 0.0f));
    auto coop = harness.spawn(glm::vec3(5.0f, 0.0f, 0.0f));
    coop->setCoopPartyCharacter(true);
    auto neutral = harness.spawn(glm::vec3(7.0f, 0.0f, 0.0f));

    EXPECT_TRUE(harness.area->ignoresCreatureCollision(*harness.pc, *companion));
    EXPECT_TRUE(harness.area->ignoresCreatureCollision(*harness.pc, *coop));
    EXPECT_TRUE(harness.area->ignoresCreatureCollision(*companion, *coop));
    EXPECT_FALSE(harness.area->ignoresCreatureCollision(*harness.pc, *neutral));
    EXPECT_FALSE(harness.area->ignoresCreatureCollision(*neutral, *coop));

    // Avoidance and the side-step probes skip friendlies only.
    harness.pc->setPosition(glm::vec3(1.5f, 0.2f, 0.0f));
    EXPECT_EQ(glm::vec3(0.0f), harness.pc->computeCreatureAvoidance(glm::vec3(1.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(harness.area->isPointClearOfCreatures(*harness.pc, glm::vec3(3.0f, 0.0f, 0.0f)));
    EXPECT_FALSE(harness.area->isPointClearOfCreatures(*harness.pc, glm::vec3(7.0f, 0.0f, 0.0f)));
    harness.engine.options().game.friendlyCollisionOff = false;
}

TEST(C10FriendlyCollision, option_off_lets_the_leader_walk_through_a_companion) {
    for (bool off : {false, true}) {
        MovementHarness harness;
        harness.engine.options().game.friendlyCollisionOff = off;
        harness.pc->setPosition(glm::vec3(-2.0f, 0.0f, 0.0f));
        auto companion = addFollower(harness, glm::vec3(0.0f, 0.0f, 0.0f));
        auto area = harness.area;
        // One straight step into the companion.
        area->moveCreature(harness.pc, glm::vec2(1.0f, 0.0f), false, 1.0f);
        float x = harness.pc->position().x;
        if (off) {
            EXPECT_GT(x, -0.5f);
        } else {
            EXPECT_LT(x, -1.1f);
        }
        harness.engine.options().game.friendlyCollisionOff = false;
    }
}

} // namespace

// --- 7. A shut door between attacker and target --------------------------------

namespace {

// The sight line from anywhere meets door first while it is shut.
void blockSightWith(MovementHarness &harness, const std::shared_ptr<Door> &door) {
    ON_CALL(harness.sceneGraph, testLineOfSight(_, _, _))
        .WillByDefault(Invoke([door](const glm::vec3 &origin, const glm::vec3 &dest, scene::Collision &collision) {
            if (door->isOpen()) {
                return false;
            }
            collision.user = door.get();
            collision.intersection = 0.5f * (origin + dest);
            return true;
        }));
}

} // namespace

// Blows do not pass a shut door: in range, the attacker opens an unlocked
// one first (as the OnBlocked AI and a player's click do) and the target
// takes no damage while the door is shut. Before, the close-in rule let the
// attack land through it (tar_m10aa, patrol droids behind ptar_secdoor).
TEST(C10DoorBetween, an_attacker_opens_an_unlocked_door_before_it_attacks) {
    MovementHarness harness;
    auto target = harness.spawn(glm::vec3(1.5f, 0.0f, 0.0f));
    target->setFaction(Faction::Hostile1);
    target->setMaxHitPoints(40);
    target->setCurrentHitPoints(40);
    auto door = harness.game->newDoor();
    door->setPosition(glm::vec3(0.75f, 0.0f, 0.0f));
    harness.area->add(door);
    blockSightWith(harness, door);
    int hp = target->currentHitPoints();

    auto action = harness.game->newAction<AttackObjectAction>(target);
    harness.pc->addAction(action);
    for (int tick = 0; tick < 30 && !door->isOpening() && !door->isOpen(); ++tick) {
        harness.pc->update(kTick);
        harness.game->combat().update(kTick);
    }
    EXPECT_TRUE(door->isOpening() || door->isOpen());
    EXPECT_EQ(hp, target->currentHitPoints());
}

TEST(C10DoorBetween, no_attack_lands_through_a_locked_door) {
    MovementHarness harness;
    auto target = harness.spawn(glm::vec3(1.5f, 0.0f, 0.0f));
    target->setFaction(Faction::Hostile1);
    target->setMaxHitPoints(40);
    target->setCurrentHitPoints(40);
    auto door = harness.game->newDoor();
    door->setPosition(glm::vec3(0.75f, 0.0f, 0.0f));
    door->setLocked(true);
    harness.area->add(door);
    blockSightWith(harness, door);
    int hp = target->currentHitPoints();

    auto action = harness.game->newAction<AttackObjectAction>(target);
    harness.pc->addAction(action);
    for (int tick = 0; tick < 30 * 12; ++tick) {
        harness.pc->update(kTick);
        harness.game->combat().update(kTick);
    }
    EXPECT_FALSE(door->isOpening() || door->isOpen());
    EXPECT_EQ(hp, target->currentHitPoints());
}

namespace {

constexpr int kDoorTestPistol = 12;

std::shared_ptr<resource::TwoDA> pistolBaseItems() {
    resource::TwoDA::Builder builder;
    builder.columns({"maxattackrange", "crithitmult", "critthreat", "damageflags", "dietoroll",
                     "equipableslots", "itemclass", "numdice", "weapontype", "weaponwield",
                     "ammunitiontype", "bodyvar"});
    for (int i = 0; i <= kDoorTestPistol; ++i) {
        if (i == kDoorTestPistol) {
            builder.row({"23", "2", "2", "2", "6", "48", "w_blstrpstl", "1", "4", "4", "-1", ""});
        } else {
            builder.row({"", "", "", "", "", "", "", "", "", "", "", ""});
        }
    }
    return std::shared_ptr<resource::TwoDA>(builder.build());
}

} // namespace

// Behind a locked door the attacker does not stand in place: it walks the
// path to its target (in the game the path goes round the locked door, a path
// barrier). No blow lands while the door is on the sight line. Before, it
// stood for good (tar_m10aa: the PC 28 m from a rec-room Vulkar, 900 ticks).
TEST(C10DoorBetween, an_attacker_behind_a_locked_door_walks_toward_its_target) {
    MovementHarness harness;
    ON_CALL(harness.engine.resourceModule().twoDas(), get("baseitems"))
        .WillByDefault(Return(pistolBaseItems()));
    auto gff = resource::Gff::Builder()
                   .field(resource::Gff::Field::newCExoString("Tag", "pistol"))
                   .field(resource::Gff::Field::newInt("BaseItem", kDoorTestPistol))
                   .field(resource::Gff::Field::newWord("StackSize", 1))
                   .build();
    auto pistol = harness.game->newItem();
    pistol->deserialize(*gff, SerializedIdentityContext::templateResource());
    ASSERT_TRUE(harness.pc->equip(InventorySlots::rightWeapon, pistol));

    auto target = harness.spawn(glm::vec3(10.0f, 0.0f, 0.0f));
    target->setFaction(Faction::Hostile1);
    target->setMaxHitPoints(40);
    target->setCurrentHitPoints(40);
    auto door = harness.game->newDoor();
    door->setPosition(glm::vec3(5.0f, 3.0f, 0.0f));
    door->setLocked(true);
    harness.area->add(door);
    blockSightWith(harness, door);
    float before = glm::distance(glm::vec2(harness.pc->position()), glm::vec2(target->position()));

    auto action = harness.game->newAction<AttackObjectAction>(target);
    harness.pc->addAction(action);
    for (int tick = 0; tick < 30 * 8; ++tick) {
        harness.pc->update(kTick);
        harness.game->combat().update(kTick);
    }
    float after = glm::distance(glm::vec2(harness.pc->position()), glm::vec2(target->position()));
    EXPECT_LT(after, before - 5.0f);
    EXPECT_FALSE(door->isOpening() || door->isOpen());
    EXPECT_EQ(40, target->currentHitPoints());
}

// A ranged attacker in range with a shut door well ahead walks to the door
// and opens it. Before, the walk to the target ended at once (in range) and
// dropped the path each frame, so the attacker never moved (tar_m05aa:
// Carth stood for 2500 ticks with the Gamorrean raid behind ptar_highdoor).
TEST(C10DoorBetween, a_ranged_attacker_walks_to_a_door_ahead_and_opens_it) {
    MovementHarness harness;
    ON_CALL(harness.engine.resourceModule().twoDas(), get("baseitems"))
        .WillByDefault(Return(pistolBaseItems()));
    auto gff = resource::Gff::Builder()
                   .field(resource::Gff::Field::newCExoString("Tag", "pistol"))
                   .field(resource::Gff::Field::newInt("BaseItem", kDoorTestPistol))
                   .field(resource::Gff::Field::newWord("StackSize", 1))
                   .build();
    auto pistol = harness.game->newItem();
    pistol->deserialize(*gff, SerializedIdentityContext::templateResource());
    ASSERT_TRUE(harness.pc->equip(InventorySlots::rightWeapon, pistol));
    ASSERT_GT(harness.pc->getAttackRange(), 10.0f);

    auto target = harness.spawn(glm::vec3(10.0f, 0.0f, 0.0f));
    target->setFaction(Faction::Hostile1);
    target->setMaxHitPoints(40);
    target->setCurrentHitPoints(40);
    auto door = harness.game->newDoor();
    door->setPosition(glm::vec3(5.0f, 0.0f, 0.0f));
    harness.area->add(door);
    blockSightWith(harness, door);

    auto action = harness.game->newAction<AttackObjectAction>(target);
    harness.pc->addAction(action);
    for (int tick = 0; tick < 30 * 8 && !door->isOpening() && !door->isOpen(); ++tick) {
        harness.pc->update(kTick);
        harness.game->combat().update(kTick);
    }
    EXPECT_TRUE(door->isOpening() || door->isOpen());
    EXPECT_LT(glm::distance(glm::vec2(harness.pc->position()), glm::vec2(door->position())), 3.0f);
    EXPECT_EQ(40, target->currentHitPoints());
}

// An attack on a target no path reaches gives up, as a walk does, rather than
// holding the attacker for good. Before, the PC's attack on a guest locked in
// a side room of Davik's estate (tar08_davikguest03 behind ptar_lowdoor)
// never ended: 80 rounds, no move, no blow, and a companion's equip order
// queued behind such an attack never ran.
TEST(C10DoorBetween, an_attack_on_an_unreachable_target_gives_up) {
    MovementHarness harness;
    auto target = harness.spawn(glm::vec3(10.0f, 8.0f, 0.0f));
    target->setFaction(Faction::Hostile1);
    target->setMaxHitPoints(40);
    target->setCurrentHitPoints(40);

    auto action = harness.game->newAction<AttackObjectAction>(target);
    harness.pc->addAction(action);
    int tick = 0;
    for (; tick < 30 * 30 && !harness.pc->actions().empty(); ++tick) {
        harness.pc->update(kTick);
        harness.game->combat().update(kTick);
    }
    EXPECT_TRUE(harness.pc->actions().empty()) << "attack still queued after " << tick << " ticks";
    EXPECT_EQ(40, target->currentHitPoints());
}

// The same for a ranged attacker in range with a wall in the way and no path
// to close in: the close-in walk is one walk, so its failures add up. Before,
// the walk into range ended "reached" every frame and reset them, and the PC
// stood for good 23 m from tar08_davikguest03 (locked in a side room).
TEST(C10DoorBetween, a_ranged_attack_with_no_line_of_fire_and_no_path_gives_up) {
    MovementHarness harness;
    ON_CALL(harness.engine.resourceModule().twoDas(), get("baseitems"))
        .WillByDefault(Return(pistolBaseItems()));
    auto gff = resource::Gff::Builder()
                   .field(resource::Gff::Field::newCExoString("Tag", "pistol"))
                   .field(resource::Gff::Field::newInt("BaseItem", kDoorTestPistol))
                   .field(resource::Gff::Field::newWord("StackSize", 1))
                   .build();
    auto pistol = harness.game->newItem();
    pistol->deserialize(*gff, SerializedIdentityContext::templateResource());
    ASSERT_TRUE(harness.pc->equip(InventorySlots::rightWeapon, pistol));

    auto target = harness.spawn(glm::vec3(8.0f, 8.0f, 0.0f));
    target->setFaction(Faction::Hostile1);
    target->setMaxHitPoints(40);
    target->setCurrentHitPoints(40);
    ASSERT_LT(harness.pc->getDistanceTo(*target), harness.pc->getAttackRange());
    // A wall (no door) between the two.
    ON_CALL(harness.sceneGraph, testLineOfSight(_, _, _))
        .WillByDefault(Invoke([](const glm::vec3 &origin, const glm::vec3 &dest, scene::Collision &collision) {
            collision.user = nullptr;
            collision.intersection = 0.5f * (origin + dest);
            return true;
        }));

    auto action = harness.game->newAction<AttackObjectAction>(target);
    harness.pc->addAction(action);
    int tick = 0;
    for (; tick < 30 * 30 && !harness.pc->actions().empty(); ++tick) {
        harness.pc->update(kTick);
        harness.game->combat().update(kTick);
    }
    EXPECT_TRUE(harness.pc->actions().empty()) << "attack still queued after " << tick << " ticks";
    EXPECT_EQ(40, target->currentHitPoints());
}
