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

// NWScript routines implemented in tranche 2 (docs/dev/routines_missing.md): effect
// subtype and duration queries, stealth XP, and faction queries.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/effect.h"
#include "reone/game/effect/regenerate.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

class Tranche2Routines : public TestWithParam<GameID> {
protected:
    void SetUp() override {
        engine.init();
        game = std::make_unique<Game>(GetParam(), std::filesystem::path {}, engine.options(), engine.services(), console);
        game->initLocalServices();
        ON_CALL(engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(sceneGraph));
        routines = std::make_unique<Routines>(GetParam(), game.get(), &engine.services());
        routines->init();
        area = game->newArea();
        TestGameModule::setActiveModuleArea(*game, area);
    }

    std::shared_ptr<Creature> addCreature(Faction faction = Faction::Friendly1) {
        auto creature = game->newCreature();
        creature->setFaction(faction);
        area->add(creature);
        return creature;
    }

    Variable call(const std::string &name, std::vector<Variable> args) {
        int index = routines->getIndexByName(name);
        EXPECT_GE(index, 0) << name << " is not registered";
        return routines->get(index).invoke(args, execution);
    }

    static Variable object(const std::shared_ptr<Object> &o) { return Variable::ofObject(o->id()); }
    static Variable i(int v) { return Variable::ofInt(v); }

    std::shared_ptr<Effect> regenerate() { return std::make_shared<RegenerateEffect>(1, 1.0f); }

    TestEngine engine;
    StubConsole console;
    std::unique_ptr<Game> game;
    std::unique_ptr<Routines> routines;
    NiceMock<scene::MockSceneGraph> sceneGraph;
    std::shared_ptr<Area> area;
    ExecutionContext execution;
};

constexpr int kSubtypeMagical = 8;
constexpr int kSubtypeSupernatural = 16;
constexpr int kSubtypeExtraordinary = 24;

} // namespace

// Effects

TEST_P(Tranche2Routines, an_effect_value_starts_out_magical_and_unapplied) {
    auto effect = regenerate();
    EXPECT_EQ(kSubtypeMagical, call("GetEffectSubType", {Variable::ofEffect(effect)}).intValue);
    EXPECT_EQ(static_cast<int>(DurationType::Instant), call("GetEffectDurationType", {Variable::ofEffect(effect)}).intValue);
}

TEST_P(Tranche2Routines, subtype_routines_mark_the_effect_and_hand_it_back) {
    auto effect = regenerate();
    Variable supernatural = call("SupernaturalEffect", {Variable::ofEffect(effect)});
    EXPECT_EQ(effect, supernatural.engineType);
    EXPECT_EQ(kSubtypeSupernatural, call("GetEffectSubType", {supernatural}).intValue);
    EXPECT_EQ(kSubtypeExtraordinary, call("GetEffectSubType", {call("ExtraordinaryEffect", {supernatural})}).intValue);
    EXPECT_EQ(kSubtypeMagical, call("GetEffectSubType", {call("MagicalEffect", {supernatural})}).intValue);
}

TEST_P(Tranche2Routines, applying_an_effect_keeps_its_subtype_and_reports_the_duration_type) {
    auto creature = addCreature();
    auto temporary = regenerate();
    auto permanent = regenerate();
    call("SupernaturalEffect", {Variable::ofEffect(temporary)});
    creature->applyEffect(temporary, DurationType::Temporary, 30.0f);
    creature->applyEffect(permanent, DurationType::Permanent);

    EXPECT_EQ(static_cast<int>(DurationType::Temporary), call("GetEffectDurationType", {Variable::ofEffect(temporary)}).intValue);
    EXPECT_EQ(kSubtypeSupernatural, call("GetEffectSubType", {Variable::ofEffect(temporary)}).intValue);
    EXPECT_EQ(static_cast<int>(DurationType::Permanent), call("GetEffectDurationType", {Variable::ofEffect(permanent)}).intValue);
    EXPECT_EQ(kSubtypeMagical, call("GetEffectSubType", {Variable::ofEffect(permanent)}).intValue);
}

TEST_P(Tranche2Routines, saved_effect_values_report_their_recorded_duration_and_subtype) {
    EffectInstance instance = regenerate()->saveFacingInstance();
    instance.subType = 0x10 | static_cast<uint16_t>(DurationType::Temporary);
    auto saved = std::make_shared<SavedEffectValue>(instance);
    EXPECT_EQ(static_cast<int>(DurationType::Temporary), call("GetEffectDurationType", {Variable::ofEffect(saved)}).intValue);
    EXPECT_EQ(kSubtypeSupernatural, call("GetEffectSubType", {Variable::ofEffect(saved)}).intValue);

    call("ExtraordinaryEffect", {Variable::ofEffect(saved)});
    EXPECT_EQ(kSubtypeExtraordinary, call("GetEffectSubType", {Variable::ofEffect(saved)}).intValue);
    EXPECT_EQ(static_cast<int>(DurationType::Temporary), call("GetEffectDurationType", {Variable::ofEffect(saved)}).intValue);
}

// Stealth XP

TEST_P(Tranche2Routines, stealth_xp_maximum_and_decrement_read_back_what_was_set) {
    EXPECT_EQ(0, call("GetMaxStealthXP", {}).intValue);
    area->setMaxStealthXP(150);
    EXPECT_EQ(150, call("GetMaxStealthXP", {}).intValue);

    EXPECT_EQ(0, call("GetStealthXPDecrement", {}).intValue);
    call("SetStealthXPDecrement", {i(7)});
    EXPECT_EQ(7, call("GetStealthXPDecrement", {}).intValue);
    EXPECT_EQ(7, area->stealthXPDecrement());
}

// Factions

TEST_P(Tranche2Routines, faction_members_are_cycled_in_area_order_and_only_the_player_party_by_default) {
    auto pc = addCreature(Faction::Player);
    game->party().addMember(kNpcPlayer, pc);
    game->party().setPlayer(pc);
    auto companion = addCreature(Faction::Player); // same faction, not in the party
    auto guard = addCreature(Faction::Hostile1);
    auto other = addCreature(Faction::Player);

    // Everyone of the faction.
    std::vector<uint32_t> found;
    Variable member = call("GetFirstFactionMember", {object(pc), i(0)});
    while (member.objectId != kObjectInvalid) {
        found.push_back(member.objectId);
        member = call("GetNextFactionMember", {object(pc), i(0)});
    }
    EXPECT_EQ((std::vector<uint32_t> {pc->id(), companion->id(), other->id()}), found);

    // The default is the party only.
    found.clear();
    member = call("GetFirstFactionMember", {object(companion)});
    while (member.objectId != kObjectInvalid) {
        found.push_back(member.objectId);
        member = call("GetNextFactionMember", {object(companion)});
    }
    EXPECT_EQ((std::vector<uint32_t> {pc->id()}), found);

    // A faction of one, and nothing for an invalid object.
    EXPECT_EQ(guard->id(), call("GetFirstFactionMember", {object(guard), i(0)}).objectId);
    EXPECT_EQ(kObjectInvalid, call("GetNextFactionMember", {object(guard), i(0)}).objectId);
    EXPECT_EQ(kObjectInvalid, call("GetFirstFactionMember", {Variable::ofObject(kObjectInvalid)}).objectId);
}

TEST_P(Tranche2Routines, a_new_first_call_restarts_the_cycle) {
    auto a = addCreature(Faction::Hostile1);
    auto b = addCreature(Faction::Hostile1);
    EXPECT_EQ(a->id(), call("GetFirstFactionMember", {object(a), i(0)}).objectId);
    EXPECT_EQ(a->id(), call("GetFirstFactionMember", {object(a), i(0)}).objectId);
    EXPECT_EQ(b->id(), call("GetNextFactionMember", {object(a), i(0)}).objectId);
    EXPECT_EQ(kObjectInvalid, call("GetNextFactionMember", {object(a), i(0)}).objectId);
}

TEST_P(Tranche2Routines, faction_averages_are_whole_numbers_over_every_member) {
    auto a = addCreature(Faction::Hostile1);
    auto b = addCreature(Faction::Hostile1);
    auto c = addCreature(Faction::Hostile1);
    addCreature(Faction::Friendly1)->setXP(9999); // another faction: not counted
    a->setXP(100);
    b->setXP(200);
    c->setXP(201);
    a->setGoodEvil(10);
    b->setGoodEvil(20);
    c->setGoodEvil(25);

    EXPECT_EQ(167, call("GetFactionAverageXP", {object(a)}).intValue);
    EXPECT_EQ(18, call("GetFactionAverageGoodEvilAlignment", {object(b)}).intValue);
    EXPECT_EQ(0, call("GetFactionAverageLevel", {object(c)}).intValue);
    for (const char *name : {"GetFactionAverageXP", "GetFactionAverageGoodEvilAlignment", "GetFactionAverageLevel"}) {
        EXPECT_EQ(-1, call(name, {Variable::ofObject(kObjectInvalid)}).intValue) << name;
    }
}

TEST_P(Tranche2Routines, reputation_is_how_the_sources_faction_regards_the_targets) {
    auto guard = addCreature(Faction::Hostile1);
    auto pc = addCreature(Faction::Player);
    auto &reputes = static_cast<MockReputes &>(engine.services().game.reputes);
    ON_CALL(reputes, getReputation(Faction::Hostile1, Faction::Player)).WillByDefault(Return(5));
    ON_CALL(reputes, getReputation(Faction::Player, Faction::Hostile1)).WillByDefault(Return(140));

    EXPECT_EQ(5, call("GetReputation", {object(guard), object(pc)}).intValue);
    EXPECT_EQ(100, call("GetReputation", {object(pc), object(guard)}).intValue);
    EXPECT_EQ(-1, call("GetReputation", {Variable::ofObject(kObjectInvalid), object(pc)}).intValue);
    EXPECT_EQ(0, call("GetReputation", {object(guard), Variable::ofObject(kObjectInvalid)}).intValue);
}

INSTANTIATE_TEST_SUITE_P(BothGames, Tranche2Routines, Values(GameID::KotOR, GameID::TSL));
