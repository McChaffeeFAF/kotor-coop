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


// Rules ported piece by piece from the vrifftech fork's rules rewrite (bc1b6d7e9, with its
// follow-up 64f5cb984), rewritten for this fork (design note vrifftech_rules_triage).

#include <limits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"

#include "reone/game/action/attackobject.h"
#include "reone/game/combat.h"
#include "reone/game/combatfeedback.h"
#include "reone/game/d20/spell.h"
#include "reone/game/effect/forcebody.h"
#include "reone/game/effect/heal.h"
#include "reone/game/effect/healforcepoints.h"
#include "reone/game/effect/immunity.h"
#include "reone/game/effect/paralyze.h"
#include "reone/game/game.h"
#include "reone/game/messagelog.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/game/stringtokens.h"
#include "reone/resource/2da.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

class RulesPorts : public Test {
protected:
    void start(GameID id = GameID::KotOR) {
        _game.reset();
        _engine.init();
        _game = std::make_unique<Game>(id, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        EXPECT_CALL(_engine.resourceModule().strings(), getText(_)).Times(AnyNumber()).WillRepeatedly(Return(std::string()));
        text(1483, "<CUSTOM0> is immune to Paralysis");
        text(1480, "<CUSTOM0> is immune to critical hits");
        text(1495, "<CUSTOM0> : Healed <CUSTOM1> hit points.");
        text(1031, "<CUSTOM0> regains <CUSTOM1> Force Points.");
    }

    void TearDown() override {
        if (_game) {
            Mock::VerifyAndClearExpectations(&_engine.resourceModule().strings());
        }
    }

    void text(int strRef, const std::string &value) {
        EXPECT_CALL(_engine.resourceModule().strings(), getText(strRef)).Times(AnyNumber()).WillRepeatedly(Return(value));
    }

    std::shared_ptr<Creature> creature(const std::string &name) {
        auto result = _game->newCreature();
        result->setName(name);
        result->setMaxHitPoints(100);
        result->setCurrentHitPoints(100);
        return result;
    }

    std::shared_ptr<Creature> leader(const std::string &name) {
        auto result = creature(name);
        _game->party().addMember(kNpcPlayer, result);
        return result;
    }

    std::vector<std::string> lines() const {
        std::vector<std::string> result;
        for (const auto &entry : _game->messageLog().entries()) {
            result.push_back(entry.text);
        }
        return result;
    }

    std::shared_ptr<Effect> paralyzeFrom(const std::shared_ptr<Object> &creator) {
        auto effect = std::make_shared<ParalyzeEffect>();
        effect->setSaveFacingCreator(creator);
        return effect;
    }

    TestEngine _engine;
    StubConsole _console;
    std::unique_ptr<Game> _game;
};

} // namespace

// --- Combat feedback ----------------------------------------------------------------

TEST_F(RulesPorts, an_effect_refused_by_immunity_tells_the_immune_leader) {
    start();
    auto pc = leader("Revan");
    auto caster = creature("Sith");
    pc->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::Paralysis), DurationType::Permanent);

    pc->applyEffect(paralyzeFrom(caster), DurationType::Temporary, 6.0f);

    EXPECT_FALSE(pc->hasEffect(EffectType::Paralyze));
    EXPECT_THAT(lines(), ElementsAre("Revan is immune to Paralysis"));
}

TEST_F(RulesPorts, an_effect_refused_by_immunity_tells_its_creator_once) {
    start();
    auto pc = leader("Revan");
    auto droid = creature("Droid");
    droid->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::Paralysis), DurationType::Permanent);
    droid->applyEffect(paralyzeFrom(pc), DurationType::Temporary, 6.0f);
    EXPECT_THAT(lines(), ElementsAre("Droid is immune to Paralysis"));

    // Neither the target nor the creator is the leader: no line.
    auto other = creature("Other");
    droid->applyEffect(paralyzeFrom(other), DurationType::Temporary, 6.0f);
    EXPECT_EQ(1u, lines().size());
}

TEST_F(RulesPorts, immunity_types_without_a_line_of_their_own_report_nothing) {
    EXPECT_EQ(1483, effectImmunityStrRef(ImmunityType::Paralysis));
    EXPECT_EQ(1489, effectImmunityStrRef(ImmunityType::DroidConfused));
    EXPECT_EQ(1492, effectImmunityStrRef(ImmunityType::Slow));
    EXPECT_FALSE(effectImmunityStrRef(ImmunityType::Blindness));
    EXPECT_FALSE(effectImmunityStrRef(ImmunityType::AcDecrease));
}

TEST_F(RulesPorts, healing_names_the_hit_points_restored) {
    start();
    auto pc = leader("Revan");
    pc->setCurrentHitPoints(90);
    pc->applyEffect(std::make_shared<HealEffect>(25), DurationType::Instant);
    EXPECT_EQ(100, pc->currentHitPoints());
    EXPECT_THAT(lines(), ElementsAre("Revan : Healed 10 hit points."));

    // Nothing restored, nothing said.
    pc->applyEffect(std::make_shared<HealEffect>(5), DurationType::Instant);
    EXPECT_EQ(1u, lines().size());
}

TEST_F(RulesPorts, force_healing_tells_only_the_healed_creatures_player_in_tsl) {
    start(GameID::TSL);
    auto pc = leader("Exile");
    auto ally = creature("Atton");
    for (auto *member : {pc.get(), ally.get()}) {
        member->addTemporaryForcePoints(50);
        member->setCurrentForcePoints(10);
        ASSERT_EQ(10, member->currentForce());
    }

    ally->applyEffect(std::make_shared<HealForcePointsEffect>(5), DurationType::Instant);
    EXPECT_TRUE(lines().empty());
    pc->applyEffect(std::make_shared<HealForcePointsEffect>(5), DurationType::Instant);
    EXPECT_THAT(lines(), ElementsAre("Exile regains 5 Force Points."));
}

TEST_F(RulesPorts, kotor_shows_no_force_healing_line) {
    start(GameID::KotOR);
    auto pc = leader("Revan");
    pc->addTemporaryForcePoints(50);
    pc->setCurrentForcePoints(10);
    ASSERT_EQ(10, pc->currentForce());
    pc->applyEffect(std::make_shared<HealForcePointsEffect>(5), DurationType::Instant);
    EXPECT_TRUE(lines().empty());
}

TEST(RulesPortsLog, an_empty_line_is_not_written) {
    MessageLog log;
    log.add(MessageLog::kFeedbackMessageType, MessageLog::Style::Normal, "");
    EXPECT_TRUE(log.entries().empty());
    log.add(MessageLog::kFeedbackMessageType, MessageLog::Style::Normal, "x");
    EXPECT_EQ(1u, log.entries().size());
}

// --- String tokens 26 and 27 ----------------------------------------------------------

TEST(RulesPortsTokens, mouse_button_tokens_name_the_button_by_their_default) {
    auto row = [](int action, int def) {
        StringTokenRow r;
        r.actionCode = action;
        r.defaultStrRef = def;
        return r;
    };
    StringTokens tokens;
    tokens.add("L_CAPMouseButton", row(26, 1));
    tokens.add("r_mouse", row(26, 2));
    tokens.add("bad", row(26, 9));
    tokens.add("BountyValue", row(27, -1));
    auto tlk = [](int strRef) { return "#" + std::to_string(strRef); };

    StringTokenSubject k1;
    EXPECT_EQ("#48482 #48485 #0", tokens.resolve("<L_CAPMouseButton> <r_mouse> <bad>", {}, k1, tlk));
    EXPECT_EQ(kUnrecognizedToken, tokens.resolve("<BountyValue>", {}, k1, tlk));

    StringTokenSubject tsl;
    tsl.tsl = true;
    tsl.bounty = 2500;
    EXPECT_EQ("#48725 #48728", tokens.resolve("<L_CAPMouseButton> <r_mouse>", {}, tsl, tlk));
    EXPECT_EQ("Bounty = 2500 credits.", tokens.resolve("Bounty = <BountyValue> credits.", {}, tsl, tlk));
}

TEST_F(RulesPorts, the_bounty_token_reads_the_nar_shaddaa_global_in_tsl) {
    for (GameID id : {GameID::TSL, GameID::KotOR}) {
        start(id);
        TwoDA::Builder builder;
        builder.columns({"token", "actioncode", "default", "strref1", "strref2", "strref3", "strref4"});
        builder.row({"BountyValue", "27", "-1", "-1", "-1", "-1", "-1"});
        std::shared_ptr<TwoDA> table(builder.build());
        EXPECT_CALL(_engine.resourceModule().twoDas(), get("stringtokens")).Times(AnyNumber()).WillRepeatedly(Return(table));
        _game->setGlobalNumber("300NAR_Bounty", 7);
        EXPECT_EQ(id == GameID::TSL ? std::string("3500") : std::string(kUnrecognizedToken),
                  _game->substituteCustomTokens("<BountyValue>"));
        Mock::VerifyAndClearExpectations(&_engine.resourceModule().twoDas());
        _engine.resourceModule().twoDas().allowAnyLookup();
    }
}

// --- Cast costs -----------------------------------------------------------------------

TEST_F(RulesPorts, a_casts_force_point_cost_is_recorded_as_it_begins) {
    start(GameID::TSL);
    auto caster = creature("Exile");
    Spell power;
    power.type = static_cast<SpellType>(9);
    power.forcePoints = 10;
    Spell other;
    other.type = static_cast<SpellType>(10);
    other.forcePoints = 4;

    EXPECT_FALSE(caster->recordedSpellForcePointCost(power.type));
    caster->recordSpellForcePointCost(power);
    EXPECT_EQ(10, caster->recordedSpellForcePointCost(power.type).value_or(-1));

    // Force Body taken up after the cast began leaves the recorded cost.
    caster->applyEffect(std::make_shared<ForceBodyEffect>(0), DurationType::Temporary, 30.0f);
    ASSERT_EQ(5, caster->forcePowerCost(power));
    EXPECT_EQ(10, caster->recordedSpellForcePointCost(power.type).value_or(-1));

    // The next cast records its own spell; the last one is forgotten.
    caster->recordSpellForcePointCost(other);
    EXPECT_FALSE(caster->recordedSpellForcePointCost(power.type));
    EXPECT_EQ(2, caster->recordedSpellForcePointCost(other.type).value_or(-1));
}

// --- Combat rounds ----------------------------------------------------------------------

namespace {

constexpr float kTick = 1.0f / 30.0f;

void runCombat(Game &game, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        game.combat().update(kTick);
    }
}

} // namespace

TEST_F(RulesPorts, a_command_cleared_after_its_turn_lets_its_round_run_out) {
    start();
    auto attacker = creature("A");
    auto target = creature("B");
    auto attack = _game->newAction<AttackObjectAction>(target);
    auto reply = _game->newAction<AttackObjectAction>(attacker);
    const CombatRound &round = _game->combat().addAction(attack, *attacker);
    _game->combat().addAction(reply, *target);
    runCombat(*_game, 1);
    ASSERT_TRUE(round.canExecute(*attack)); // the attack takes its turn

    runCombat(*_game, 10);
    attack->markCancelled();
    runCombat(*_game, 1);

    // The round is still there: the attacker's side runs out, and the
    // partner keeps its command and its turn.
    ASSERT_EQ(1u, _game->combat().roundCount());
    EXPECT_TRUE(round.actions[0].abandoned);
    EXPECT_FALSE(reply->isCancelled());
    EXPECT_FALSE(round.canExecute(*attack));

    // A new attack waits for the old round to end.
    auto next = _game->newAction<AttackObjectAction>(target);
    const CombatRound &waiting = _game->combat().addAction(next, *attacker);
    EXPECT_EQ(&round, &waiting);
    EXPECT_FALSE(waiting.canExecute(*next));
    EXPECT_EQ(1u, _game->combat().roundCount());

    // At the end of the round (90 ticks from its start) the attacker starts afresh.
    runCombat(*_game, 90 - 12);
    EXPECT_EQ(CombatRound::Finished, round.actions[0].state);
    reply->complete();
    runCombat(*_game, 1);
    EXPECT_EQ(0u, _game->combat().roundCount());
    const CombatRound &fresh = _game->combat().addAction(next, *attacker);
    EXPECT_EQ(1u, _game->combat().roundCount());
    EXPECT_EQ(next, fresh.actions.front().action);
}

TEST_F(RulesPorts, a_command_cleared_before_its_turn_leaves_its_round_at_once) {
    start();
    auto attacker = creature("A");
    auto target = creature("B");
    auto attack = _game->newAction<AttackObjectAction>(target);
    _game->combat().addAction(attack, *attacker);
    runCombat(*_game, 1);
    attack->markCancelled();
    runCombat(*_game, 1);
    EXPECT_EQ(0u, _game->combat().roundCount());

    // Nothing to wait for: the next attack opens its own round.
    auto next = _game->newAction<AttackObjectAction>(target);
    const CombatRound &round = _game->combat().addAction(next, *attacker);
    EXPECT_EQ(next, round.actions.front().action);
}

TEST_F(RulesPorts, a_round_whose_target_is_down_ends_when_its_command_does) {
    start();
    auto attacker = creature("A");
    auto target = creature("B");
    target->setCurrentHitPoints(1);
    auto attack = _game->newAction<AttackObjectAction>(target);
    const CombatRound &round = _game->combat().addAction(attack, *attacker);
    runCombat(*_game, 1);
    ASSERT_TRUE(round.canExecute(*attack));
    target->damage(std::numeric_limits<int>::max(), attacker);
    ASSERT_TRUE(target->isDead());

    attack->complete();
    runCombat(*_game, 1);
    // No waiting out the round on a dead target.
    EXPECT_EQ(0u, _game->combat().roundCount());
}

TEST_F(RulesPorts, a_creature_that_is_down_leaves_its_side_of_the_round) {
    start();
    auto attacker = creature("A");
    auto target = creature("B");
    target->setCurrentHitPoints(1);
    auto attack = _game->newAction<AttackObjectAction>(target);
    auto reply = _game->newAction<AttackObjectAction>(attacker);
    const CombatRound &round = _game->combat().addAction(attack, *attacker);
    _game->combat().addAction(reply, *target);
    runCombat(*_game, 1);
    ASSERT_TRUE(round.canExecute(*attack));

    target->damage(std::numeric_limits<int>::max(), attacker);
    runCombat(*_game, 1);
    // The dead creature's command is given up; the killer's round runs on.
    EXPECT_TRUE(reply->isCancelled());
    EXPECT_EQ(CombatRound::Finished, round.actions[1].state);
    EXPECT_EQ(1u, _game->combat().roundCount());

    attack->complete();
    runCombat(*_game, 1);
    EXPECT_EQ(0u, _game->combat().roundCount());
}
