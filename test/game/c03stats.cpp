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

// Campaign batch C03: force points, alignment, multiclassing, faction-wide
// changes and the damage/killer queries. The notes these pin are in
// design note c03_stats.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../fixtures/resource.h"

#include "reone/game/action/castspellatobject.h"
#include "reone/game/d20/classes.h"
#include "reone/game/d20/forcepoints.h"
#include "reone/game/d20/spell.h"
#include "reone/game/effect/damage.h"
#include "reone/game/effect/stunned.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/game/talents.h"
#include "reone/resource/2da.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

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

// classes.2da as K1 ships it for the columns force points read: soldier,
// scout, scoundrel, the three Jedi classes and a combat droid.
std::shared_ptr<TwoDA> makeClassesTable() {
    const std::vector<std::string> columns = {
        "name", "description", "hitdie", "skillpointbase", "str", "dex", "con", "int", "wis", "cha",
        "skillstable", "savingthrowtable", "attackbonustable", "featstable", "featgain", "spellgaintable",
        "forcedie", "spellcaster"};
    auto row = [](const char *hitdie, const char *forcedie, const char *spellcaster) {
        return std::vector<std::string> {"1", "2", hitdie, "2", "10", "10", "10", "10", "10", "10",
                                         "", "save", "attack", "", "", "", forcedie, spellcaster};
    };
    std::vector<std::vector<std::string>> rows(9, row("10", "0", "0"));
    rows[1] = row("8", "0", "0");
    rows[2] = row("6", "0", "0");
    rows[3] = row("10", "4", "1"); // Jedi Guardian
    rows[4] = row("6", "8", "1");  // Jedi Consular
    rows[5] = row("8", "6", "1");  // Jedi Sentinel
    rows[6] = row("12", "0", "0"); // Combat droid
    return makeTable(columns, rows);
}

// forceadjust.2da, as K1 and K2 ship it.
std::shared_ptr<TwoDA> makeForceAdjustTable() {
    return makeTable(
        {"goodcost", "evilcost"},
        {{"1.75", "0.5"}, {"1.5", "0.7"}, {"1.5", "0.8"}, {"1.25", "0.85"}, {"1.25", "0.9"}, {"1", "1"},
         {"0.9", "1.25"}, {"0.85", "1.25"}, {"0.8", "1.5"}, {"0.7", "1.5"}, {"0.5", "1.75"}});
}

std::shared_ptr<Spell> makePower(int id, int forcePoints, char goodEvil) {
    auto spell = std::make_shared<Spell>();
    spell->type = static_cast<SpellType>(id);
    spell->forcePoints = forcePoints;
    spell->goodEvil = goodEvil;
    spell->category = 0x1101;
    spell->maxCR = 1;
    return spell;
}

constexpr int kPowerHeal = 28;      // 25 FP, light side
constexpr int kPowerChoke = 9;      // 15 FP, dark side
constexpr int kPowerPush = 23;      // 10 FP, universal
constexpr int kDamageTypeSonic = 1024;
constexpr int kDamageTypeFire = 256;

/**
 * A game with an active area, the routine table, and classes/forceadjust
 * served from K1-shaped tables.
 */
class C03Test : public Test {
protected:
    void SetUp() override {
        _engine.init();
        ON_CALL(_twoDas, get("classes")).WillByDefault(Return(makeClassesTable()));
        ON_CALL(_twoDas, get("save")).WillByDefault(Return(makeTable({"level", "fortsave", "refsave", "willsave"}, {{"1", "0", "0", "0"}})));
        ON_CALL(_twoDas, get("attack")).WillByDefault(Return(makeTable({"bab"}, {{"0"}})));
        ON_CALL(_twoDas, get("skills")).WillByDefault(Return(makeTable({"label"}, {{"computeruse"}})));
        ON_CALL(_twoDas, get("feat")).WillByDefault(Return(makeTable({"label"}, {{"f0"}})));
        _classes = std::make_unique<Classes>(_strings, _twoDas);
        ON_CALL(_engine.gameModule().classes(), get(_)).WillByDefault(Invoke([this](ClassType type) {
            return _classes->get(type);
        }));
        ON_CALL(_engine.resourceModule().twoDas(), get("forceadjust")).WillByDefault(Return(makeForceAdjustTable()));

        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _game->initLocalServices();
        _routines = std::make_unique<Routines>(GameID::KotOR, _game.get(), &_engine.services());
        _routines->init();
        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(_sceneGraph));
        _area = _game->newArea();
        TestGameModule::setActiveModuleArea(*_game, _area);
    }

    CreatureClass &cls(ClassType type) {
        auto clazz = _classes->get(type);
        EXPECT_TRUE(clazz);
        return *clazz;
    }

    std::shared_ptr<Creature> addCreature(const std::string &tag) {
        auto creature = _game->newCreature();
        creature->setTag(tag);
        _area->add(creature);
        return creature;
    }

    /** A creature with the given classes and Wisdom; force points start full. */
    std::shared_ptr<Creature> jedi(std::vector<std::pair<ClassType, int>> levels, int wisdom, bool pc, int basePoints = 0) {
        auto creature = addCreature(pc ? "pc" : "npc");
        creature->attributes().setAbilityScore(Ability::Wisdom, wisdom);
        for (auto [type, level] : levels) {
            creature->attributes().addClassLevels(&cls(type), level);
        }
        creature->setIsPC(pc);
        creature->addBaseForcePoints(basePoints);
        return creature;
    }

    Variable call(const std::string &name, std::vector<Variable> args, const std::shared_ptr<Object> &caller = nullptr) {
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
    NiceMock<MockStrings> _strings;
    NiceMock<MockTwoDAs> _twoDas;
    std::unique_ptr<Classes> _classes;
    NiceMock<scene::MockSceneGraph> _sceneGraph;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
    std::shared_ptr<Area> _area;
};

} // namespace

// --- Force points: maximum -------------------------------------------------

// The K1 party templates: Bastila (Sentinel 3, base 18, WIS 12) and Jolee
// (Consular 6, base 48, WIS 15). A companion adds its Wisdom modifier for
// every level on top of the stored base.
TEST_F(C03Test, npc_max_force_is_base_plus_wisdom_per_level) {
    auto bastila = jedi({{ClassType::JediSentinel, 3}}, 12, false, 18);
    EXPECT_EQ(21, bastila->maxForcePoints());
    EXPECT_EQ(21, bastila->currentForce());

    auto jolee = jedi({{ClassType::JediConsular, 6}}, 15, false, 48);
    EXPECT_EQ(60, jolee->maxForcePoints());
}

// With a poor Wisdom the companion still keeps one point per level.
TEST_F(C03Test, npc_max_force_is_at_least_one_per_level) {
    auto weak = jedi({{ClassType::JediGuardian, 3}}, 4, false, 2); // WIS 4: -3
    EXPECT_EQ(3, weak->maxForcePoints());
}

// The player counts only its Force-class levels, each at least 1.
TEST_F(C03Test, pc_max_force_counts_only_force_class_levels) {
    auto pc = jedi({{ClassType::Soldier, 8}, {ClassType::JediGuardian, 1}}, 14, true, 4);
    EXPECT_EQ(6, pc->maxForcePoints()); // 1 x (4 + 2)

    auto dull = jedi({{ClassType::Soldier, 8}, {ClassType::JediGuardian, 2}}, 2, true, 8); // WIS 2: -4
    EXPECT_EQ(2, dull->maxForcePoints());
}

// No Force class, or a droid: no force points at all, whatever the base says.
TEST_F(C03Test, max_force_is_zero_without_a_force_class_or_for_a_droid) {
    auto soldier = jedi({{ClassType::Soldier, 5}}, 16, false, 10);
    EXPECT_EQ(0, soldier->maxForcePoints());
    EXPECT_EQ(0, soldier->currentForce());

    auto pcSoldier = jedi({{ClassType::Soldier, 5}}, 16, true);
    EXPECT_EQ(0, pcSoldier->maxForcePoints());

    auto droid = jedi({{ClassType::JediConsular, 4}}, 14, false, 32);
    droid->setRacialType(RacialType::Droid);
    droid->recalculateForcePoints();
    EXPECT_EQ(0, droid->maxForcePoints());
    EXPECT_EQ(0, droid->currentForce());
}

// A level-up raises the maximum and the current value by the same amount,
// so force points already spent stay spent; rest refills.
TEST_F(C03Test, level_up_moves_current_force_by_the_change) {
    auto bastila = jedi({{ClassType::JediSentinel, 3}}, 12, false, 18);
    bastila->drainForcePoints(10);
    ASSERT_EQ(11, bastila->currentForce());

    bastila->attributes().addClassLevels(&cls(ClassType::JediSentinel), 1);
    bastila->addBaseForcePoints(6);
    EXPECT_EQ(28, bastila->maxForcePoints()); // 24 + 4
    EXPECT_EQ(18, bastila->currentForce());   // 11 + 7

    bastila->drainForcePoints(100);
    EXPECT_EQ(0, bastila->currentForce());
}

// CurrentForce is saved on the base axis: current - (max - base).
TEST_F(C03Test, current_force_serializes_on_the_base_axis) {
    auto bastila = jedi({{ClassType::JediSentinel, 3}}, 12, false, 18);
    bastila->drainForcePoints(5);
    EXPECT_EQ(16, bastila->currentForce());
    EXPECT_EQ(13, bastila->serializedCurrentForce()); // 16 - (21 - 18)
}

TEST_F(C03Test, force_point_routines_read_current_and_max) {
    auto bastila = jedi({{ClassType::JediSentinel, 3}}, 12, false, 18);
    bastila->drainForcePoints(4);
    EXPECT_EQ(17, call("GetCurrentForcePoints", {Variable::ofObject(bastila->id())}).intValue);
    EXPECT_EQ(21, call("GetMaxForcePoints", {Variable::ofObject(bastila->id())}).intValue);
    // OBJECT_SELF by default.
    EXPECT_EQ(21, call("GetMaxForcePoints", {Variable::ofObject(kObjectSelf)}, bastila).intValue);

    auto door = _game->newDoor();
    _area->add(door);
    EXPECT_EQ(0, call("GetCurrentForcePoints", {Variable::ofObject(door->id())}).intValue);
}

// --- Force points: power cost ----------------------------------------------

TEST(ForcePowerCost, companions_pay_the_base_cost) {
    auto table = makeForceAdjustTable();
    ForcePowerPayer npc;
    npc.playerCharacter = false;
    npc.goodEvil = 0;
    EXPECT_EQ(25, computeForcePowerCost(*makePower(kPowerHeal, 25, 'G'), npc, table.get()));
    EXPECT_EQ(0, computeForcePowerCost(*makePower(1, 0, 'G'), npc, table.get()));
}

// The player pays by alignment: forceadjust.2da row GoodEvil / 10.
TEST(ForcePowerCost, player_cost_follows_alignment) {
    auto table = makeForceAdjustTable();
    ForcePowerPayer dark;
    dark.playerCharacter = true;
    dark.goodEvil = 5; // row 0
    EXPECT_EQ(43, computeForcePowerCost(*makePower(kPowerHeal, 25, 'G'), dark, table.get()));  // 25 x 1.75
    EXPECT_EQ(7, computeForcePowerCost(*makePower(kPowerChoke, 15, 'E'), dark, table.get()));   // 15 x 0.5
    EXPECT_EQ(10, computeForcePowerCost(*makePower(kPowerPush, 10, 0), dark, table.get()));     // universal

    ForcePowerPayer light;
    light.playerCharacter = true;
    light.goodEvil = 100; // row 10
    EXPECT_EQ(12, computeForcePowerCost(*makePower(kPowerHeal, 25, 'G'), light, table.get())); // 12.5
    EXPECT_EQ(26, computeForcePowerCost(*makePower(kPowerChoke, 15, 'E'), light, table.get())); // 26.25

    ForcePowerPayer neutral;
    neutral.playerCharacter = true;
    neutral.goodEvil = 50;
    EXPECT_EQ(25, computeForcePowerCost(*makePower(kPowerHeal, 25, 'G'), neutral, table.get()));
}

// Charisma trims a surcharge, 0.05 per point, but never below the base.
TEST(ForcePowerCost, charisma_trims_a_surcharge_only) {
    auto table = makeForceAdjustTable();
    ForcePowerPayer dark;
    dark.playerCharacter = true;
    dark.goodEvil = 0;
    dark.charismaModifier = 3;
    EXPECT_EQ(40, computeForcePowerCost(*makePower(kPowerHeal, 25, 'G'), dark, table.get())); // 1.6
    dark.charismaModifier = 20;
    EXPECT_EQ(25, computeForcePowerCost(*makePower(kPowerHeal, 25, 'G'), dark, table.get()));
    // A discount is untouched.
    EXPECT_EQ(7, computeForcePowerCost(*makePower(kPowerChoke, 15, 'E'), dark, table.get()));
}

TEST(ForcePowerCost, a_paid_power_costs_at_least_one) {
    auto table = makeTable({"goodcost", "evilcost"}, {{"0.01", "0.01"}});
    ForcePowerPayer pc;
    pc.playerCharacter = true;
    pc.goodEvil = 0;
    EXPECT_EQ(1, computeForcePowerCost(*makePower(kPowerHeal, 25, 'G'), pc, table.get()));
}

// --- Force points: casting -------------------------------------------------

TEST_F(C03Test, creature_pays_its_own_cost) {
    auto pc = jedi({{ClassType::Soldier, 2}, {ClassType::JediGuardian, 3}}, 12, true, 12);
    pc->adjustGoodEvil(-50); // GoodEvil 0 (creatures start at 0) stays 0
    ASSERT_EQ(15, pc->maxForcePoints()); // 3 x (4 + 1)
    auto heal = makePower(kPowerHeal, 25, 'G');
    EXPECT_EQ(43, pc->forcePowerCost(*heal));
    EXPECT_FALSE(pc->canAffordForcePower(*heal));
    auto choke = makePower(kPowerChoke, 15, 'E');
    EXPECT_TRUE(pc->canAffordForcePower(*choke)); // 7
    pc->spendForcePoints(*choke);
    EXPECT_EQ(8, pc->currentForce());
}

// A power the caster cannot afford is dropped before anything happens.
TEST_F(C03Test, unaffordable_cast_is_dropped_without_paying) {
    auto bastila = jedi({{ClassType::JediSentinel, 3}}, 12, false, 18);
    bastila->drainForcePoints(15); // 6 left
    auto heal = makePower(kPowerHeal, 25, 'G');
    auto action = _game->newAction<CastSpellAtObjectAction>(heal, bastila, std::nullopt);
    action->execute(action, *bastila, 1.0f / 30.0f);
    EXPECT_TRUE(action->isCompleted());
    EXPECT_EQ(6, bastila->currentForce());
}

// Cheat casts (ActionCastSpellAtObject bCheat) and item casts cost nothing,
// so they are never dropped for want of force points.
TEST_F(C03Test, cheat_cast_is_not_dropped_for_force_points) {
    auto soldier = jedi({{ClassType::Soldier, 3}}, 10, false);
    auto heal = makePower(kPowerHeal, 25, 'G');
    auto action = _game->newAction<CastSpellAtObjectAction>(heal, soldier, std::nullopt, /*cheat=*/true);
    action->execute(action, *soldier, 1.0f / 30.0f);
    EXPECT_EQ(0, soldier->currentForce());
    EXPECT_FALSE(action->isCompleted());
}

// The talent search skips powers the creature cannot pay for.
TEST_F(C03Test, talent_search_offers_only_affordable_powers) {
    auto bastila = jedi({{ClassType::JediSentinel, 3}}, 12, false, 18);
    auto heal = makePower(kPowerHeal, 25, 'G');
    auto push = makePower(kPowerPush, 10, 0);
    bastila->attributes().addSpell(heal->type);
    bastila->attributes().addSpell(push->type);
    NiceMock<MockSpells> spells;
    NiceMock<MockFeats> feats;
    ON_CALL(spells, get(_)).WillByDefault(Invoke([&](SpellType type) -> std::shared_ptr<Spell> {
        if (type == heal->type) return heal;
        if (type == push->type) return push;
        return nullptr;
    }));
    TalentQuery query;
    query.category = 0xffff;
    query.mode = TalentSearchMode::Best;
    query.excludeType = 1; // no feats
    auto always = [](int min, int) { return min; };

    auto choice = findCreatureTalent(*bastila, spells, feats, query, always);
    ASSERT_TRUE(choice);
    EXPECT_EQ(kPowerPush, choice->id); // heal costs 25 > 21

    bastila->drainForcePoints(15); // 6 left: nothing affordable
    EXPECT_FALSE(findCreatureTalent(*bastila, spells, feats, query, always));
}

// --- AddMultiClass ----------------------------------------------------------

// k_pdan_makejedi: the Jedi class joins at level 0, XP is topped up to the
// next level, and nothing else changes until the level-up.
TEST_F(C03Test, add_multi_class_adds_a_level_zero_class_and_one_pending_level) {
    auto pc = jedi({{ClassType::Soldier, 8}}, 14, true);
    pc->setXP(30000);
    int hitPoints = pc->maxHitPoints();

    call("AddMultiClass", {Variable::ofInt(static_cast<int>(ClassType::JediGuardian)), Variable::ofObject(pc->id())});

    const auto &attributes = pc->attributes();
    ASSERT_EQ(2u, attributes.classLevels().size());
    EXPECT_EQ(ClassType::Soldier, attributes.getClassByPosition(1));
    EXPECT_EQ(ClassType::JediGuardian, attributes.getClassByPosition(2));
    EXPECT_EQ(0, attributes.getLevelByPosition(2));
    EXPECT_EQ(8, attributes.getAggregateLevel());
    EXPECT_EQ(ClassType::JediGuardian, attributes.getEffectiveClass()); // the level-up takes it
    EXPECT_EQ(36000, pc->xp());                                         // 8 x 9 x 500
    EXPECT_TRUE(pc->isLevelUpPending());
    EXPECT_EQ(hitPoints, pc->maxHitPoints());
    EXPECT_EQ(0, pc->maxForcePoints());

    // The first Jedi level: forcedie 4 + WIS 14 (+2).
    pc->attributes().addClassLevels(&cls(ClassType::JediGuardian), 1);
    pc->addBaseForcePoints(cls(ClassType::JediGuardian).forcePointsPerLevel());
    EXPECT_EQ(6, pc->maxForcePoints());
    EXPECT_EQ(6, pc->currentForce());
}

// More XP than the threshold is kept; a class the creature has is not added
// twice; a third class replaces the second slot.
TEST_F(C03Test, add_multi_class_edge_cases) {
    auto pc = jedi({{ClassType::Scout, 4}}, 10, true);
    pc->setXP(12000); // above the level-5 threshold of 10000
    call("AddMultiClass", {Variable::ofInt(static_cast<int>(ClassType::JediSentinel)), Variable::ofObject(pc->id())});
    EXPECT_EQ(12000, pc->xp());

    call("AddMultiClass", {Variable::ofInt(static_cast<int>(ClassType::JediSentinel)), Variable::ofObject(pc->id())});
    EXPECT_EQ(2u, pc->attributes().classLevels().size());

    call("AddMultiClass", {Variable::ofInt(static_cast<int>(ClassType::JediConsular)), Variable::ofObject(pc->id())});
    ASSERT_EQ(2u, pc->attributes().classLevels().size());
    EXPECT_EQ(ClassType::Scout, pc->attributes().getClassByPosition(1));
    EXPECT_EQ(ClassType::JediConsular, pc->attributes().getClassByPosition(2));

    // Invalid objects and classes do nothing.
    call("AddMultiClass", {Variable::ofInt(99), Variable::ofObject(pc->id())});
    call("AddMultiClass", {Variable::ofInt(3), Variable::ofObject(kObjectInvalid)});
    EXPECT_EQ(ClassType::JediConsular, pc->attributes().getClassByPosition(2));
}

// --- AdjustAlignment ---------------------------------------------------------

TEST_F(C03Test, adjust_alignment_shifts_light_and_dark_within_range) {
    auto pc = addCreature("pc");
    pc->adjustGoodEvil(50);
    ASSERT_EQ(50, call("GetGoodEvilValue", {Variable::ofObject(pc->id())}).intValue);

    call("AdjustAlignment", {Variable::ofObject(pc->id()), Variable::ofInt(2), Variable::ofInt(12)});
    EXPECT_EQ(62, pc->goodEvil());
    call("AdjustAlignment", {Variable::ofObject(pc->id()), Variable::ofInt(3), Variable::ofInt(20)});
    EXPECT_EQ(42, pc->goodEvil());
    call("AdjustAlignment", {Variable::ofObject(pc->id()), Variable::ofInt(3), Variable::ofInt(100)});
    EXPECT_EQ(0, pc->goodEvil());
    call("AdjustAlignment", {Variable::ofObject(pc->id()), Variable::ofInt(2), Variable::ofInt(250)});
    EXPECT_EQ(100, pc->goodEvil());
    EXPECT_EQ(100, call("GetGoodEvilValue", {Variable::ofObject(pc->id())}).intValue);
    // ALIGNMENT_ALL changes nothing.
    call("AdjustAlignment", {Variable::ofObject(pc->id()), Variable::ofInt(0), Variable::ofInt(30)});
    EXPECT_EQ(100, pc->goodEvil());
}

// ALIGNMENT_NEUTRAL moves towards 50 and stops there.
TEST_F(C03Test, adjust_alignment_neutral_stops_at_fifty) {
    auto pc = addCreature("pc");
    pc->adjustGoodEvil(80);
    call("AdjustAlignment", {Variable::ofObject(pc->id()), Variable::ofInt(1), Variable::ofInt(15)});
    EXPECT_EQ(65, pc->goodEvil());
    call("AdjustAlignment", {Variable::ofObject(pc->id()), Variable::ofInt(1), Variable::ofInt(40)});
    EXPECT_EQ(50, pc->goodEvil());

    pc->adjustGoodEvil(-10);
    call("AdjustAlignment", {Variable::ofObject(pc->id()), Variable::ofInt(1), Variable::ofInt(15)});
    EXPECT_EQ(50, pc->goodEvil());
}

// The TSL table's appended bDontModifyNPCs defaults; a non-creature is ignored.
TEST_F(C03Test, adjust_alignment_tolerates_tsl_arguments_and_non_creatures) {
    auto pc = addCreature("pc");
    call("AdjustAlignment", {Variable::ofObject(pc->id()), Variable::ofInt(2), Variable::ofInt(5), Variable::ofInt(1)});
    EXPECT_EQ(5, pc->goodEvil());
    call("AdjustAlignment", {Variable::ofObject(kObjectInvalid), Variable::ofInt(2), Variable::ofInt(5)});
    EXPECT_EQ(5, pc->goodEvil());
}

// --- Party routines ------------------------------------------------------------

// Janice's droids are not players: AddToParty leaves the companion party alone,
// and RemoveFromParty on the lone PC changes no membership.
TEST_F(C03Test, multiplayer_party_routines_leave_companions_alone) {
    auto pc = addCreature("pc");
    _game->party().addMember(kNpcPlayer, pc);
    _game->party().setPlayer(pc);
    auto droid = addCreature("M4H8");
    int size = _game->party().getSize();

    call("AddToParty", {Variable::ofObject(droid->id()), Variable::ofObject(pc->id())});
    EXPECT_EQ(size, _game->party().getSize());
    EXPECT_FALSE(_game->party().isMember(*droid));

    call("RemoveFromParty", {Variable::ofObject(pc->id())});
    EXPECT_TRUE(_game->party().isMember(*pc));
    EXPECT_EQ(size, _game->party().getSize());
}

// --- Faction-wide changes --------------------------------------------------------

TEST_F(C03Test, change_faction_by_faction_moves_every_member_in_the_area) {
    auto a = addCreature("a");
    auto b = addCreature("b");
    auto other = addCreature("other");
    a->setFaction(Faction::Hostile1);
    b->setFaction(Faction::Hostile1);
    other->setFaction(Faction::Friendly1);

    call("ChangeFactionByFaction", {Variable::ofInt(static_cast<int>(Faction::Hostile1)), Variable::ofInt(static_cast<int>(Faction::Neutral))});
    EXPECT_EQ(Faction::Neutral, a->faction());
    EXPECT_EQ(Faction::Neutral, b->faction());
    EXPECT_EQ(Faction::Friendly1, other->faction());
}

TEST_F(C03Test, surrender_by_faction_also_clears_actions_and_combat) {
    auto a = addCreature("a");
    a->setFaction(Faction::Hostile1);
    auto target = addCreature("target");
    a->addAction(_game->newAction<CastSpellAtObjectAction>(makePower(kPowerPush, 0, 0), target, std::nullopt));
    ASSERT_FALSE(a->actions().empty());

    call("SurrenderByFaction", {Variable::ofInt(static_cast<int>(Faction::Hostile1)), Variable::ofInt(static_cast<int>(Faction::Friendly1))});
    EXPECT_EQ(Faction::Friendly1, a->faction());
    EXPECT_TRUE(a->actions().empty());
    EXPECT_FALSE(a->isInCombat());
}

TEST_F(C03Test, surrender_stops_hostiles_within_250_m_not_just_10_m) {
    // K2 behaviour, owner decision: every creature the caller is hostile to
    // within 250 m stops fighting and loses its effects.
    auto &reputes = static_cast<MockReputes &>(_engine.services().game.reputes);
    ON_CALL(reputes, getIsEnemy(An<const Creature &>(), An<const Creature &>()))
        .WillByDefault(Invoke([](const Creature &source, const Creature &target) {
            return source.faction() != target.faction() &&
                   source.faction() != Faction::Invalid && target.faction() != Faction::Invalid;
        }));
    auto boss = addCreature("boss");
    boss->setFaction(Faction::Hostile1);
    auto soldier = addCreature("soldier");
    soldier->setFaction(Faction::Friendly1);
    soldier->setPosition(glm::vec3(30.0f, 0.0f, 0.0f));
    auto ally = addCreature("ally");
    ally->setFaction(Faction::Hostile1);
    ally->setPosition(glm::vec3(5.0f, 0.0f, 0.0f));
    auto distant = addCreature("distant");
    distant->setFaction(Faction::Friendly1);
    distant->setPosition(glm::vec3(300.0f, 0.0f, 0.0f));
    for (const auto &creature : {soldier, ally, distant}) {
        creature->addAction(_game->newAction<CastSpellAtObjectAction>(makePower(kPowerPush, 0, 0), boss, std::nullopt));
        creature->activateCombat();
        creature->applyEffect(std::make_shared<StunnedEffect>(), DurationType::Temporary, 60.0f);
    }

    call("SurrenderToEnemies", {}, boss);
    EXPECT_EQ(Faction::Invalid, boss->faction());
    EXPECT_TRUE(soldier->actions().empty());
    EXPECT_FALSE(soldier->isInCombat());
    EXPECT_TRUE(soldier->effects().empty());
    // Not hostile to the caller, or too far away: untouched.
    EXPECT_FALSE(ally->actions().empty());
    EXPECT_TRUE(ally->isInCombat());
    EXPECT_FALSE(ally->effects().empty());
    EXPECT_FALSE(distant->actions().empty());
    EXPECT_FALSE(distant->effects().empty());
}

// --- GetFactionAverageReputation -------------------------------------------------

TEST_F(C03Test, faction_average_reputation_is_the_faction_regard) {
    auto guard = addCreature("tat18_14sandg_01");
    guard->setFaction(Faction::Hostile1);
    auto pc = addCreature("pc");
    pc->setFaction(Faction::Player);
    auto &reputes = static_cast<MockReputes &>(_engine.services().game.reputes);
    ON_CALL(reputes, getReputation(Faction::Hostile1, Faction::Player)).WillByDefault(Return(5));

    EXPECT_EQ(5, call("GetFactionAverageReputation", {Variable::ofObject(guard->id()), Variable::ofObject(pc->id())}).intValue);
    EXPECT_EQ(-1, call("GetFactionAverageReputation", {Variable::ofObject(kObjectInvalid), Variable::ofObject(pc->id())}).intValue);
    EXPECT_EQ(0, call("GetFactionAverageReputation", {Variable::ofObject(guard->id()), Variable::ofObject(kObjectInvalid)}).intValue);
}

// --- GetDamageDealtByType / GetLastKiller ------------------------------------------

// The terentatek asks how much sonic damage the last hit carried.
TEST_F(C03Test, damage_dealt_by_type_reads_the_last_damage_event) {
    auto terentatek = addCreature("terentatek");
    terentatek->setMaxHitPoints(500);
    terentatek->setCurrentHitPoints(500);
    auto pc = addCreature("pc");

    terentatek->recordTypedDamage(12, kDamageTypeSonic, pc);
    terentatek->recordTypedDamage(5, kDamageTypeFire, pc);
    EXPECT_EQ(12, call("GetDamageDealtByType", {Variable::ofInt(kDamageTypeSonic)}, terentatek).intValue);
    EXPECT_EQ(5, call("GetDamageDealtByType", {Variable::ofInt(kDamageTypeFire)}, terentatek).intValue);
    EXPECT_EQ(0, call("GetDamageDealtByType", {Variable::ofInt(kDamageTypeSonic | kDamageTypeFire)}, terentatek).intValue);

    // Another damager starts a new event.
    auto other = addCreature("other");
    terentatek->recordTypedDamage(3, kDamageTypeFire, other);
    EXPECT_EQ(0, call("GetDamageDealtByType", {Variable::ofInt(kDamageTypeSonic)}, terentatek).intValue);
    EXPECT_EQ(3, call("GetDamageDealtByType", {Variable::ofInt(kDamageTypeFire)}, terentatek).intValue);
}

// Uthar talks to whoever killed the student.
TEST_F(C03Test, last_killer_is_the_damager_that_killed) {
    auto student = addCreature("student");
    student->setMaxHitPoints(10);
    student->setCurrentHitPoints(10);
    auto pc = addCreature("pc");
    auto bystander = addCreature("bystander");

    EXPECT_EQ(kObjectInvalid, call("GetLastKiller", {}, student).objectId);
    student->damage(4, bystander);
    EXPECT_EQ(kObjectInvalid, call("GetLastKiller", {}, student).objectId);
    student->damage(20, pc);
    ASSERT_TRUE(student->isDead());
    EXPECT_EQ(pc->id(), call("GetLastKiller", {}, student).objectId);
}
