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

// Routines batch 1: talents, spell events, AI and scene routines.
// Behaviour notes: design note routines_batch1.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/action/movetoobject.h"
#include "reone/game/d20/feat.h"
#include "reone/game/d20/spell.h"
#include "reone/game/effect.h"
#include "reone/game/event.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/game/talent.h"
#include "reone/game/talents.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

std::shared_ptr<Spell> makeSpell(int id, uint32_t category, int maxCR, uint32_t exclusion = 0) {
    auto spell = std::make_shared<Spell>();
    spell->type = static_cast<SpellType>(id);
    spell->category = category;
    spell->maxCR = maxCR;
    spell->exclusion = exclusion;
    return spell;
}

std::shared_ptr<Feat> makeFeat(uint32_t category, int maxCR) {
    auto feat = std::make_shared<Feat>();
    feat->category = category;
    feat->maxCR = maxCR;
    return feat;
}

/** A random source that replays fixed answers, then returns min. */
struct ScriptedRandom {
    std::vector<int> answers;
    size_t next {0};
    std::vector<std::pair<int, int>> calls;

    TalentRandom fn() {
        return [this](int min, int max) {
            calls.emplace_back(min, max);
            int value = next < answers.size() ? answers[next++] : min;
            return std::clamp(value, min, max);
        };
    }
};

class TalentSearchTest : public Test {
protected:
    TalentSearchTest() :
        game(GameID::KotOR, "", testEngine().options(), testEngine().services(), console) {
    }

    void SetUp() override {
        creature = game.newCreature();
        ON_CALL(spells, get(_)).WillByDefault(Invoke([this](SpellType type) -> std::shared_ptr<Spell> {
            auto it = spellTable.find(static_cast<int>(type));
            return it != spellTable.end() ? it->second : nullptr;
        }));
        ON_CALL(feats, get(_)).WillByDefault(Invoke([this](FeatType type) -> std::shared_ptr<Feat> {
            auto it = featTable.find(static_cast<int>(type));
            return it != featTable.end() ? it->second : nullptr;
        }));
    }

    void knowSpell(int id, uint32_t category, int maxCR, uint32_t exclusion = 0) {
        spellTable[id] = makeSpell(id, category, maxCR, exclusion);
        creature->attributes().addSpell(static_cast<SpellType>(id));
    }

    void knowFeat(int id, uint32_t category, int maxCR) {
        featTable[id] = makeFeat(category, maxCR);
        creature->attributes().addFeat(static_cast<FeatType>(id));
    }

    std::optional<TalentChoice> find(const TalentQuery &query, ScriptedRandom &random) {
        return findCreatureTalent(*creature, spells, feats, query, random.fn());
    }

    StubConsole console;
    Game game;
    std::shared_ptr<Creature> creature;
    NiceMock<MockSpells> spells;
    NiceMock<MockFeats> feats;
    std::map<int, std::shared_ptr<Spell>> spellTable;
    std::map<int, std::shared_ptr<Feat>> featTable;
};

TalentQuery best(uint32_t category, int crMax = 20) {
    TalentQuery query;
    query.category = category;
    query.mode = TalentSearchMode::Best;
    query.crMax = crMax;
    return query;
}

TalentQuery random(uint32_t category) {
    TalentQuery query;
    query.category = category;
    query.mode = TalentSearchMode::Random;
    return query;
}

/** Routine table bound to a game, as the VM calls it. */
class RoutineHarness : boost::noncopyable {
public:
    RoutineHarness(GameID gameId) :
        _game(gameId, "", testEngine().options(), testEngine().services(), _console),
        _routines(gameId, &_game, &testEngine().services()) {
        _routines.init();
    }

    Variable call(const std::string &name, std::vector<Variable> args, std::vector<Argument> scriptArgs = {}) {
        Routine &routine = _routines.get(_routines.getIndexByName(name));
        ExecutionContext execution;
        execution.routines = &_routines;
        execution.args = std::move(scriptArgs);
        return routine.invoke(args, execution);
    }

    Variable callAs(const std::shared_ptr<Object> &caller, const std::string &name, std::vector<Variable> args,
                    std::vector<Argument> scriptArgs = {}) {
        scriptArgs.insert(scriptArgs.begin(), Argument(ArgKind::Caller, Variable::ofObject(caller->id())));
        return call(name, std::move(args), std::move(scriptArgs));
    }

    Game &game() { return _game; }
    StubConsole &console() { return _console; }

private:
    StubConsole _console;
    Game _game;
    Routines _routines;
};

/** Installs spell/feat rows on the shared test engine's mocks for one test. */
class ServiceTables : boost::noncopyable {
public:
    ServiceTables() {
        auto &module = testEngine().gameModule();
        ON_CALL(module.spells(), get(_)).WillByDefault(Invoke([this](SpellType type) -> std::shared_ptr<Spell> {
            auto it = spells.find(static_cast<int>(type));
            return it != spells.end() ? it->second : nullptr;
        }));
        ON_CALL(module.feats(), get(_)).WillByDefault(Invoke([this](FeatType type) -> std::shared_ptr<Feat> {
            auto it = feats.find(static_cast<int>(type));
            return it != feats.end() ? it->second : nullptr;
        }));
    }

    ~ServiceTables() {
        auto &module = testEngine().gameModule();
        Mock::VerifyAndClearExpectations(&module.spells());
        Mock::VerifyAndClearExpectations(&module.feats());
    }

    std::map<int, std::shared_ptr<Spell>> spells;
    std::map<int, std::shared_ptr<Feat>> feats;
};

constexpr int kForcePowerStun = 46;
constexpr int kForcePowerKill = 32;
constexpr int kForcePowerChoke = 9;
constexpr int kForcePowerWound = 50;
constexpr int kFeatPowerAttack = 28;
constexpr int kFeatMasterPowerAttack = 83;
constexpr int kFeatFlurry = 11;

std::shared_ptr<Talent> talentOf(const Variable &value) {
    return std::static_pointer_cast<Talent>(value.engineType);
}

} // namespace

// Categories: every query digit is F (any) or must be equal (K2 exe; K1 AI queries).
TEST(TalentCategory, matches_digit_by_digit_with_f_as_wildcard) {
    EXPECT_TRUE(talentCategoryMatches(0x1104, 0x1104));
    EXPECT_FALSE(talentCategoryMatches(0x1304, 0x1104)); // slam is not a melee feat
    EXPECT_TRUE(talentCategoryMatches(0x1808, 0xf8ff));  // self buff
    EXPECT_TRUE(talentCategoryMatches(0x2201, 0xf2ff));  // area debilitate
    EXPECT_FALSE(talentCategoryMatches(0x1301, 0xf2ff)); // choke: digit 3, not 2
    EXPECT_TRUE(talentCategoryMatches(0x1021, 0xff2f));
    EXPECT_TRUE(talentCategoryMatches(0x8000, 0x8000));
    EXPECT_FALSE(talentCategoryMatches(0x8101, 0x8000)); // lightning is not a droid item
    EXPECT_FALSE(talentCategoryMatches(0, 0xf8ff));      // uncategorised talents never match
    EXPECT_TRUE(talentCategoryMatches(0x1234, 0xffff));
}

// Best: highest maxcr within the pool; ties are broken by the random source.
TEST_F(TalentSearchTest, best_picks_the_highest_maxcr_and_breaks_ties_at_random) {
    knowFeat(kFeatPowerAttack, 0x1104, 3);
    knowFeat(kFeatFlurry, 0x1104, 3);
    knowFeat(kFeatMasterPowerAttack, 0x1104, 9);
    knowFeat(53, 0x1104, 9); // whirlwind

    ScriptedRandom pickFirst {{1, 0}};
    auto first = find(best(0x1104), pickFirst);
    ASSERT_TRUE(first);
    EXPECT_EQ(TalentType::Feat, first->type);
    EXPECT_EQ(53, first->id);
    // One coin flip for pool order, one tie-break among the two maxcr-9 feats.
    ASSERT_EQ(2u, pickFirst.calls.size());
    EXPECT_EQ(std::make_pair(0, 1), pickFirst.calls[1]);

    ScriptedRandom pickSecond {{1, 1}};
    auto second = find(best(0x1104), pickSecond);
    ASSERT_TRUE(second);
    EXPECT_EQ(kFeatMasterPowerAttack, second->id);
}

TEST_F(TalentSearchTest, best_respects_the_cr_cap) {
    knowFeat(kFeatPowerAttack, 0x1104, 3);
    knowFeat(kFeatMasterPowerAttack, 0x1104, 9);

    ScriptedRandom rng {{1}};
    auto choice = find(best(0x1104, 6), rng);
    ASSERT_TRUE(choice);
    EXPECT_EQ(kFeatPowerAttack, choice->id);
}

// Random: uniform over every match in the pool, regardless of maxcr.
TEST_F(TalentSearchTest, random_draws_from_every_match_in_the_pool) {
    knowSpell(kForcePowerStun, 0x1201, 3);
    knowSpell(kForcePowerKill, 0x1101, 9);
    knowSpell(30, 0x1201, 6); // horror

    ScriptedRandom rng {{0, 1}};
    auto choice = find(random(0xf2ff), rng);
    ASSERT_TRUE(choice);
    EXPECT_EQ(TalentType::Spell, choice->type);
    EXPECT_EQ(kForcePowerStun, choice->id); // candidates in id order: 30, 46
    ASSERT_EQ(2u, rng.calls.size());
    EXPECT_EQ(std::make_pair(0, 1), rng.calls[1]);
}

// The pool searched first is a coin flip; an empty pool falls back to the other.
TEST_F(TalentSearchTest, pool_order_is_random_with_fallback) {
    knowSpell(kForcePowerKill, 0x1101, 9);
    knowFeat(kFeatPowerAttack, 0x1101, 3); // contrived: same code in both pools

    ScriptedRandom spellsFirst {{0}};
    EXPECT_EQ(TalentType::Spell, find(best(0x1101), spellsFirst)->type);

    ScriptedRandom featsFirst {{1}};
    EXPECT_EQ(TalentType::Feat, find(best(0x1101), featsFirst)->type);

    ScriptedRandom fallback {{1}};
    auto choice = find(best(0xf2ff), fallback);
    EXPECT_FALSE(choice);
    knowSpell(kForcePowerStun, 0x1201, 3);
    ScriptedRandom fallback2 {{1}};
    choice = find(best(0xf2ff), fallback2);
    ASSERT_TRUE(choice);
    EXPECT_EQ(kForcePowerStun, choice->id);
}

TEST_F(TalentSearchTest, exclusions_skip_one_talent_or_a_whole_type) {
    knowSpell(kForcePowerChoke, 0x1101, 6);
    knowSpell(kForcePowerWound, 0x1101, 3);
    knowFeat(kFeatPowerAttack, 0x1101, 3);

    TalentQuery query = best(0x1101);
    query.excludeType = 0;
    query.excludeId = kForcePowerChoke;
    ScriptedRandom spellsFirst {{0}};
    EXPECT_EQ(kForcePowerWound, find(query, spellsFirst)->id);

    query.excludeId = -1; // TALENT_EXCLUDE_ALL_OF_TYPE
    ScriptedRandom spellsFirst2 {{0}};
    auto choice = find(query, spellsFirst2);
    ASSERT_TRUE(choice);
    EXPECT_EQ(TalentType::Feat, choice->type);
}

TEST_F(TalentSearchTest, inclusion_mask_filters_on_the_exclusion_column_only_when_set) {
    knowSpell(kForcePowerStun, 0x1201, 3, 0x02);

    ScriptedRandom rng {{0}};
    EXPECT_TRUE(find(random(0x1201), rng));

    TalentQuery query = random(0x1201);
    query.inclusion = 0x02;
    ScriptedRandom rng2 {{0}};
    EXPECT_FALSE(find(query, rng2));
}

TEST_F(TalentSearchTest, uncategorised_talents_are_never_returned) {
    knowFeat(43, 0, 0); // weapon proficiency: empty category
    ScriptedRandom rng {{1}};
    EXPECT_FALSE(find(best(0x0000), rng));
    ScriptedRandom rng2 {{1}};
    EXPECT_FALSE(find(random(0xffff), rng2));
}

TEST_F(TalentSearchTest, has_talent_checks_spells_feats_and_skills) {
    knowSpell(kForcePowerStun, 0x1201, 3);
    knowFeat(kFeatPowerAttack, 0x1104, 3);

    EXPECT_TRUE(creatureHasTalent(*creature, TalentType::Spell, kForcePowerStun));
    EXPECT_FALSE(creatureHasTalent(*creature, TalentType::Spell, kForcePowerKill));
    EXPECT_TRUE(creatureHasTalent(*creature, TalentType::Feat, kFeatPowerAttack));
    EXPECT_FALSE(creatureHasTalent(*creature, TalentType::Feat, kFeatFlurry));
    EXPECT_FALSE(creatureHasTalent(*creature, TalentType::Skill, 0));
    creature->attributes().setSkillRank(SkillType::ComputerUse, 2);
    EXPECT_TRUE(creatureHasTalent(*creature, TalentType::Skill, 0));
    EXPECT_FALSE(creatureHasTalent(*creature, TalentType::Invalid, 0));
    EXPECT_FALSE(findItemGrantingSpell(*creature, static_cast<SpellType>(64)));
}

// Routine level, both tables.
TEST(TalentRoutines, constructors_and_accessors) {
    ServiceTables tables;
    tables.spells[kForcePowerStun] = makeSpell(kForcePowerStun, 0x1201, 3);
    tables.feats[kFeatPowerAttack] = makeFeat(0x1104, 3);

    for (GameID id : {GameID::KotOR, GameID::TSL}) {
        RoutineHarness harness(id);
        auto spell = harness.call("TalentSpell", {Variable::ofInt(kForcePowerStun)});
        auto feat = harness.call("TalentFeat", {Variable::ofInt(kFeatPowerAttack)});
        auto skill = harness.call("TalentSkill", {Variable::ofInt(3)});

        EXPECT_EQ(0, harness.call("GetTypeFromTalent", {spell}).intValue);
        EXPECT_EQ(1, harness.call("GetTypeFromTalent", {feat}).intValue);
        EXPECT_EQ(2, harness.call("GetTypeFromTalent", {skill}).intValue);
        EXPECT_EQ(kForcePowerStun, harness.call("GetIdFromTalent", {spell}).intValue);
        EXPECT_EQ(3, harness.call("GetIdFromTalent", {skill}).intValue);
        EXPECT_EQ(0x1201, harness.call("GetCategoryFromTalent", {spell}).intValue);
        EXPECT_EQ(0x1104, harness.call("GetCategoryFromTalent", {feat}).intValue);
        EXPECT_EQ(0, harness.call("GetCategoryFromTalent", {skill}).intValue);
        EXPECT_EQ(1, harness.call("GetIsTalentValid", {skill}).intValue);
    }
}

TEST(TalentRoutines, creature_talent_best_random_and_has_talent) {
    ServiceTables tables;
    tables.spells[kForcePowerStun] = makeSpell(kForcePowerStun, 0x1201, 3);
    tables.feats[kFeatPowerAttack] = makeFeat(0x1104, 3);

    for (GameID id : {GameID::KotOR, GameID::TSL}) {
        RoutineHarness harness(id);
        auto npc = harness.game().newCreature();
        npc->attributes().addSpell(static_cast<SpellType>(kForcePowerStun));
        npc->attributes().addFeat(static_cast<FeatType>(kFeatPowerAttack));

        auto melee = harness.callAs(npc, "GetCreatureTalentBest", {Variable::ofInt(0x1104), Variable::ofInt(20)});
        ASSERT_TRUE(talentOf(melee));
        EXPECT_EQ(1, harness.call("GetIsTalentValid", {melee}).intValue);
        EXPECT_EQ(kFeatPowerAttack, harness.call("GetIdFromTalent", {melee}).intValue);
        EXPECT_EQ(1, harness.callAs(npc, "GetCreatureHasTalent", {melee}).intValue);

        auto debilitate = harness.callAs(npc, "GetCreatureTalentRandom", {Variable::ofInt(0xf2ff)});
        EXPECT_EQ(kForcePowerStun, harness.call("GetIdFromTalent", {debilitate}).intValue);
        EXPECT_EQ(0, harness.call("GetTypeFromTalent", {debilitate}).intValue);

        auto none = harness.callAs(npc, "GetCreatureTalentBest", {Variable::ofInt(0x8000), Variable::ofInt(20)});
        EXPECT_EQ(0, harness.call("GetIsTalentValid", {none}).intValue);
        EXPECT_EQ(-1, harness.call("GetIdFromTalent", {none}).intValue);
        EXPECT_EQ(0, harness.call("GetCategoryFromTalent", {none}).intValue);
        EXPECT_EQ(0, harness.callAs(npc, "GetCreatureHasTalent", {none}).intValue);

        auto other = harness.call("TalentFeat", {Variable::ofInt(kFeatFlurry)});
        EXPECT_EQ(0, harness.callAs(npc, "GetCreatureHasTalent", {other}).intValue);
    }
}

TEST(TalentRoutines, tsl_best_honours_exclusion_parameters) {
    ServiceTables tables;
    tables.feats[kFeatPowerAttack] = makeFeat(0x1104, 3);
    RoutineHarness harness(GameID::TSL);
    auto npc = harness.game().newCreature();
    npc->attributes().addFeat(static_cast<FeatType>(kFeatPowerAttack));

    auto excluded = harness.callAs(npc, "GetCreatureTalentBest",
                                   {Variable::ofInt(0x1104), Variable::ofInt(20), Variable::ofObject(npc->id()),
                                    Variable::ofInt(0), Variable::ofInt(1), Variable::ofInt(kFeatPowerAttack)});
    EXPECT_EQ(0, harness.call("GetIsTalentValid", {excluded}).intValue);
}

// Spell events.
TEST(SpellEvents, spell_cast_at_event_carries_caster_spell_and_harm) {
    RoutineHarness harness(GameID::KotOR);
    auto caster = harness.game().newCreature();
    auto event = harness.call("EventSpellCastAt", {Variable::ofObject(caster->id()), Variable::ofInt(kForcePowerStun), Variable::ofInt(1)});
    auto payload = std::static_pointer_cast<Event>(event.engineType);
    ASSERT_TRUE(payload);
    EXPECT_EQ(EventKind::SpellCastAt, payload->kind());
    EXPECT_EQ(std::vector<uint32_t> {caster->id()}, payload->objects());
    EXPECT_EQ((std::vector<int32_t> {kForcePowerStun, 1}), payload->integers());
}

TEST(SpellEvents, spell_cast_at_queries_read_the_event_arguments) {
    RoutineHarness harness(GameID::KotOR);
    auto caster = harness.game().newCreature();
    auto target = harness.game().newCreature();
    std::vector<Argument> eventArgs {
        {ArgKind::LastSpellCaster, Variable::ofObject(caster->id())},
        {ArgKind::LastSpell, Variable::ofInt(kForcePowerStun)},
        {ArgKind::LastSpellHarmful, Variable::ofInt(1)}};

    EXPECT_EQ(caster->id(), harness.callAs(target, "GetLastSpellCaster", {}, eventArgs).objectId);
    EXPECT_EQ(kForcePowerStun, harness.callAs(target, "GetLastSpell", {}, eventArgs).intValue);
    EXPECT_EQ(1, harness.callAs(target, "GetLastSpellHarmful", {}, eventArgs).intValue);

    EXPECT_EQ(kObjectInvalid, harness.callAs(target, "GetLastSpellCaster", {}).objectId);
    EXPECT_EQ(0, harness.callAs(target, "GetLastSpellHarmful", {}).intValue);

    std::vector<Argument> impactArgs {{ArgKind::SpellTargetObject, Variable::ofObject(target->id())}};
    EXPECT_EQ(target->id(), harness.callAs(caster, "GetSpellTargetObject", {}, impactArgs).objectId);
}

TEST(SpellEvents, attempted_spell_target_lasts_until_combat_is_cancelled) {
    RoutineHarness harness(GameID::KotOR);
    auto caster = harness.game().newCreature();
    auto target = harness.game().newCreature();
    EXPECT_EQ(kObjectInvalid, harness.callAs(caster, "GetAttemptedSpellTarget", {}).objectId);

    caster->setAttemptedSpellTarget(target);
    EXPECT_EQ(target->id(), harness.callAs(caster, "GetAttemptedSpellTarget", {}).objectId);
    EXPECT_EQ(target->id(), harness.call("GetSpellTarget", {Variable::ofObject(caster->id())}).objectId);

    harness.call("CancelCombat", {Variable::ofObject(caster->id())});
    EXPECT_EQ(kObjectInvalid, harness.callAs(caster, "GetAttemptedSpellTarget", {}).objectId);
    EXPECT_EQ(kObjectInvalid, harness.call("GetSpellTarget", {Variable::ofObject(caster->id())}).objectId);
}

TEST(SpellEvents, last_force_power_and_success) {
    RoutineHarness harness(GameID::KotOR);
    auto caster = harness.game().newCreature();
    EXPECT_EQ(-1, harness.callAs(caster, "GetLastForcePowerUsed", {}).intValue);
    EXPECT_EQ(0, harness.callAs(caster, "GetWasForcePowerSuccessful", {}).intValue);

    caster->recordForcePowerUsed(static_cast<SpellType>(kForcePowerStun));
    EXPECT_EQ(kForcePowerStun, harness.callAs(caster, "GetLastForcePowerUsed", {}).intValue);
    EXPECT_EQ(1, harness.callAs(caster, "GetWasForcePowerSuccessful", {}).intValue);
    EXPECT_EQ(ActionType::CastSpell, caster->getLastAttackAction());

    caster->setLastForcePowerFailed();
    EXPECT_EQ(0, harness.callAs(caster, "GetWasForcePowerSuccessful", {}).intValue);

    caster->cancelCombat();
    EXPECT_EQ(-1, harness.callAs(caster, "GetLastForcePowerUsed", {}).intValue);
}

namespace {

class SpellTaggedEffect : public Effect {
public:
    SpellTaggedEffect() : Effect(EffectType::Haste) {}
    void applyTo(Object &) override {}
};

} // namespace

TEST(SpellEvents, has_spell_effect) {
    RoutineHarness harness(GameID::KotOR);
    auto creature = harness.game().newCreature();
    auto effect = std::make_shared<SpellTaggedEffect>();
    effect->setSaveFacingSpellId(kForcePowerStun);
    creature->applyEffect(effect, DurationType::Permanent);
    ASSERT_EQ(1u, creature->effects().size());

    EXPECT_EQ(1, harness.call("GetHasSpellEffect", {Variable::ofInt(kForcePowerStun), Variable::ofObject(creature->id())}).intValue);
    EXPECT_EQ(0, harness.call("GetHasSpellEffect", {Variable::ofInt(kForcePowerKill), Variable::ofObject(creature->id())}).intValue);
}

// AI and events.
TEST(AiEvents, party_ai_style) {
    RoutineHarness harness(GameID::KotOR);
    EXPECT_EQ(0, harness.call("GetPartyAIStyle", {}).intValue);
    harness.call("SetPartyAIStyle", {Variable::ofInt(2)});
    EXPECT_EQ(2, harness.call("GetPartyAIStyle", {}).intValue);
}

TEST(AiEvents, blocking_and_found_enemy_default_invalid) {
    RoutineHarness harness(GameID::KotOR);
    auto walker = harness.game().newCreature();
    auto blocker = harness.game().newCreature();
    EXPECT_EQ(kObjectInvalid, harness.callAs(walker, "GetBlockingCreature", {}).objectId);
    EXPECT_EQ(kObjectInvalid, harness.callAs(walker, "GetFoundEnemyCreature", {}).objectId);

    walker->setBlockingCreature(blocker);
    walker->setFoundEnemyCreature(blocker);
    EXPECT_EQ(blocker->id(), harness.callAs(walker, "GetBlockingCreature", {}).objectId);
    EXPECT_EQ(blocker->id(), harness.callAs(walker, "GetFoundEnemyCreature", {}).objectId);
}

TEST(AiEvents, attempted_movement_target) {
    RoutineHarness harness(GameID::KotOR);
    auto walker = harness.game().newCreature();
    auto destination = harness.game().newCreature();
    EXPECT_EQ(kObjectInvalid, harness.callAs(walker, "GetAttemptedMovementTarget", {}).objectId);

    walker->addAction(harness.game().newAction<MoveToObjectAction>(destination, true, 1.0f));
    EXPECT_EQ(destination->id(), harness.callAs(walker, "GetAttemptedMovementTarget", {}).objectId);
}

TEST(AiEvents, total_damage_dealt_starts_at_zero) {
    RoutineHarness harness(GameID::KotOR);
    auto victim = harness.game().newCreature();
    EXPECT_EQ(0, harness.callAs(victim, "GetTotalDamageDealt", {}).intValue);
}

// Scene and dialogue.
TEST(SceneRoutines, pause_game) {
    RoutineHarness harness(GameID::KotOR);
    harness.call("PauseGame", {Variable::ofInt(1)});
    EXPECT_TRUE(harness.game().isPaused());
    harness.call("PauseGame", {Variable::ofInt(0)});
    EXPECT_FALSE(harness.game().isPaused());
}

TEST(SceneRoutines, no_clicks_for) {
    RoutineHarness harness(GameID::KotOR);
    EXPECT_FALSE(harness.game().areClicksBlocked());
    harness.call("NoClicksFor", {Variable::ofFloat(0.7f)});
    EXPECT_TRUE(harness.game().areClicksBlocked());
}

TEST(SceneRoutines, good_evil_value) {
    RoutineHarness harness(GameID::KotOR);
    auto creature = harness.game().newCreature();
    EXPECT_EQ(creature->goodEvil(), harness.call("GetGoodEvilValue", {Variable::ofObject(creature->id())}).intValue);
    EXPECT_EQ(-1, harness.call("GetGoodEvilValue", {Variable::ofObject(kObjectInvalid)}).intValue);
}

TEST(SceneRoutines, aur_post_string_reaches_the_console) {
    RoutineHarness harness(GameID::KotOR);
    harness.call("AurPostString", {Variable::ofString("hello"), Variable::ofInt(0), Variable::ofInt(15), Variable::ofFloat(2.0f)});
    ASSERT_FALSE(harness.console().lines.empty());
    EXPECT_THAT(harness.console().lines.back(), HasSubstr("hello"));
}

TEST(SceneRoutines, begin_conversation_without_a_dialog_returns_false) {
    RoutineHarness harness(GameID::KotOR);
    auto npc = harness.game().newCreature();
    EXPECT_EQ(0, harness.callAs(npc, "BeginConversation", {}).intValue);
    harness.callAs(npc, "ResetDialogState", {});
}
