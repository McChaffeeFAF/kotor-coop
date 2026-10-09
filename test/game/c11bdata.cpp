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

// Gameplay data K1 ships that the engine now reads, second part (campaign
// batch C11b, design note c11_data).

#include <algorithm>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"

#include "reone/game/action/trap.h"
#include "reone/game/action/useskill.h"
#include "reone/game/creaturestates.h"
#include "reone/game/d20/class.h"
#include "reone/game/effect/frightened.h"
#include "reone/game/effect/stunned.h"
#include "reone/game/gameclock.h"
#include "reone/game/object/placeable.h"
#include "reone/game/onhit.h"
#include "reone/game/presentationpointer.h"
#include "reone/game/stringtokens.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/game/traps.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/scene/collision.h"
#include "reone/scene/node/trigger.h"
#include "reone/script/executioncontext.h"
#include "reone/script/program.h"
#include "reone/script/routine.h"
#include "reone/script/variable.h"
#include "reone/system/randomutil.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;
using namespace testing;

namespace {

constexpr int kNpcPlayer = -1;
constexpr int kLocalFirst = 21;

std::shared_ptr<TwoDA> makeTable(
    const std::vector<std::string> &columns,
    const std::vector<std::vector<std::string>> &rows) {
    TwoDA::Builder builder;
    builder.columns(columns);
    for (const auto &row : rows) {
        builder.row(row);
    }
    return std::shared_ptr<TwoDA>(builder.build());
}

// The first rows of K1's traps.2da that the tests use.
std::shared_ptr<TwoDA> makeK1Traps() {
    std::vector<std::vector<std::string>> rows;
    for (int i = 0; i < 12; ++i) {
        rows.push_back({"TRAP_" + std::to_string(i), "k_trp_generic", "15", "20", "20", "1278",
                        "g_i_trapkit00" + std::to_string(i % 10), "", "1278", "v_mnfrag", "cb_gr_fragment"});
    }
    // Row 3, the minor fragmentation mine, as K1 ships it.
    rows[3] = {"TRAP_FRAGMENTATION_MINE_MINOR", "k_trp_generic", "15", "20", "20", "1281",
               "g_i_trapkit004", "", "1281", "v_mnfrag", "cb_gr_fragment"};
    return makeTable(
        {"label", "trapscript", "setdc", "detectdcmod", "disarmdcmod", "trapname", "resref",
         "iconresref", "name", "model", "explosionsound"},
        rows);
}

// A script that stores (routine result == expected) in a local number of the caller.
std::shared_ptr<ScriptProgram> probeProgram(
    const Routines &routines, const std::string &name,
    const std::string &routine, uint32_t expected, int localIndex) {
    auto program = std::make_shared<ScriptProgram>(name);
    program->add(Instruction::newACTION(routines.getIndexByName(routine), 0));
    program->add(Instruction::newCONSTO(static_cast<int>(expected)));
    program->add(Instruction(InstructionType::EQUALOO));
    program->add(Instruction::newCONSTI(localIndex));
    program->add(Instruction::newCONSTO(static_cast<int>(kObjectSelf)));
    program->add(Instruction::newACTION(routines.getIndexByName("SetLocalNumber"), 3));
    program->add(Instruction(InstructionType::RETN));
    return program;
}

class C11bTraps : public Test {
protected:
    void SetUp() override {
        _requests.clear();
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _game->initLocalServices();
        _routines = std::make_unique<Routines>(GameID::KotOR, _game.get(), &_engine.services());
        _routines->init();
        _engine.gameOptions().clientDifficulty = 1;

        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(_sceneGraph));
        // Real trigger shapes, so creatures walking in are detected.
        ON_CALL(_sceneGraph, newTrigger(_)).WillByDefault(Invoke([this](std::vector<glm::vec3> geometry) {
            return std::make_shared<TriggerSceneNode>(
                std::move(geometry), _sceneGraph, _engine.services().graphics,
                _engine.services().audio, _engine.services().resource);
        }));
        ON_CALL(_sceneGraph, testElevation(_, _)).WillByDefault(Invoke([](const glm::vec3 &position, Collision &collision) {
            collision.intersection = glm::vec3(position.x, position.y, 0.0f);
            collision.user = nullptr;
            return true;
        }));
        ON_CALL(_sceneGraph, testWalk(_, _, _, _)).WillByDefault(Return(false));
        EXPECT_CALL(_engine.resourceModule().twoDas(), get("traps"))
            .Times(AnyNumber())
            .WillRepeatedly(Return(makeK1Traps()));
        std::vector<std::vector<std::string>> baseItems(60, {"0x00002", "i_generic", "99"});
        EXPECT_CALL(_engine.resourceModule().twoDas(), get("baseitems"))
            .Times(AnyNumber())
            .WillRepeatedly(Return(makeTable({"equipableslots", "itemclass", "stacking"}, baseItems)));
        // Hostile unless both sides share the player's faction.
        EXPECT_CALL(reputes(), getReputation(_, _))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([](Faction source, Faction target) {
                return source == target ? 100 : 0;
            }));

        _area = _game->newArea();
        TestGameModule::setActiveModuleArea(*_game, _area);
    }

    void TearDown() override {
        Mock::VerifyAndClearExpectations(&_engine.resourceModule().twoDas());
        _engine.resourceModule().twoDas().allowAnyLookup();
    }

    MockReputes &reputes() {
        return static_cast<MockReputes &>(_engine.services().game.reputes);
    }

    void countRequests(const std::string &resRef) {
        EXPECT_CALL(_engine.resourceModule().scripts(), get(resRef))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this](const std::string &key) {
                _requests.push_back(key);
                return std::shared_ptr<ScriptProgram>();
            }));
    }

    void probe(const std::string &resRef, const std::string &routine, uint32_t expected) {
        auto program = probeProgram(*_routines, resRef, routine, expected, kLocalFirst);
        EXPECT_CALL(_engine.resourceModule().scripts(), get(resRef))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this, program](const std::string &key) {
                _requests.push_back(key);
                return program;
            }));
    }

    int requests(const std::string &resRef) const {
        return static_cast<int>(std::count(_requests.begin(), _requests.end(), resRef));
    }

    std::shared_ptr<Creature> addCreature(const std::string &tag, Faction faction, glm::vec3 position = glm::vec3(0.0f)) {
        auto creature = _game->newCreature();
        creature->setTag(tag);
        creature->setFaction(faction);
        creature->setPosition(position);
        creature->setMaxHitPoints(20);
        creature->setCurrentHitPoints(20);
        _area->add(creature);
        return creature;
    }

    std::shared_ptr<Creature> addPlayer(glm::vec3 position = glm::vec3(0.0f)) {
        auto player = addCreature("player", Faction::Player, position);
        _game->party().addMember(kNpcPlayer, player);
        _game->party().setPlayer(player);
        return player;
    }

    // A K1 trap trigger: a 2 × 2 m square at the origin, UTT fields as
    // g_t_trap004 (a minor fragmentation mine) has them, DCs 20/20.
    std::shared_ptr<Trigger> addTrap(std::vector<Gff::Field> extra = {}) {
        std::vector<Gff::Field> fields {
            Gff::Field::newInt("Type", 2),
            Gff::Field::newByte("TrapFlag", 1),
            Gff::Field::newByte("TrapType", 3),
            Gff::Field::newByte("TrapDetectable", 1),
            Gff::Field::newByte("TrapDetectDC", 20),
            Gff::Field::newByte("TrapDisarmable", 1),
            Gff::Field::newByte("DisarmDC", 20),
            Gff::Field::newByte("TrapOneShot", 1),
            Gff::Field::newDword("Faction", static_cast<uint32_t>(Faction::Trap))};
        // A test's field replaces the default of the same name.
        for (auto &field : extra) {
            fields.erase(std::remove_if(fields.begin(), fields.end(), [&field](const Gff::Field &existing) {
                             return existing.label == field.label;
                         }),
                         fields.end());
            fields.push_back(std::move(field));
        }
        auto builder = Gff::Builder();
        for (auto &field : fields) {
            builder.field(std::move(field));
        }
        std::vector<std::shared_ptr<Gff>> points;
        for (auto point : {glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f), glm::vec2(1.0f, 1.0f), glm::vec2(-1.0f, 1.0f)}) {
            points.push_back(Gff::Builder().type(3)
                                 .field(Gff::Field::newFloat("PointX", point.x))
                                 .field(Gff::Field::newFloat("PointY", point.y))
                                 .field(Gff::Field::newFloat("PointZ", 0.0f))
                                 .build());
        }
        builder.field(Gff::Field::newList("Geometry", std::move(points)));
        auto gff = builder.build();
        auto trap = _game->newTrigger();
        trap->deserialize(*gff, SerializedIdentityContext::templateResource());
        _area->add(trap);
        return trap;
    }

    std::shared_ptr<Item> addMine(Object &owner, int trapRow, int stack = 2) {
        std::vector<std::shared_ptr<Gff>> properties {
            Gff::Builder().type(0)
                .field(Gff::Field::newWord("PropertyName", kItemPropertyTrap))
                .field(Gff::Field::newWord("Subtype", trapRow))
                .build()};
        auto gff = Gff::Builder()
                       .field(Gff::Field::newCExoString("Tag", "g_i_trapkit004"))
                       .field(Gff::Field::newInt("BaseItem", 0))
                       .field(Gff::Field::newWord("StackSize", stack))
                       .field(Gff::Field::newList("PropertiesList", std::move(properties)))
                       .build();
        auto item = _game->newItem();
        item->deserialize(*gff, SerializedIdentityContext::templateResource());
        owner.addItem(item);
        return item;
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

    // Steps of 10 cm, testing the triggers after each as movement does.
    void walk(const std::shared_ptr<Creature> &creature, glm::vec2 dir, int steps) {
        for (int i = 0; i < steps; ++i) {
            creature->setPosition(creature->position() + glm::vec3(dir * 0.1f, 0.0f));
            TestGameModule::checkTriggers(*_area, creature);
        }
    }

    TestEngine _engine;
    StubConsole _console;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
    NiceMock<MockSceneGraph> _sceneGraph;
    std::shared_ptr<Area> _area;
    std::vector<std::string> _requests;
};

} // namespace

void reone::game::TestGameModule::checkTriggers(Area &area, const std::shared_ptr<Object> &object) {
    area.checkTriggersIntersection(object);
}

// Rules -----------------------------------------------------------------------

TEST(C11bTrapRules, disarm_succeeds_at_the_dc_and_sets_off_the_trap_below_dc_minus_ten) {
    EXPECT_EQ(TrapSkillOutcome::Success, resolveDisarm(20, 20));
    EXPECT_EQ(TrapSkillOutcome::Failure, resolveDisarm(10, 20));
    EXPECT_EQ(TrapSkillOutcome::Triggered, resolveDisarm(9, 20));
    // DC 66 and up cannot be beaten.
    EXPECT_EQ(TrapSkillOutcome::Failure, resolveDisarm(100, 66));
}

TEST(C11bTrapRules, recover_is_ten_harder_and_sets_off_the_trap_below_that_minus_five) {
    EXPECT_EQ(TrapSkillOutcome::Success, resolveRecover(30, 20));
    EXPECT_EQ(TrapSkillOutcome::Failure, resolveRecover(25, 20));
    EXPECT_EQ(TrapSkillOutcome::Triggered, resolveRecover(24, 20));
    EXPECT_EQ(TrapSkillOutcome::Failure, resolveRecover(100, 56));
}

TEST(C11bTrapRules, only_a_rolled_set_mine_fails_critically) {
    EXPECT_EQ(SetMineOutcome::Success, resolveSetMine(15, 15, true));
    EXPECT_EQ(SetMineOutcome::Failure, resolveSetMine(2, 15, true));
    EXPECT_EQ(SetMineOutcome::Failure, resolveSetMine(5, 15, false));
    EXPECT_EQ(SetMineOutcome::CriticalFailure, resolveSetMine(4, 15, false));
}

TEST(C11bTrapRules, skill_xp_is_level_times_ten_or_fifteen_for_a_hard_trap) {
    EXPECT_EQ(50, trapSkillXP(5, 24));
    EXPECT_EQ(75, trapSkillXP(5, 25));
}

TEST(C11bTrapRules, difficulty_shifts_every_dc_by_five) {
    EXPECT_EQ(-5, trapDifficultyShift(0));
    EXPECT_EQ(0, trapDifficultyShift(1));
    EXPECT_EQ(5, trapDifficultyShift(2));
}

TEST(C11bTrapRules, a_trap_fires_on_a_hostile_of_another_faction) {
    EXPECT_TRUE(trapFiresOn(10, Faction::Trap, Faction::Player));
    EXPECT_FALSE(trapFiresOn(11, Faction::Trap, Faction::Player));
    EXPECT_FALSE(trapFiresOn(0, Faction::Player, Faction::Player));
}

TEST(C11bTrapRules, detection_is_awareness_plus_d10_against_the_dc) {
    EXPECT_TRUE(trapDetectRollSucceeds(10, 10, 20));
    EXPECT_FALSE(trapDetectRollSucceeds(10, 9, 20));
}

// Firing ----------------------------------------------------------------------

TEST_F(C11bTraps, a_hostile_entering_runs_the_traps_2da_script_once) {
    auto trap = addTrap();
    auto player = addPlayer(glm::vec3(-3.0f, 0.0f, 0.0f));
    probe("k_trp_generic", "GetEnteringObject", player->id());

    walk(player, glm::vec2(1.0f, 0.0f), 30);

    EXPECT_EQ(1, requests("k_trp_generic"));
    EXPECT_EQ(1, trap->getLocalNumber(kLocalFirst));
    // One-shot: it is off, and walking back in does nothing.
    EXPECT_FALSE(trap->isTrap());
    walk(player, glm::vec2(-1.0f, 0.0f), 30);
    walk(player, glm::vec2(1.0f, 0.0f), 30);
    EXPECT_EQ(1, requests("k_trp_generic"));
}

TEST_F(C11bTraps, on_trap_triggered_replaces_the_2da_script) {
    auto trap = addTrap({Gff::Field::newResRef("OnTrapTriggered", "c11_ontrap")});
    auto player = addPlayer(glm::vec3(-3.0f, 0.0f, 0.0f));
    countRequests("c11_ontrap");
    countRequests("k_trp_generic");

    walk(player, glm::vec2(1.0f, 0.0f), 30);

    EXPECT_EQ(1, requests("c11_ontrap"));
    EXPECT_EQ(0, requests("k_trp_generic"));
}

TEST_F(C11bTraps, a_creature_of_the_traps_own_faction_does_not_set_it_off) {
    auto trap = addTrap();
    auto ally = addCreature("trapper", Faction::Trap, glm::vec3(-3.0f, 0.0f, 0.0f));
    countRequests("k_trp_generic");

    walk(ally, glm::vec2(1.0f, 0.0f), 30);

    EXPECT_EQ(0, requests("k_trp_generic"));
    EXPECT_TRUE(trap->isTrap());
}

TEST_F(C11bTraps, the_traps_key_held_by_the_party_disarms_it_instead) {
    auto trap = addTrap({Gff::Field::newCExoString("KeyName", "trapkey"),
                         Gff::Field::newResRef("OnDisarm", "c11_disarmed")});
    auto player = addPlayer(glm::vec3(-3.0f, 0.0f, 0.0f));
    auto key = _game->newItem();
    key->setTag("trapkey");
    player->addItem(key);
    countRequests("k_trp_generic");
    probe("c11_disarmed", "GetLastDisarmed", player->id());

    walk(player, glm::vec2(1.0f, 0.0f), 30);

    EXPECT_EQ(0, requests("k_trp_generic"));
    EXPECT_EQ(1, requests("c11_disarmed"));
    EXPECT_EQ(1, trap->getLocalNumber(kLocalFirst));
    EXPECT_FALSE(trap->isTrap());
}

// Detection -------------------------------------------------------------------

TEST_F(C11bTraps, a_party_member_near_the_trap_detects_it_for_the_whole_party) {
    auto trap = addTrap();
    auto player = addPlayer(glm::vec3(2.5f, 0.0f, 0.0f));
    auto companion = addCreature("carth", Faction::Player, glm::vec3(30.0f, 0.0f, 0.0f));
    _game->party().addAvailableMember(0, companion);
    _game->party().addMember(0, companion);
    player->attributes().setSkillRank(SkillType::Awareness, 20); // 20 + d10 beats DC 20
    EXPECT_FALSE(trap->isSelectable());

    updateTrapDetection(*_game, *_area);

    EXPECT_TRUE(trap->isTrapDetectedByParty());
    EXPECT_TRUE(trap->isSelectable());
    EXPECT_EQ(1, call("GetTrapDetectedBy", {Variable::ofObject(trap->id()), Variable::ofObject(companion->id())}).intValue);
    EXPECT_EQ(trap->id(), call("GetLastTrapDetected", {Variable::ofObject(player->id())}).objectId);
    EXPECT_EQ(trap->id(), call("GetNearestTrapToObject", {Variable::ofObject(companion->id()), Variable::ofInt(1)}).objectId);
    // The Disable and Recover actions are offered on it.
    auto actions = _game->module()->getContextActions(trap);
    ASSERT_EQ(2u, actions.size());
    EXPECT_EQ(ActionType::DisableTrap, actions[0].type);
    EXPECT_EQ(ActionType::RecoverTrap, actions[1].type);
}

TEST_F(C11bTraps, nobody_detects_a_trap_out_of_range_or_undetectable) {
    auto far = addTrap();
    auto hidden = addTrap({Gff::Field::newByte("TrapDetectable", 0)});
    auto player = addPlayer(glm::vec3(3.5f, 0.0f, 0.0f));
    player->attributes().setSkillRank(SkillType::Awareness, 40);

    updateTrapDetection(*_game, *_area);
    EXPECT_FALSE(far->isTrapDetectedByParty());

    player->setPosition(glm::vec3(0.0f));
    updateTrapDetection(*_game, *_area);
    EXPECT_TRUE(far->isTrapDetectedByParty());
    EXPECT_FALSE(hidden->isTrapDetectedByParty());
}

TEST_F(C11bTraps, a_low_awareness_never_beats_a_high_dc) {
    auto trap = addTrap({Gff::Field::newByte("TrapDetectDC", 40)});
    auto player = addPlayer(glm::vec3(0.0f));
    player->attributes().setSkillRank(SkillType::Awareness, 5);
    for (int i = 0; i < 20; ++i) {
        updateTrapDetection(*_game, *_area);
    }
    EXPECT_FALSE(trap->isTrapDetectedByParty());
}

// Disarm, recover, flag -------------------------------------------------------

TEST_F(C11bTraps, disarming_out_of_combat_takes_twenty_and_awards_xp) {
    auto trap = addTrap({Gff::Field::newResRef("OnDisarm", "c11_disarmed")});
    auto player = addPlayer(glm::vec3(0.0f));
    player->attributes().setSkillRank(SkillType::Demolitions, 0); // 20 + 0 against DC 20
    countRequests("c11_disarmed");
    countRequests("k_trp_generic");
    int xp = _game->party().xp();

    auto action = _game->newAction<TrapSkillAction>(ActionType::DisableTrap, trap);
    action->execute(action, *player, 0.1f);

    EXPECT_TRUE(action->isCompleted());
    EXPECT_EQ(1, requests("c11_disarmed"));
    EXPECT_EQ(0, requests("k_trp_generic"));
    EXPECT_FALSE(trap->isTrap());
    int level = player->attributes().getAggregateLevel();
    EXPECT_EQ(xp + trapSkillXP(level, 20), _game->party().xp());
}

TEST_F(C11bTraps, disarm_dc_is_the_triggers_own_not_the_2da_mod) {
    // traps.2da row 3 says disarmdcmod 20; the UTT's DisarmDC 31 is the DC
    // (owner decision, design note k2_mismatches). 20 + 10 fails but does not set it off.
    auto trap = addTrap({Gff::Field::newByte("DisarmDC", 31)});
    auto player = addPlayer(glm::vec3(0.0f));
    player->attributes().setSkillRank(SkillType::Demolitions, 10);
    countRequests("k_trp_generic");
    EXPECT_EQ(31, call("GetTrapDisarmDC", {Variable::ofObject(trap->id())}).intValue);
    EXPECT_EQ(20, call("GetTrapDetectDC", {Variable::ofObject(trap->id())}).intValue);

    auto action = _game->newAction<TrapSkillAction>(ActionType::DisableTrap, trap);
    action->execute(action, *player, 0.1f);

    EXPECT_TRUE(trap->isTrap());
    EXPECT_EQ(0, requests("k_trp_generic"));
}

TEST_F(C11bTraps, a_badly_failed_disarm_sets_the_trap_off_on_the_disarmer) {
    auto trap = addTrap({Gff::Field::newByte("DisarmDC", 45)});
    auto player = addPlayer(glm::vec3(0.0f));
    probe("k_trp_generic", "GetEnteringObject", player->id());

    auto action = _game->newAction<UseSkillAction>(SkillType::Demolitions, trap);
    action->execute(action, *player, 0.1f);

    EXPECT_EQ(1, requests("k_trp_generic"));
    EXPECT_EQ(1, trap->getLocalNumber(kLocalFirst));
}

TEST_F(C11bTraps, recovering_gives_the_traps_kit) {
    auto trap = addTrap();
    auto player = addPlayer(glm::vec3(0.0f));
    player->attributes().setSkillRank(SkillType::Demolitions, 10); // 30 against 20 + 10
    EXPECT_CALL(_engine.resourceModule().gffs(), get("g_i_trapkit004", ResType::Uti)).Times(AtLeast(1));

    auto action = _game->newAction<TrapSkillAction>(ActionType::RecoverTrap, trap);
    action->execute(action, *player, 0.1f);

    EXPECT_FALSE(trap->isTrap());
}

TEST_F(C11bTraps, flagging_marks_the_trap) {
    auto trap = addTrap();
    auto player = addPlayer(glm::vec3(0.0f));
    auto action = _game->newAction<UseSkillAction>(
        SkillType::Demolitions, trap, static_cast<int>(SubSkill::FlagTrap));
    action->execute(action, *player, 0.1f);
    EXPECT_EQ(1, call("GetTrapFlagged", {Variable::ofObject(trap->id())}).intValue);
    EXPECT_TRUE(trap->isTrap());
}

// Routines --------------------------------------------------------------------

TEST_F(C11bTraps, trap_routines_read_the_trigger) {
    auto trap = addTrap({Gff::Field::newCExoString("KeyName", "trapkey")});
    auto plain = _game->newTrigger();
    Variable trapArg = Variable::ofObject(trap->id());

    EXPECT_EQ(3, call("GetTrapBaseType", {trapArg}).intValue);
    EXPECT_EQ(1, call("GetIsTrapped", {trapArg}).intValue);
    EXPECT_EQ(0, call("GetIsTrapped", {Variable::ofObject(plain->id())}).intValue);
    EXPECT_EQ(1, call("GetTrapDetectable", {trapArg}).intValue);
    EXPECT_EQ(1, call("GetTrapDisarmable", {trapArg}).intValue);
    EXPECT_EQ(1, call("GetTrapOneShot", {trapArg}).intValue);
    EXPECT_EQ("trapkey", call("GetTrapKeyTag", {trapArg}).strValue);
    EXPECT_EQ(kObjectInvalid, call("GetTrapCreator", {trapArg}).objectId);

    call("SetTrapDisabled", {trapArg});
    EXPECT_EQ(0, call("GetIsTrapped", {trapArg}).intValue);
    // The script of a fired one-shot trap still reads its base type.
    EXPECT_EQ(3, call("GetTrapBaseType", {trapArg}).intValue);
}

TEST_F(C11bTraps, set_trap_detected_by_a_pc_shows_it_to_the_party) {
    auto trap = addTrap();
    auto player = addPlayer(glm::vec3(10.0f, 0.0f, 0.0f));
    EXPECT_EQ(1, call("SetTrapDetectedBy", {Variable::ofObject(trap->id()), Variable::ofObject(player->id())}).intValue);
    EXPECT_TRUE(trap->isTrapDetectedByParty());
}

// Mines -----------------------------------------------------------------------

TEST_F(C11bTraps, a_mine_item_names_its_traps_row_by_its_first_property) {
    auto player = addPlayer();
    auto mine = addMine(*player, 3);
    EXPECT_EQ(3, mineTrapRow(*mine));
    auto plain = _game->newItem();
    EXPECT_EQ(-1, mineTrapRow(*plain));
}

TEST_F(C11bTraps, a_mine_laid_by_the_player_is_set_off_by_a_hostile_walking_in) {
    auto player = addPlayer(glm::vec3(0.0f));
    player->attributes().setSkillRank(SkillType::Demolitions, 4);
    auto mine = addMine(*player, 3, 2);
    auto hostile = addCreature("sith", Faction::Hostile1, glm::vec3(10.0f, 1.0f, 0.0f));
    probe("k_trp_generic", "GetEnteringObject", hostile->id());

    auto action = _game->newAction<SetTrapAction>(mine, glm::vec3(0.0f, 1.0f, 0.0f));
    for (int i = 0; i < 100 && !action->isCompleted(); ++i) {
        action->execute(action, *player, 0.1f);
    }
    ASSERT_TRUE(action->isCompleted());
    // Take 20 + 4 = 24 against setdc 15: laid, one mine used up.
    EXPECT_EQ(1, mine->stackSize());
    std::shared_ptr<Trigger> laid;
    for (const auto &object : _area->getObjectsByType(ObjectType::Trigger)) {
        laid = std::static_pointer_cast<Trigger>(object);
    }
    ASSERT_TRUE(laid);
    EXPECT_TRUE(laid->isTrap());
    EXPECT_EQ(Faction::Player, laid->faction());
    EXPECT_EQ(player->id(), call("GetTrapCreator", {Variable::ofObject(laid->id())}).objectId);
    EXPECT_EQ(24 + 20, laid->trapFields().detectDC);
    EXPECT_EQ(24 + 20, laid->trapFields().disarmDC);
    EXPECT_TRUE(laid->isTrapDetectedByParty());
    ASSERT_EQ(4u, laid->geometry().size());
    EXPECT_FLOAT_EQ(4.0f, laid->geometry()[1].x - laid->geometry()[0].x);

    // The setter walking over it does nothing; the hostile sets it off.
    walk(player, glm::vec2(0.0f, 1.0f), 50);
    EXPECT_EQ(0, requests("k_trp_generic"));
    walk(hostile, glm::vec2(-1.0f, 0.0f), 100);
    EXPECT_EQ(1, requests("k_trp_generic"));
    EXPECT_EQ(1, laid->getLocalNumber(kLocalFirst));
    EXPECT_FALSE(laid->isTrap());
}

TEST_F(C11bTraps, an_ordinary_set_mine_failure_keeps_the_item) {
    auto player = addPlayer(glm::vec3(0.0f));
    player->attributes().setSkillRank(SkillType::Demolitions, 0);
    auto mine = addMine(*player, 3, 1);
    _engine.gameOptions().clientDifficulty = 2; // setdc 15 + 5 = 20: 20 + 0 still sets it
    EXPECT_TRUE(setMine(*_game, _engine.services(), *player, *mine, glm::vec3(0.0f)));
    auto second = addMine(*player, 12, 1);
    // Row 12 exists only as a default row with setdc 15; make it unreachable.
    EXPECT_CALL(_engine.resourceModule().twoDas(), get("traps"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeTable({"trapscript", "setdc", "detectdcmod", "disarmdcmod", "trapname", "resref", "model"},
                                         std::vector<std::vector<std::string>>(13, {"k_trp_generic", "125", "140", "130", "1", "", "v_mnfrag"}))));
    EXPECT_FALSE(setMine(*_game, _engine.services(), *player, *second, glm::vec3(0.0f)));
    EXPECT_EQ(1, second->stackSize());
    EXPECT_EQ(player->id(), second->owner());
}

// Item properties: on hit ---------------------------------------------------

namespace {

class C11bOnHit : public C11bTraps {
protected:
    void SetUp() override {
        C11bTraps::SetUp();
        EXPECT_CALL(_engine.resourceModule().twoDas(), get("iprp_onhit"))
            .Times(AnyNumber())
            .WillRepeatedly(Return(makeTable({"name", "label", "cost", "param1resref"},
                                             {{"885", "Sleep", "2", "1"}, {"886", "Stun", "1", "1"}, {"887", "Paralyze", "2", "1"},
                                              {"888", "Confusion", "1", "1"}, {"891", "Fear", "1", "1"}, {"893", "Slow", "1", "1"},
                                              {"902", "AbilityDrain", "2.5", "2"}, {"903", "ItemPoison", "2", "10"},
                                              {"905", "SlayRG", "3", "5"}, {"906", "SlayAG", "4", "3"}, {"1087", "Instant_Death", "", ""}})));
        EXPECT_CALL(_engine.resourceModule().twoDas(), get("iprp_onhitdur"))
            .Times(AnyNumber())
            .WillRepeatedly(Return(makeTable({"name", "label", "cost", "effectchance", "durationrounds"},
                                             {{"806", "25%", "", "25", "2"}, {"812", "100%", "", "100", "2"}})));
        std::vector<std::vector<std::string>> costTables(26, {"", "", "0"});
        costTables[25] = {"IPRP_ONHITDC", "OnHitDC_saves", "0"};
        EXPECT_CALL(_engine.resourceModule().twoDas(), get("iprp_costtable"))
            .Times(AnyNumber())
            .WillRepeatedly(Return(makeTable({"name", "label", "clientload"}, costTables)));
        EXPECT_CALL(_engine.resourceModule().twoDas(), get("iprp_onhitdc"))
            .Times(AnyNumber())
            .WillRepeatedly(Return(makeTable({"name", "label", "value"},
                                             {{"1090", "10", "10"}, {"1094", "100", "100"}})));
    }

    std::shared_ptr<Item> onHitWeapon(int subtype, int param1Value, int costValue) {
        std::vector<std::shared_ptr<Gff>> properties {
            Gff::Builder().type(0)
                .field(Gff::Field::newWord("PropertyName", 32))
                .field(Gff::Field::newWord("Subtype", subtype))
                .field(Gff::Field::newByte("CostTable", 25))
                .field(Gff::Field::newWord("CostValue", costValue))
                .field(Gff::Field::newByte("Param1", 1))
                .field(Gff::Field::newByte("Param1Value", param1Value))
                .build()};
        auto gff = Gff::Builder()
                       .field(Gff::Field::newCExoString("Tag", "stunsaber"))
                       .field(Gff::Field::newInt("BaseItem", 0))
                       .field(Gff::Field::newList("PropertiesList", std::move(properties)))
                       .build();
        auto item = _game->newItem();
        item->deserialize(*gff, SerializedIdentityContext::templateResource());
        return item;
    }
};

} // namespace

TEST_F(C11bOnHit, a_timed_kind_reads_chance_and_duration_and_the_dc_table) {
    auto rule = onHitRule(static_cast<int>(OnHitKind::Stun));
    ASSERT_TRUE(rule);
    EXPECT_EQ(3, rule->save);
    EXPECT_EQ(SavingThrowType::MindAffecting, rule->saveType);
    OnHitRoll roll = resolveOnHitRoll(_engine.services().resource.twoDas, 1, 0, 1, *rule);
    EXPECT_EQ(25, roll.chance);
    EXPECT_EQ(2, roll.durationRounds);
    EXPECT_EQ(100, roll.saveDC);
    // A cost value past the table falls back to DC 20.
    EXPECT_EQ(20, resolveOnHitRoll(_engine.services().resource.twoDas, 1, 0, 9, *rule).saveDC);
    // Slay racial group never fires.
    EXPECT_EQ(0, onHitRule(static_cast<int>(OnHitKind::SlayRacialGroup))->chance);
}

TEST_F(C11bOnHit, a_certain_stun_the_target_cannot_save_against_stuns_it) {
    auto attacker = addCreature("attacker", Faction::Hostile1);
    auto target = addCreature("target", Faction::Player);
    auto weapon = onHitWeapon(static_cast<int>(OnHitKind::Stun), 1, 1); // 100 %, 2 rounds, DC 100

    applyOnHitProperties(*_game, _engine.services(), *attacker, *weapon, *target);

    EXPECT_TRUE(target->hasEffect(EffectType::Stunned));
    for (const auto &effect : target->effects()) {
        if (effect.effect && effect.effect->type() == EffectType::Stunned) {
            EXPECT_FLOAT_EQ(6.0f, effect.duration);
        }
    }
}

TEST_F(C11bOnHit, instant_death_and_item_poison_allow_no_save) {
    auto attacker = addCreature("attacker", Faction::Hostile1);
    auto poisoned = addCreature("poisoned", Faction::Player);
    // poison.2da row 0 with a save no one makes.
    EXPECT_CALL(_engine.resourceModule().twoDas(), get("poison"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeTable({"label", "dc_save", "duration", "period", "dam_hp"}, {{"Mild", "100", "30", "6", "1"}})));
    applyOnHitProperties(*_game, _engine.services(), *attacker, *onHitWeapon(static_cast<int>(OnHitKind::ItemPoison), 0, 0), *poisoned);
    EXPECT_TRUE(poisoned->hasEffect(EffectType::Poison));

    auto killed = addCreature("killed", Faction::Player);
    applyOnHitProperties(*_game, _engine.services(), *attacker, *onHitWeapon(static_cast<int>(OnHitKind::InstantDeath), 0, 0), *killed);
    EXPECT_TRUE(killed->isDead());
}

TEST_F(C11bOnHit, a_timed_kind_without_a_duration_row_never_fires) {
    auto attacker = addCreature("attacker", Faction::Hostile1);
    auto target = addCreature("target", Faction::Player);
    applyOnHitProperties(*_game, _engine.services(), *attacker, *onHitWeapon(static_cast<int>(OnHitKind::Stun), 7, 1), *target);
    EXPECT_FALSE(target->hasEffect(EffectType::Stunned));
}

// modulesave: deletesavegrouponenter ------------------------------------------

TEST(C11bModuleSave, entering_a_module_drops_its_delete_groups_cached_states) {
    TwoDA::Builder builder;
    builder.columns({"modulename", "includeinsave", "autosaveonenter", "areaname", "savegroup", "deletesavegrouponenter"});
    builder.row("end_m01aa", {"end_m01aa", "1", "force", "10221", "1", ""});
    builder.row("end_m01ab", {"end_m01ab", "1", "force", "41920", "1", ""});
    builder.row("tar_m02aa", {"tar_m02aa", "1", "force", "1", "2", ""});
    builder.row("tar_m02af", {"tar_m02af", "1", "no", "35891", "2", "1"});
    std::shared_ptr<TwoDA> table(builder.build());

    EXPECT_EQ((std::vector<std::string> {"end_m01aa", "end_m01ab"}), modulesDeletedOnEnter(*table, "TAR_M02AF"));
    EXPECT_TRUE(modulesDeletedOnEnter(*table, "tar_m02aa").empty());
    EXPECT_TRUE(modulesDeletedOnEnter(*table, "unknown").empty());
}

// The IFO clock -----------------------------------------------------------------

TEST(C11bClock, calendar_days_count_twelve_months_of_twenty_eight) {
    EXPECT_EQ(0u, calendarDayIndex(0, 1, 1));
    EXPECT_EQ(28u * 5u, calendarDayIndex(0, 6, 1));
    EXPECT_EQ((1372u * 12u + 5u) * 28u, calendarDayIndex(1372, 6, 1));
}

TEST(C11bClock, day_state_splits_at_dawn_and_dusk) {
    EXPECT_TRUE(dayStateAt(6, 6, 18).dawn);
    EXPECT_TRUE(dayStateAt(13, 6, 18).day);
    EXPECT_TRUE(dayStateAt(18, 6, 18).dusk);
    EXPECT_TRUE(dayStateAt(23, 6, 18).night);
    EXPECT_TRUE(dayStateAt(2, 6, 18).night);
    // Equal hours: always day.
    DayState always = dayStateAt(0, 0, 0);
    EXPECT_TRUE(always.day);
    EXPECT_FALSE(always.dawn || always.dusk || always.night);
}

TEST_F(C11bTraps, set_time_only_moves_forward) {
    uint32_t hourMs = _game->minutesPerHour() * 60000u;
    call("SetTime", {Variable::ofInt(13), Variable::ofInt(0), Variable::ofInt(0), Variable::ofInt(0)});
    EXPECT_EQ(13, call("GetTimeHour", {}).intValue);
    uint32_t day = _game->worldTimeDay();
    call("SetTime", {Variable::ofInt(5), Variable::ofInt(0), Variable::ofInt(0), Variable::ofInt(0)});
    EXPECT_EQ(5, call("GetTimeHour", {}).intValue);
    EXPECT_EQ(day + 1, _game->worldTimeDay());
    EXPECT_EQ(5u * hourMs, _game->worldTimeOfDay());
}

// statescripts, removefxondeath, formations, cls_spgn_jedi ------------------

TEST_F(C11bTraps, a_frightened_creature_runs_the_fear_state_script) {
    auto creature = addCreature("coward", Faction::Hostile1);
    EXPECT_FALSE(stateScriptRow(*creature));
    creature->applyEffect(_game->newEffect<FrightenedEffect>(), DurationType::Temporary, 6.0f);
    EXPECT_EQ(2, stateScriptRow(*creature).value_or(-1));

    EXPECT_CALL(_engine.resourceModule().twoDas(), get("statescripts"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeTable({"statename", "scriptname"},
                                         {{"Normal", ""}, {"SW_PHYCHIC_STATIC", "k_sup_static"}, {"SW_FEAR", "k_sup_fear"}})));
    EXPECT_EQ("k_sup_fear", stateScriptName(_engine.services().resource.twoDas, 2));
}

TEST_F(C11bTraps, death_removes_all_but_equipped_innate_and_listed_effects) {
    EXPECT_CALL(_engine.resourceModule().twoDas(), get("removefxondeath"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeTable({"label", "effecttype"}, {{"EFFECT_DISGUISE", "62"}, {"EFFECT_BEAM", "21"}})));
    auto creature = addCreature("victim", Faction::Hostile1);
    creature->applyEffect(_game->newEffect<FrightenedEffect>(), DurationType::Temporary, 60.0f);
    creature->applyEffect(_game->newEffect<StunnedEffect>(), DurationType::Innate);

    removeEffectsOnDeath(_engine.services().resource.twoDas, *creature);

    EXPECT_FALSE(creature->hasEffect(EffectType::Frightened));
    EXPECT_TRUE(creature->hasEffect(EffectType::Stunned));
    EXPECT_EQ((std::set<int> {21, 62}), effectTypesKeptOnDeath(_engine.services().resource.twoDas));
}

TEST_F(C11bTraps, a_formation_slot_is_distance_at_facing_plus_angle) {
    EXPECT_CALL(_engine.resourceModule().twoDas(), get("formations"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeTable({"label", "angle_1", "distance_1", "angle_2", "distance_2"},
                                         {{"FORMATION_WEDGE", "225", "2", "90", "3"}})));
    auto &twoDas = _engine.services().resource.twoDas;
    // Facing 0 looks along +Y: angle 90 is 3 m straight ahead.
    auto ahead = formationSlot(twoDas, 0, 2, glm::vec3(1.0f, 1.0f, 0.0f), 0.0f);
    ASSERT_TRUE(ahead);
    EXPECT_NEAR(1.0f, ahead->x, 1e-4f);
    EXPECT_NEAR(4.0f, ahead->y, 1e-4f);
    // 225: behind and to the left.
    auto wedge = formationSlot(twoDas, 0, 1, glm::vec3(0.0f), 0.0f);
    ASSERT_TRUE(wedge);
    EXPECT_LT(wedge->x, 0.0f);
    EXPECT_LT(wedge->y, 0.0f);
    EXPECT_FALSE(formationSlot(twoDas, 0, 3, glm::vec3(0.0f), 0.0f));
}

TEST(C11bClasses, cls_spgn_jedi_gates_force_power_gain_by_level) {
    TwoDA::Builder builder;
    builder.columns({"level", "numspelllevels"});
    builder.row({"1", "1"});
    builder.row({"2", "0"});
    std::shared_ptr<TwoDA> table(builder.build());
    EXPECT_TRUE(powerGainAllowed(*table, 1));
    EXPECT_FALSE(powerGainAllowed(*table, 2));
    EXPECT_TRUE(powerGainAllowed(*table, 3));
}

// Fields ------------------------------------------------------------------------

TEST(C11bFields, placeable_animation_state_names_its_starting_pose) {
    EXPECT_STREQ("default", placeableStateAnimation(0, false));
    EXPECT_STREQ("open", placeableStateAnimation(1, false));
    EXPECT_STREQ("close", placeableStateAnimation(2, false));
    EXPECT_STREQ("dead", placeableStateAnimation(3, false));
    EXPECT_STREQ("on", placeableStateAnimation(4, false));
    EXPECT_STREQ("off", placeableStateAnimation(5, false));
    EXPECT_STREQ("open", placeableStateAnimation(5, true));
}

TEST_F(C11bTraps, a_trap_shows_the_disarm_cursor_from_cursors_2da) {
    EXPECT_CALL(_engine.resourceModule().twoDas(), get("cursors"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeTable({"label", "resref", "cursorid"},
                                         {{"None", "", ""}, {"Transition", "gui_mp_transu", "21"}, {"Use", "gui_mp_useu", "25"},
                                          {"Examine", "", "17"}, {"Talk", "", "11"}, {"Walk", "", "3"}, {"XWalk", "", "5"},
                                          {"Attack", "", "7"}, {"Magic", "", "29"}, {"NoUse", "", "27"}, {"Trap", "gui_mp_disarmu", "33"}})));
    auto trap = addTrap({Gff::Field::newByte("Cursor", 2)});
    EXPECT_EQ(10, trap->cursorRow());
    auto &twoDas = _engine.services().resource.twoDas;
    EXPECT_EQ(resource::CursorType::DisableMine, cursorTypeForCursorsRow(twoDas, trap->cursorRow()));
    EXPECT_EQ(resource::CursorType::Door, cursorTypeForCursorsRow(twoDas, 1));
    EXPECT_EQ(resource::CursorType::None, cursorTypeForCursorsRow(twoDas, 0));
}

TEST(C11bFields, a_module_in_loadscreens_uses_its_image) {
    TwoDA::Builder builder;
    builder.columns({"bmpresref", "musicresref"});
    builder.row("default", {"LOAD_DEFAULT", "mus_loadscreen"});
    builder.row("manm26mg", {"LOAD_SWOOP", "mus_loadscreen"});
    std::shared_ptr<TwoDA> table(builder.build());
    EXPECT_EQ("load_swoop", loadScreenImage(table.get(), "MANM26MG"));
    EXPECT_EQ("load_end_m01aa", loadScreenImage(table.get(), "end_m01aa"));
    EXPECT_EQ("load_end_m01aa", loadScreenImage(nullptr, "end_m01aa"));

    TwoDA::Builder hintBuilder;
    hintBuilder.columns({"gameplayhint", "storyhint"});
    hintBuilder.row({"48412", "38103"});
    std::shared_ptr<TwoDA> hints(hintBuilder.build());
    EXPECT_EQ(48412, loadScreenHint(*hints, 0));
    EXPECT_EQ(-1, loadScreenHint(*hints, 1));
}

TEST(C11bFields, calendar_and_key_tokens_resolve) {
    StringTokens tokens;
    tokens.add("GameYear", StringTokenRow {15, -1, {-1, -1, -1, -1}});
    tokens.add("GameMonth", StringTokenRow {17, -1, {-1, -1, -1, -1}});
    tokens.add("DefaultAction", StringTokenRow {25, 239, {0, -1, -1, -1}});
    StringTokenSubject subject;
    subject.year = 1372;
    subject.month = 6;
    subject.keyName = [](int row, int direction) { return row == 239 && direction == 0 ? std::string("R") : std::string(); };
    EXPECT_EQ("1372/6 R", tokens.resolve("<GameYear>/<GameMonth> <DefaultAction>", {}, subject, nullptr));
}

TEST_F(C11bTraps, effect_icons_come_from_effecticon_by_priority) {
    EXPECT_CALL(_engine.resourceModule().twoDas(), get("effecticon"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeTable({"label", "iconresref", "good", "description", "priority"},
                                         {{"NULL_ICON", "", "", "", ""}, {"AFFLICTION", "ip_affliction", "0", "", "70"},
                                          {"SPEED_BURST", "ip_speedburst", "1", "", "20"}})));
    auto creature = addCreature("buffed", Faction::Player);
    auto slow = _game->newEffect<FrightenedEffect>();
    slow->setEffectIcon(1);
    auto fast = _game->newEffect<StunnedEffect>();
    fast->setEffectIcon(2);
    creature->applyEffect(slow, DurationType::Temporary, 60.0f);
    creature->applyEffect(fast, DurationType::Temporary, 60.0f);

    auto icons = creatureEffectIcons(_engine.services().resource.twoDas, *creature);
    ASSERT_EQ(2u, icons.size());
    EXPECT_EQ("ip_speedburst", icons[0].icon);
    EXPECT_TRUE(icons[0].good);
    EXPECT_EQ("ip_affliction", icons[1].icon);
}
