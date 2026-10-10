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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <set>

#include "../fixtures/engine.h"
#include "reone/game/animations.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/followleader.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/action/usetalentonobject.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/castspell.h"
#include "reone/game/d20/spell.h"
#include "reone/game/fakecast.h"
#include "reone/game/game.h"
#include "reone/game/attack.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/resource/types.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/script/executioncontext.h"
#include "reone/system/randomutil.h"

// Attack animation selection stays internal to the action implementation. The
// variant is rolled inside the action, so calling these helpers is the only way
// to exercise a specific roll.
#include "../../src/libs/game/action/attackanimations.h"

using namespace reone;
using namespace reone::game;
using namespace testing;

TEST(CombatRoundReferences, live_participants_resolve_and_reach_execution_state) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto target = game.newCreature();
    auto action = game.newAction<AttackObjectAction>(target);

    const CombatRound &round = game.combat().addAction(action, *attacker);
    ASSERT_EQ(1u, round.actions.size());
    EXPECT_EQ(attacker, round.actions.front().attacker.resolve());
    EXPECT_EQ(target, round.actions.front().target.resolve());

    game.combat().update(0.0f);

    EXPECT_TRUE(round.canExecute(*action));
    EXPECT_EQ(1u, game.combat().roundCount());
}

TEST(CombatRoundReferences, retired_target_prunes_round_and_cancels_without_damage) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto target = game.newCreature();
    target->setCurrentHitPoints(12);
    auto action = game.newAction<AttackObjectAction>(target);
    const CombatRound &round = game.combat().addAction(action, *attacker);

    game.destroyRuntimeObjectGraph(target);

    ASSERT_TRUE(target);
    EXPECT_FALSE(target->isRuntimeLive());
    EXPECT_FALSE(round.actions.front().target.resolve());
    game.combat().update(0.0f);

    EXPECT_TRUE(action->isCancelled());
    EXPECT_TRUE(action->isCompleted());
    EXPECT_EQ(12, target->currentHitPoints());
    EXPECT_EQ(0u, game.combat().roundCount());
}

TEST(CombatRoundReferences, retired_attacker_prunes_round_despite_strong_storage) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto target = game.newCreature();
    auto action = game.newAction<AttackObjectAction>(target);
    const CombatRound &round = game.combat().addAction(action, *attacker);

    game.destroyRuntimeObjectGraph(attacker);

    ASSERT_TRUE(attacker);
    EXPECT_FALSE(attacker->isRuntimeLive());
    EXPECT_FALSE(round.actions.front().attacker.resolve());
    game.combat().update(0.0f);

    EXPECT_TRUE(action->isCancelled());
    EXPECT_TRUE(action->isCompleted());
    EXPECT_EQ(0u, game.combat().roundCount());
}

TEST(CombatRoundReferences, dead_but_live_target_is_a_gameplay_condition) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto target = game.newCreature();
    target->setCurrentHitPoints(1);
    auto action = game.newAction<AttackObjectAction>(target);
    const CombatRound &round = game.combat().addAction(action, *attacker);

    target->damage(std::numeric_limits<int>::max(), attacker);
    ASSERT_TRUE(target->isDead());
    ASSERT_TRUE(target->isRuntimeLive());
    game.combat().update(0.0f);

    EXPECT_EQ(target, round.actions.front().target.resolve());
    EXPECT_FALSE(action->isCancelled());
    EXPECT_EQ(1u, game.combat().roundCount());
}

TEST(CombatRoundReferences, action_dependencies_cancel_attack_and_feat_targets) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto attackTarget = game.newCreature();
    auto featTarget = game.newCreature();
    auto attack = game.newAction<AttackObjectAction>(attackTarget);
    auto feat = game.newAction<UseFeatAction>(FeatType::PowerAttack, featTarget);

    game.destroyRuntimeObjectGraph(attackTarget);
    game.destroyRuntimeObjectGraph(featTarget);
    ASSERT_FALSE(attack->runtimeDependenciesLive());
    ASSERT_FALSE(feat->runtimeDependenciesLive());

    attack->execute(attack, *attacker, 0.0f);
    feat->execute(feat, *attacker, 0.0f);

    EXPECT_TRUE(attack->isCancelled());
    EXPECT_TRUE(feat->isCancelled());
    EXPECT_FALSE(attack->target());
    EXPECT_FALSE(feat->target());
}

TEST(CombatRoundReferences, completed_or_dequeued_actions_do_not_keep_rounds_alive) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto queuedAttacker = game.newCreature();
    auto queuedTarget = game.newCreature();
    auto queued = game.newAction<AttackObjectAction>(queuedTarget);
    queuedAttacker->addAction(queued);
    game.combat().addAction(queued, *queuedAttacker);
    ASSERT_EQ(1u, game.combat().roundCount());

    queuedAttacker->clearAllActions(/*force=*/true);
    game.combat().update(0.0f);

    EXPECT_TRUE(queued->isCancelled());
    EXPECT_TRUE(queued->isCompleted());
    EXPECT_EQ(0u, game.combat().roundCount());

    auto completedAttacker = game.newCreature();
    auto completedTarget = game.newCreature();
    auto completed = game.newAction<AttackObjectAction>(completedTarget);
    game.combat().addAction(completed, *completedAttacker);
    completed->complete();

    game.combat().update(0.0f);

    EXPECT_EQ(0u, game.combat().roundCount());

    auto cancelledAttacker = game.newCreature();
    auto cancelledTarget = game.newCreature();
    auto cancelled = game.newAction<AttackObjectAction>(cancelledTarget);
    game.combat().addAction(cancelled, *cancelledAttacker);
    cancelled->markCancelled();

    game.combat().update(0.0f);

    EXPECT_TRUE(cancelled->isCompleted());
    EXPECT_EQ(0u, game.combat().roundCount());
}

TEST(Action, use_talent_dispatch_to_use_feat) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    auto player = game.newCreature();
    auto target = game.newCreature();
    auto talent = game.newTalent(TalentType::Feat, static_cast<int>(FeatType::PowerAttack));
    auto action = game.newAction<UseTalentOnObjectAction>(std::move(talent), target);
    auto subAction = action->subAction();
    ASSERT_TRUE(subAction);
    EXPECT_TRUE(action->saveFacingState());

    EXPECT_FALSE(action->isCompleted());
    EXPECT_FALSE(subAction->isCompleted());

    // Cycle through combat states
    for (int i = 0; i < 10; ++i) {
        action->execute(action, *target, 1.0f);
        game.combat().update(2.0f);
    }

    EXPECT_TRUE(action->isCompleted());
    EXPECT_TRUE(subAction->isCompleted());
}

TEST(Action, use_talent_dispatch_to_cast_spell) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    auto spell = std::make_shared<Spell>();
    spell->type = SpellType::LightSaberThrow;
    spell->castTime = 1.0f;
    spell->conjTime = 1.0f;

    EXPECT_CALL(engine.gameModule().spells(), get(SpellType::LightSaberThrow))
        .Times(AnyNumber())
        .WillRepeatedly(Return(spell));

    auto player = game.newCreature();
    auto target = game.newCreature();
    auto talent = game.newTalent(TalentType::Spell, static_cast<int>(SpellType::LightSaberThrow));
    auto action = game.newAction<UseTalentOnObjectAction>(std::move(talent), target);
    auto subAction = action->subAction();
    ASSERT_TRUE(subAction);
    EXPECT_FALSE(action->saveFacingState());

    EXPECT_FALSE(action->isCompleted());
    EXPECT_FALSE(subAction->isCompleted());

    // Cycle through combat states
    for (int i = 0; i < 10; ++i) {
        action->execute(action, *target, 1.0f);
        game.combat().update(2.0f);
    }

    EXPECT_TRUE(action->isCompleted());
    EXPECT_TRUE(subAction->isCompleted());
}

namespace {

constexpr char kScriptedDialog[] = "hk50";

// A conversation-start action queued on a speaker and aimed at a listener, in
// the shape scripts use: a named dialogue, as K2 103PER k_102exit and
// a_hk50forcedlg both do.
//
// The speaker is left with an approach still to make, which is what makes the
// action's fate observable either way: discarded actions complete, whereas an
// honored one stays in progress while its speaker walks over. The guard runs
// before the approach, so it behaves the same whichever start range a script
// asks for.
std::shared_ptr<StartConversationAction> makeScriptedStartConversation(
    Game &game,
    const std::shared_ptr<Object> &listener) {

    return game.newAction<StartConversationAction>(
        listener,
        kScriptedDialog,
        /*privateConversation=*/false,
        resource::ConversationType::Cinematic,
        /*ignoreStartRange=*/false);
}

// Restricted movement keeps that approach unfinished for a whole test, so
// assertions stay on the action rather than on pathfinding.
std::shared_ptr<Creature> makeApproachingSpeaker(Game &game) {
    auto speaker = game.newCreature();
    speaker->setMovementRestricted(true);
    return speaker;
}

void setScreen(Game &game, Game::Screen screen) {
    TestGameModule::setCurrentScreen(game, static_cast<int>(screen));
}

} // namespace

// A. An already-running conversation rejects a newly executed
// ActionStartConversation. The action is dropped outright rather than beginning
// the approach it would otherwise begin, no dialogue is resolved or loaded, and
// the running conversation keeps the screen.
TEST(Action, start_conversation_is_discarded_while_a_conversation_is_active) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::TSL, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().gffs(), get(_, resource::ResType::Dlg)).Times(0);

    auto speaker = makeApproachingSpeaker(game);
    auto listener = game.newCreature();
    setScreen(game, Game::Screen::Conversation);
    ASSERT_TRUE(game.isConversationActive());

    auto action = makeScriptedStartConversation(game, listener);
    action->execute(action, *speaker, 1.0f);

    EXPECT_TRUE(action->isCompleted());
    EXPECT_EQ(Game::Screen::Conversation, game.currentScreen());
    EXPECT_TRUE(game.isConversationActive());
}

// B. The rejected action is dropped, not deferred. Once the running
// conversation ends, the action must not come back to life - if it did, K2
// 103PER would answer its own recovery call the instant a_tlkhk50 finished and
// HK-50 would immediately re-greet the player.
TEST(Action, discarded_start_conversation_does_not_run_after_the_conversation_ends) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::TSL, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().gffs(), get(_, resource::ResType::Dlg)).Times(0);

    auto speaker = makeApproachingSpeaker(game);
    auto listener = game.newCreature();
    setScreen(game, Game::Screen::Conversation);

    auto action = makeScriptedStartConversation(game, listener);
    speaker->addAction(action);

    // Executed while the conversation owns the screen: rejected on the spot.
    speaker->update(1.0f);
    EXPECT_TRUE(action->isCompleted());

    // A completed action is reaped on the following update, leaving the queue
    // empty rather than holding the conversation call open.
    speaker->update(1.0f);
    EXPECT_TRUE(speaker->actions().empty());

    // The conversation ends and the queue is pumped again.
    setScreen(game, Game::Screen::InGame);
    ASSERT_FALSE(game.isConversationActive());
    speaker->update(1.0f);
    speaker->update(1.0f);

    EXPECT_TRUE(speaker->actions().empty());
    EXPECT_EQ(Game::Screen::InGame, game.currentScreen());
}

// C. The complement of A, and the check that the guard rejects nothing it
// should not: with no conversation running, the very same action survives and
// its speaker sets about approaching the listener instead of being discarded.
TEST(Action, start_conversation_survives_when_no_conversation_is_active) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::TSL, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().gffs(), get(_, resource::ResType::Dlg)).Times(0);

    auto speaker = makeApproachingSpeaker(game);
    auto listener = game.newCreature();
    setScreen(game, Game::Screen::InGame);
    ASSERT_FALSE(game.isConversationActive());

    auto action = makeScriptedStartConversation(game, listener);
    action->execute(action, *speaker, 1.0f);

    EXPECT_FALSE(action->isCompleted());
}

// D. Routine 701 reports the same conversation-active state the guard uses.
TEST(Action, get_is_conversation_active_reports_the_shared_predicate) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::TSL, "", engine.options(), engine.services(), console);
    Routines routines(resource::GameID::TSL, &game, &engine.services());
    routines.init();
    script::Routine &routine = routines.get(701);
    ASSERT_EQ("GetIsConversationActive", routine.name());

    script::ExecutionContext ctx;

    setScreen(game, Game::Screen::InGame);
    EXPECT_EQ(0, routine.invoke({}, ctx).intValue);

    setScreen(game, Game::Screen::Conversation);
    EXPECT_EQ(1, routine.invoke({}, ctx).intValue);
}

namespace {

// A leader far enough away that a follower still has ground to cover, so an
// honored FollowLeader stays in progress rather than completing on arrival.
constexpr float kFarFromLeader = 10.0f * kDefaultFollowDistance;

} // namespace

// E. FollowLeader has no leader to follow while the party is empty - before it
// is first populated, after a module transition resets it, and once the last
// member is removed. The action is dropped there instead of dereferencing the
// absent leader, and nothing else is promoted in its place.
TEST(Action, follow_leader_is_discarded_when_the_party_has_no_leader) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    auto follower = makeApproachingSpeaker(game);
    ASSERT_TRUE(game.party().isEmpty());
    ASSERT_FALSE(game.party().getLeader());

    auto action = game.newAction<FollowLeaderAction>();
    action->execute(action, *follower, 1.0f);

    EXPECT_TRUE(action->isCompleted());
}

// F. The complement of E, and the check that the guard drops nothing it should
// not: with a leader in the party the action survives and its follower sets
// about the approach instead of being discarded.
TEST(Action, follow_leader_survives_when_the_party_has_a_leader) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    auto leader = game.newCreature();
    leader->setPosition(glm::vec3(kFarFromLeader, 0.0f, 0.0f));
    game.party().addMember(kNpcPlayer, leader);
    ASSERT_EQ(leader, game.party().getLeader());

    auto follower = makeApproachingSpeaker(game);

    auto action = game.newAction<FollowLeaderAction>();
    action->execute(action, *follower, 1.0f);

    EXPECT_FALSE(action->isCompleted());
}

// G. Leader-present completion is unchanged too: a follower already within
// following distance of the leader finishes the action.
TEST(Action, follow_leader_completes_once_the_follower_is_with_the_leader) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    auto leader = game.newCreature();
    game.party().addMember(kNpcPlayer, leader);
    auto follower = game.newCreature();
    ASSERT_EQ(leader->position(), follower->position());

    auto action = game.newAction<FollowLeaderAction>();
    action->execute(action, *follower, 1.0f);

    EXPECT_TRUE(action->isCompleted());
}

namespace {

// Authored attack animations this selector may choose from. Variant 0 is not
// authored in any family, in either game. K2 authors further non-cinematic
// variants for some families, which this selector deliberately leaves unused.
const std::set<std::string> kAuthoredAttackAnims {
    "g1a1", "g1a2",                               // stun baton
    "g2a1", "g2a2", "m2a1", "m2a2",               // single sword
    "g3a1", "g3a2", "m3a1", "m3a2",               // double bladed sword
    "g4a1", "g4a2", "m4a1", "m4a2",               // dual swords
    "g8a1", "g8a2",                               // unarmed
    "c2a1", "c2a2", "c2a3", "c2a4", "c2a5",       // single sword duel
    "c3a1", "c3a2", "c3a3", "c3a4", "c3a5",       // double bladed duel
    "c4a1", "c4a2", "c4a3", "c4a4", "c4a5",       // dual swords duel
    "c10a1", "c10a2", "c10a3", "c10a4", "c10a5"}; // complex unarmed duel

constexpr int kFirstVariantRoll = 1;
constexpr int kLastVariantRoll = 5;

} // namespace

TEST(AttackAnimation, non_cinematic_melee_attacks_never_request_variant_zero) {
    const CreatureWieldType wields[] {
        CreatureWieldType::SingleSword,
        CreatureWieldType::DoubleBladedSword,
        CreatureWieldType::DualSwords};

    for (auto wield : wields) {
        for (int roll = kFirstVariantRoll; roll <= kLastVariantRoll; ++roll) {
            // A door or a placeable has no wield of its own.
            std::string generic = getMeleeAttackAnim(wield, CreatureWieldType::None, roll, /*duel=*/false);
            // A creature attacked outside a duel.
            std::string monster = getMeleeAttackAnim(wield, CreatureWieldType::SingleSword, roll, /*duel=*/false);
            std::string stunBaton = getStunBatonAttackAnim(roll);
            std::string unarmed = getUnarmedAttackAnim(
                CreatureWieldType::HandToHand, CreatureWieldType::None, roll, /*duel=*/false);

            for (const std::string &anim : {generic, monster, stunBaton, unarmed}) {
                EXPECT_THAT(anim, Not(EndsWith("a0"))) << "roll " << roll;
                EXPECT_EQ(1u, kAuthoredAttackAnims.count(anim)) << anim << " is not an authored animation";
            }
        }
    }
}

TEST(AttackAnimation, a_roll_of_three_selects_an_authored_variant) {
    // A roll of 3 used to reduce to variant 0, which exists in no animation
    // family, so the attacker played nothing at all.
    EXPECT_EQ("g1a1", getStunBatonAttackAnim(3));
    EXPECT_EQ("g2a1", getMeleeAttackAnim(CreatureWieldType::SingleSword, CreatureWieldType::None, 3, /*duel=*/false));
    EXPECT_EQ("m2a1", getMeleeAttackAnim(CreatureWieldType::SingleSword, CreatureWieldType::SingleSword, 3, /*duel=*/false));
    EXPECT_EQ("g8a1", getUnarmedAttackAnim(CreatureWieldType::HandToHand, CreatureWieldType::None, 3, /*duel=*/false));
}

TEST(AttackAnimation, stun_baton_attacks_select_authored_variants) {
    // Every roll has to land on an animation the models author.
    std::set<std::string> selected;
    for (int roll = kFirstVariantRoll; roll <= kLastVariantRoll; ++roll) {
        selected.insert(getStunBatonAttackAnim(roll));
    }

    EXPECT_EQ((std::set<std::string> {"g1a1", "g1a2"}), selected);
}

TEST(AttackAnimation, generic_single_sword_attacks_select_authored_variants) {
    std::set<std::string> selected;
    for (int roll = kFirstVariantRoll; roll <= kLastVariantRoll; ++roll) {
        selected.insert(getMeleeAttackAnim(
            CreatureWieldType::SingleSword, CreatureWieldType::None, roll, /*duel=*/false));
    }

    EXPECT_EQ((std::set<std::string> {"g2a1", "g2a2"}), selected);
}

TEST(AttackAnimation, non_duel_creature_attacks_select_authored_monster_variants) {
    std::set<std::string> selected;
    for (int roll = kFirstVariantRoll; roll <= kLastVariantRoll; ++roll) {
        selected.insert(getMeleeAttackAnim(
            CreatureWieldType::SingleSword, CreatureWieldType::SingleSword, roll, /*duel=*/false));
    }

    EXPECT_EQ((std::set<std::string> {"m2a1", "m2a2"}), selected);
}

TEST(AttackAnimation, unarmed_attacks_select_authored_variants) {
    std::set<std::string> selected;
    for (int roll = kFirstVariantRoll; roll <= kLastVariantRoll; ++roll) {
        selected.insert(getUnarmedAttackAnim(
            CreatureWieldType::HandToHand, CreatureWieldType::None, roll, /*duel=*/false));
        // A complex unarmed attacker outside a duel falls back to the same set.
        selected.insert(getUnarmedAttackAnim(
            CreatureWieldType::HandToHandComplex, CreatureWieldType::SingleSword, roll, /*duel=*/false));
    }

    EXPECT_EQ((std::set<std::string> {"g8a1", "g8a2"}), selected);
}

TEST(AttackAnimation, duels_still_select_all_five_cinematic_variants) {
    std::set<std::string> selected;
    for (int roll = kFirstVariantRoll; roll <= kLastVariantRoll; ++roll) {
        selected.insert(getMeleeAttackAnim(
            CreatureWieldType::SingleSword, CreatureWieldType::DualSwords, roll, /*duel=*/true));
    }

    EXPECT_EQ((std::set<std::string> {"c2a1", "c2a2", "c2a3", "c2a4", "c2a5"}), selected);

    // Complex unarmed duels keep their own cinematic variants.
    std::set<std::string> unarmed;
    for (int roll = kFirstVariantRoll; roll <= kLastVariantRoll; ++roll) {
        unarmed.insert(getUnarmedAttackAnim(
            CreatureWieldType::HandToHandComplex, CreatureWieldType::HandToHandComplex, roll, /*duel=*/true));
    }

    EXPECT_EQ((std::set<std::string> {"c10a1", "c10a2", "c10a3", "c10a4", "c10a5"}), unarmed);
}

namespace {

constexpr uint32_t kD20SeedSearchLimit = 1'000'000;

uint32_t findD20Seed(int firstRoll, int secondRoll = 0) {
    for (uint32_t seed = 0; seed < kD20SeedSearchLimit; ++seed) {
        setRandomSeed(seed);
        if (randomInt(1, 20) != firstRoll) {
            continue;
        }
        if (secondRoll == 0 || randomInt(1, 20) == secondRoll) {
            return seed;
        }
    }
    return std::numeric_limits<uint32_t>::max();
}

AttackResultType rollUnarmedAttack(
    const Creature &attacker,
    const Object &target,
    int firstRoll,
    int secondRoll = 0) {

    // std::default_random_engine and uniform_int_distribution do not provide a
    // portable seed-to-roll mapping, so locate a suitable seed for this STL.
    uint32_t seed = findD20Seed(firstRoll, secondRoll);
    if (seed == std::numeric_limits<uint32_t>::max()) {
        ADD_FAILURE() << "Could not find seed for d20 sequence "
                      << firstRoll << ", " << secondRoll;
        return AttackResultType::Invalid;
    }

    setRandomSeed(seed);
    AttackBuffer attacks;
    attacks.addPhysicalAttacks(attacker, target);
    return attacks.result();
}

std::shared_ptr<resource::TwoDA> retailImpactAnimations() {
    resource::TwoDA::Builder builder;
    builder.columns({"name", "attack"});
    const std::map<int, std::string> attacks {
        {87, "g1a1"},   // stun baton
        {114, "f2a2"},  // Flurry
        {122, "g2a1"},  // single weapon
        {204, "g4a1"},  // dual wield
        {247, "g8a1"},  // unarmed
    };
    for (int row = 0; row <= 247; ++row) {
        auto found = attacks.find(row);
        builder.row({
            found == attacks.end()
                ? "anim" + std::to_string(row)
                : found->second,
            found == attacks.end() ? "0" : "1",
        });
    }
    return builder.build();
}

std::shared_ptr<resource::TwoDA> retailImpactTimes() {
    resource::TwoDA::Builder builder;
    builder.columns({"hits", "hit1", "hit2", "hit3"});
    builder.row("87", {"1", "500", "", ""});
    builder.row("114", {"1", "767", "", ""});
    builder.row("122", {"1", "500", "", ""});
    builder.row("204", {"1", "500", "", ""});
    builder.row("247", {"1", "500", "", ""});
    return builder.build();
}

Animations loadRetailImpactFixture(TestEngine &engine) {
    auto animations = retailImpactAnimations();
    auto combatAnimations = retailImpactTimes();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("animations"))
        .WillOnce(Return(animations));
    EXPECT_CALL(engine.resourceModule().twoDas(), get("combatanimations.2da"))
        .WillOnce(Return(combatAnimations));
    Animations result(engine.resourceModule().twoDas());
    result.init();
    return result;
}

} // namespace

TEST(PhysicalAttackResolution, natural_one_and_twenty_override_totals) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto target = game.newCreature();
    target->setObjectSeen(attacker, true);

    attacker->attributes().setAbilityScore(Ability::Strength, 100);
    target->attributes().setAbilityScore(Ability::Dexterity, 10);
    EXPECT_EQ(
        AttackResultType::Miss,
        rollUnarmedAttack(*attacker, *target, 1));

    attacker->attributes().setAbilityScore(Ability::Strength, 3);
    target->attributes().setAbilityScore(Ability::Dexterity, 100);
    EXPECT_EQ(
        AttackResultType::HitSuccessful,
        rollUnarmedAttack(*attacker, *target, 20, 1));
}

TEST(PhysicalAttackResolution, ordinary_rolls_compare_against_defense) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto target = game.newCreature();
    attacker->attributes().setAbilityScore(Ability::Strength, 10);
    target->attributes().setAbilityScore(Ability::Dexterity, 10);
    target->setObjectSeen(attacker, true);

    EXPECT_EQ(
        AttackResultType::Miss,
        rollUnarmedAttack(*attacker, *target, 9));
    EXPECT_EQ(
        AttackResultType::HitSuccessful,
        rollUnarmedAttack(*attacker, *target, 10));
}

TEST(PhysicalAttackResolution, critical_threat_requires_confirmation) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto target = game.newCreature();
    attacker->attributes().setAbilityScore(Ability::Strength, 10);
    target->attributes().setAbilityScore(Ability::Dexterity, 10);
    target->setObjectSeen(attacker, true);

    EXPECT_EQ(
        AttackResultType::CriticalHit,
        rollUnarmedAttack(*attacker, *target, 20, 10));
    EXPECT_EQ(
        AttackResultType::HitSuccessful,
        rollUnarmedAttack(*attacker, *target, 20, 9));
}

namespace {

constexpr int kTestPistol = 12;
constexpr int kTestSaber = 8;

std::shared_ptr<resource::TwoDA> deflectionBaseItemsTable() {
    resource::TwoDA::Builder builder;
    builder.columns({"maxattackrange", "crithitmult", "critthreat", "damageflags", "dietoroll",
                     "equipableslots", "itemclass", "numdice", "weapontype", "weaponwield",
                     "ammunitiontype", "bodyvar"});
    for (int i = 0; i <= kTestPistol; ++i) {
        if (i == kTestSaber) {
            builder.row({"1.5", "2", "2", "2", "8", "48", "w_lghtsbr", "1", "1", "2", "-1", ""});
        } else if (i == kTestPistol) {
            builder.row({"23", "2", "2", "2", "6", "48", "w_blstrpstl", "1", "4", "4", "-1", ""});
        } else {
            builder.row({"", "", "", "", "", "", "", "", "", "", "", ""});
        }
    }
    return std::shared_ptr<resource::TwoDA>(builder.build());
}

std::shared_ptr<Item> makeDeflectionTestItem(Game &game, std::string tag, int baseItem) {
    auto gff = resource::Gff::Builder()
                   .field(resource::Gff::Field::newCExoString("Tag", std::move(tag)))
                   .field(resource::Gff::Field::newInt("BaseItem", baseItem))
                   .field(resource::Gff::Field::newWord("StackSize", 1))
                   .build();
    auto item = game.newItem();
    item->deserialize(*gff, SerializedIdentityContext::templateResource());
    return item;
}

} // namespace

TEST(BlasterDeflection, a_lightsaber_wielder_with_proficiency_can_deflect_a_blaster_bolt) {
    TestEngine &engine = testEngine();
    ON_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .WillByDefault(Return(deflectionBaseItemsTable()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto target = game.newCreature();
    attacker->attributes().setAbilityScore(Ability::Dexterity, 10);
    target->attributes().setAbilityScore(Ability::Dexterity, 10);
    target->setObjectSeen(attacker, true);
    // Apart, so ranged combat's close-proximity bonus does not confound the roll.
    attacker->setPosition(glm::vec3(0.0f, 0.0f, 0.0f));
    target->setPosition(glm::vec3(0.0f, 50.0f, 0.0f));

    EXPECT_TRUE(attacker->equip(InventorySlots::rightWeapon, makeDeflectionTestItem(game, "pistol", kTestPistol)));
    EXPECT_TRUE(target->equip(InventorySlots::rightWeapon, makeDeflectionTestItem(game, "saber", kTestSaber)));
    target->attributes().addFeat(FeatType::WeaponProficiencyLightsaber);

    // Attack roll 15 beats the target's baseline defense (10) without
    // reaching the weapon's critical threat (19+); the deflect roll (also a
    // d20, no Deflect feat bonus) then needs to clear that same total (15).
    EXPECT_EQ(AttackResultType::Deflected, rollUnarmedAttack(*attacker, *target, 15, 15));
    EXPECT_EQ(AttackResultType::HitSuccessful, rollUnarmedAttack(*attacker, *target, 15, 14));
}

TEST(BlasterDeflection, a_target_without_a_lightsaber_or_proficiency_cannot_deflect) {
    TestEngine &engine = testEngine();
    ON_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .WillByDefault(Return(deflectionBaseItemsTable()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto target = game.newCreature();
    attacker->attributes().setAbilityScore(Ability::Dexterity, 10);
    target->attributes().setAbilityScore(Ability::Dexterity, 10);
    target->setObjectSeen(attacker, true);
    attacker->setPosition(glm::vec3(0.0f, 0.0f, 0.0f));
    target->setPosition(glm::vec3(0.0f, 50.0f, 0.0f));

    EXPECT_TRUE(attacker->equip(InventorySlots::rightWeapon, makeDeflectionTestItem(game, "pistol", kTestPistol)));
    // No lightsaber equipped, and no Weapon Proficiency (Lightsaber) feat.

    EXPECT_EQ(AttackResultType::HitSuccessful, rollUnarmedAttack(*attacker, *target, 15));

    target->attributes().addFeat(FeatType::WeaponProficiencyLightsaber);
    // Has the feat now, but still no lightsaber equipped.
    EXPECT_EQ(AttackResultType::HitSuccessful, rollUnarmedAttack(*attacker, *target, 15));
}

TEST(BlasterDeflection, resolve_and_feat_bonus_pure_functions) {
    EXPECT_TRUE(resolveBlasterDeflection(/*deflectRoll=*/15, /*deflectBonus=*/0, /*attackTotal=*/15));
    EXPECT_FALSE(resolveBlasterDeflection(/*deflectRoll=*/14, /*deflectBonus=*/0, /*attackTotal=*/15));
    EXPECT_TRUE(resolveBlasterDeflection(/*deflectRoll=*/10, /*deflectBonus=*/5, /*attackTotal=*/15));

    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto creature = game.newCreature();

    // No Deflect feat (K1, or a K2 creature without Weapon Master): no bonus.
    EXPECT_EQ(0, blasterDeflectionFeatBonus(*creature));

    // K2's Deflect feat (Weapon Master's capstone; not present in K1's
    // feat.2da): a flat +1 with no aggregate class levels recorded.
    creature->attributes().addFeat(FeatType::Deflect);
    EXPECT_EQ(1, blasterDeflectionFeatBonus(*creature));
}

TEST(AttackImpactTiming, uses_retail_combat_animation_hit_columns) {
    TestEngine &engine = testEngine();
    Animations animations = loadRetailImpactFixture(engine);

    // These values are shared by the shipped K1/K2 tables. The special feat
    // row demonstrates that the animation, not the weapon base item, owns the
    // timing authority.
    EXPECT_EQ(500, animations.getMeleeImpactTime("g2a1", 0));
    EXPECT_EQ(500, animations.getMeleeImpactTime("g4a1", 0));
    EXPECT_EQ(500, animations.getMeleeImpactTime("g8a1", 0));
    EXPECT_EQ(767, animations.getMeleeImpactTime("f2a2", 0));
}

TEST(AttackImpactTiming, missing_rows_and_shots_use_retail_zero_fallback) {
    TestEngine &engine = testEngine();
    Animations animations = loadRetailImpactFixture(engine);

    EXPECT_EQ(0, animations.getMeleeImpactTime("not_authored", 0));
    EXPECT_EQ(0, animations.getMeleeImpactTime("g2a1", 1));
}

TEST(AttackImpactTiming, melee_damage_waits_for_the_authored_impact) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto target = game.newCreature();
    attacker->applyAssuredHit();
    target->setCurrentHitPoints(20);

    AttackBuffer attacks;
    attacks.addPhysicalAttacks(*attacker, *target);
    attacks.resolve(*attacker, *target);
    NiceMock<MockAnimations> animations;
    ON_CALL(animations, getMeleeImpactTime("g8a1", 0))
        .WillByDefault(Return(500));
    attacks.prepareMeleeSequence(animations, {"g8a1"});

    EXPECT_EQ(0u, attacks.signalReadyMelee(
        499, game, engine.services(), *attacker, *target));
    EXPECT_EQ(20, target->currentHitPoints());
    EXPECT_EQ(1u, attacks.signalReadyMelee(
        500, game, engine.services(), *attacker, *target));
    EXPECT_LT(target->currentHitPoints(), 20);
}

namespace {

// An action whose step runs a script on its actor, as a walk runs the OnEnter
// of a trigger it crosses. The script clears the actor's actions.
class ClearingStepAction : public game::Action {
public:
    ClearingStepAction(Game &game, ServicesView &services, bool foreign) :
        game::Action(game, services, ActionType::Wait), _foreign(foreign) {}

    void execute(std::shared_ptr<game::Action> self, Object &actor, float dt) override {
        if (_foreign) {
            Object::ForeignScriptScope scope(actor);
            actor.clearAllActions();
        } else {
            actor.clearAllActions();
        }
    }

private:
    bool _foreign;
};

} // namespace

// The action's own script (an ActionDoCommand(ClearAllActions())) keeps the
// executing action and the continuation it hands off to.
TEST(Action, clear_all_actions_from_the_executing_action_keeps_that_action) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto actor = game.newCreature();
    auto step = game.newAction<ClearingStepAction>(/*foreign=*/false);
    actor->addAction(step);

    actor->update(1.0f);

    ASSERT_EQ(1u, actor->actions().size());
    EXPECT_EQ(step, actor->actions().front());
    EXPECT_FALSE(step->isCancelled());
}

// k_ptar_henstr_en (tar_m04aa): the PC walking to Trewin crosses tar04_henstr,
// whose OnEnter clears the PC's actions and has Hester start her scene. The
// OnEnter is not the walk's own script, so the walk is cleared with its
// conversation admission, and Hester's conversation can be admitted.
TEST(Action, clear_all_actions_from_a_trigger_crossed_by_the_executing_action_clears_it) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto actor = game.newCreature();
    auto listener = game.newCreature();
    auto walk = game.newAction<StartConversationAction>(
        listener, "", false, resource::ConversationType::Cinematic, false);
    walk->admit();
    ASSERT_TRUE(game.globalFade().dialogPending());
    auto step = game.newAction<ClearingStepAction>(/*foreign=*/true);
    actor->addAction(step);
    actor->addAction(walk);

    actor->update(1.0f);

    EXPECT_TRUE(actor->actions().empty());
    EXPECT_TRUE(step->isCancelled());
    EXPECT_TRUE(walk->isCancelled());
    EXPECT_FALSE(game.globalFade().dialogPending());
    EXPECT_TRUE(game.globalFade().admitDialog());
}

namespace {

// A single-use medpac: ActivateItem (spell subtype), one charge.
std::shared_ptr<Item> makeMedpac(Game &game, SpellType spell) {
    auto property = resource::Gff::Builder()
                        .field(resource::Gff::Field::newWord("PropertyName", static_cast<int>(ItemProperty::ActivateItem)))
                        .field(resource::Gff::Field::newWord("Subtype", static_cast<int>(spell)))
                        .field(resource::Gff::Field::newByte("CostTable", 3))
                        .field(resource::Gff::Field::newWord("CostValue", 1))
                        .build();
    auto gff = resource::Gff::Builder()
                   .field(resource::Gff::Field::newCExoString("Tag", "g_i_medeqpmnt01"))
                   .field(resource::Gff::Field::newInt("BaseItem", kTestPistol))
                   .field(resource::Gff::Field::newWord("StackSize", 1))
                   .field(resource::Gff::Field::newList("PropertiesList", {std::move(property)}))
                   .build();
    auto item = game.newItem();
    item->deserialize(*gff, SerializedIdentityContext::templateResource());
    return item;
}

std::shared_ptr<Spell> stubHealingSpell(TestEngine &engine, SpellType type) {
    auto spell = std::make_shared<Spell>();
    spell->type = type;
    spell->castTime = 1.0f;
    spell->conjTime = 1.0f;
    EXPECT_CALL(engine.gameModule().spells(), get(_)).Times(AnyNumber()).WillRepeatedly(Return(spell));
    return spell;
}

} // namespace

// K1 has one party inventory, which the player character holds: a companion
// the player leads is offered the party's medpacs, and using one takes it out
// of that inventory. Before, single-player offered a companion leader only
// its own items, so with Carth leading the party's medpacs were out of reach
// (the tar_m05aa raid wipe), and an item use from the shared inventory was
// dropped at the conjure step unless co-op or Smarter companions was on.
TEST(Action, a_companion_uses_a_medpac_from_the_shared_party_inventory) {
    TestEngine &engine = testEngine();
    ON_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .WillByDefault(Return(deflectionBaseItemsTable()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto spell = stubHealingSpell(engine, SpellType::LightSaberThrow);

    auto player = game.newCreature();
    auto carth = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    game.party().addAvailableMember(2, carth);
    game.party().addMember(2, carth);
    auto medpac = makeMedpac(game, SpellType::LightSaberThrow);
    player->addItem(medpac);
    ASSERT_EQ(1u, player->itemAttributes().healingSpells().size());

    EXPECT_EQ(player, game.party().sharedItemHolder(*carth));
    EXPECT_EQ(nullptr, game.party().sharedItemHolder(*player));
    auto offered = game.party().usableItemSpells(*carth, ItemAttributes::Kind::Healing);
    ASSERT_EQ(1u, offered.size());
    EXPECT_EQ(medpac, offered.begin()->first);

    auto action = game.newAction<CastSpellAtObjectAction>(spell, carth, std::optional<std::shared_ptr<Item>>(medpac));
    for (int i = 0; i < 10 && !action->isCompleted(); ++i) {
        action->execute(action, *carth, 1.0f);
        game.combat().update(2.0f);
    }
    EXPECT_TRUE(player->items().empty());
    EXPECT_TRUE(player->itemAttributes().healingSpells().empty());
}

// A creature outside the party has no claim on the party's items.
TEST(Action, a_creature_outside_the_party_is_not_offered_the_party_inventory) {
    TestEngine &engine = testEngine();
    ON_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .WillByDefault(Return(deflectionBaseItemsTable()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    stubHealingSpell(engine, SpellType::LightSaberThrow);

    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    player->addItem(makeMedpac(game, SpellType::LightSaberThrow));
    auto stranger = game.newCreature();

    EXPECT_EQ(nullptr, game.party().sharedItemHolder(*stranger));
    EXPECT_TRUE(game.party().usableItemSpells(*stranger, ItemAttributes::Kind::Healing).empty());
    EXPECT_EQ(1u, game.party().usableItemSpells(*player, ItemAttributes::Kind::Healing).size());
}

// GetIsPC is true for the party leader the player has switched to, as well as
// for the player character (tar02_openbase: T3-M4 leading opens the Sith base).
TEST(Action, get_is_pc_counts_the_companion_the_player_leads) {
    TestEngine &engine = testEngine();
    StubConsole console;
    for (bool t3Leads : {false, true}) {
        Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
        Routines routines(resource::GameID::KotOR, &game, &engine.services());
        routines.init();
        script::Routine &routine = routines.get(routines.getIndexByName("GetIsPC"));

        auto player = game.newCreature();
        auto t3 = game.newCreature();
        auto carth = game.newCreature();
        game.party().addAvailableMember(7, t3);
        game.party().addAvailableMember(2, carth);
        // The leader is the first member (what switchLeader rotates to).
        if (t3Leads) {
            game.party().addMember(7, t3);
            game.party().addMember(kNpcPlayer, player);
        } else {
            game.party().addMember(kNpcPlayer, player);
            game.party().addMember(7, t3);
        }
        game.party().setPlayer(player);
        game.party().addMember(2, carth);
        ASSERT_EQ(t3Leads ? t3 : player, game.party().getLeader());

        script::ExecutionContext ctx;
        ctx.routines = &routines;
        auto isPC = [&](const std::shared_ptr<Creature> &c) {
            return routine.invoke({script::Variable::ofObject(c->id())}, ctx).intValue;
        };
        EXPECT_EQ(1, isPC(player));
        EXPECT_EQ(t3Leads ? 1 : 0, isPC(t3));
        EXPECT_EQ(0, isPC(carth));
    }
}

// Force-cast animations (design note v14_programmed_effects, section 9).

TEST(CastAnimation, spells_2da_poses_pick_the_k1_cast_clips) {
    auto self = forceCastAnimations("hand", "self"); // Force Push, heal, most powers
    ASSERT_TRUE(self);
    EXPECT_EQ("castout2", self->conjure);
    EXPECT_EQ("castoutlp2", self->cast);
    auto choke = forceCastAnimations("dark", "dark");
    ASSERT_TRUE(choke);
    EXPECT_EQ("castout1", choke->conjure);
    EXPECT_EQ("castoutlp1", choke->cast);
    auto lightning = forceCastAnimations("hand", "dark"); // fear, horror: a hand conjure raises into the dark pose
    ASSERT_TRUE(lightning);
    EXPECT_EQ("castout1", lightning->conjure);
    EXPECT_EQ("castoutlp1", lightning->cast);
    auto storm = forceCastAnimations("hand", "up");
    ASSERT_TRUE(storm);
    EXPECT_EQ("castout3", storm->conjure);
    EXPECT_EQ("castoutlp3", storm->cast);
    auto deathField = forceCastAnimations("up", "up");
    ASSERT_TRUE(deathField);
    EXPECT_EQ("castout3", deathField->conjure);
    auto mixed = forceCastAnimations("dark", "self");
    ASSERT_TRUE(mixed);
    EXPECT_EQ("castout1", mixed->conjure) << "a dark conjure keeps its own raise";
    EXPECT_EQ("castoutlp2", mixed->cast);
    auto saber = forceCastAnimations("throw", "throw");
    ASSERT_TRUE(saber);
    EXPECT_EQ("throwsab", saber->conjure);
    EXPECT_EQ("throwsablp", saber->cast);
    EXPECT_TRUE(forceCastAnimations("HAND", "Self")) << "the table's case does not matter";
    EXPECT_FALSE(forceCastAnimations("hand", "jump"));
    EXPECT_FALSE(forceCastAnimations("", "monster"));
    EXPECT_FALSE(forceCastAnimations("", ""));
}

TEST(CastAnimation, items_keep_their_use_and_throw_clips_and_powers_take_poses) {
    auto medpac = castActionAnimations("hand", "self", /*fromItem=*/true, /*grenade=*/false, /*itemTargeting=*/false);
    EXPECT_EQ("inject", medpac.conjure) << "an item is used, whatever its spell row says";
    EXPECT_EQ("", medpac.cast);
    auto targeted = castActionAnimations("", "", true, false, true);
    EXPECT_EQ("activate", targeted.conjure);
    auto grenade = castActionAnimations("throw", "throw", true, true, true);
    EXPECT_EQ("throwgren", grenade.conjure);
    EXPECT_EQ("", grenade.cast);
    auto push = castActionAnimations("hand", "self", false, false, false);
    EXPECT_EQ("castout2", push.conjure);
    EXPECT_EQ("castoutlp2", push.cast);
    auto monster = castActionAnimations("hand", "monster", false, false, false);
    EXPECT_EQ("inject", monster.conjure) << "no pose named: the old clip";
    EXPECT_EQ("", monster.cast);
}

TEST(CastAnimation, a_fake_cast_holds_the_caster_for_conjure_then_cast_time) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto caster = game.newCreature();
    Spell spell;
    spell.type = SpellType::ForcePush;
    spell.conjAnim = "hand";
    spell.castAnim = "self";
    spell.conjTime = 0.17f;
    spell.castTime = 1.33f;
    FakeCast cast;
    float elapsed = 0.0f;
    bool done = false;
    while (!done && elapsed < 5.0f) {
        done = cast.update(*caster, &spell, glm::vec3(1.0f, 0.0f, 0.0f), engine.services(), 0.05f);
        elapsed += 0.05f;
        if (!done) {
            EXPECT_TRUE(caster->isMovementRestricted()) << "held while casting, at " << elapsed;
        }
    }
    EXPECT_TRUE(done);
    EXPECT_NEAR(1.55f, elapsed, 0.11f) << "conjtime + casttime";
    EXPECT_FALSE(caster->isMovementRestricted());
}
