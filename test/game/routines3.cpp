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

// NWScript routines implemented in tranche 3 (docs/dev/routines_missing.md): saving
// throw, armour class and challenge rating queries, nearest-to-location
// searches, encounter difficulty, transition targets and area fog.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/effect/savingthrowincrease.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/encounter.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/trigger.h"
#include "reone/game/object/waypoint.h"
#include "reone/game/script/routines.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

class Tranche3Routines : public TestWithParam<GameID> {
protected:
    void SetUp() override {
        engine.init();
        for (const char *table : {"genericdoors", "placeables"}) {
            TwoDA::Builder builder;
            builder.columns({"modelname"});
            builder.row({""});
            ON_CALL(engine.resourceModule().twoDas(), get(std::string(table)))
                .WillByDefault(Return(std::shared_ptr<TwoDA>(builder.build())));
        }
        TwoDA::Builder baseItems;
        baseItems.columns({"weapontype", "itemclass"});
        baseItems.row({"0", "armor"});
        baseItems.row({"4", "pistol"}); // WeaponType::Ranged
        ON_CALL(engine.resourceModule().twoDas(), get(std::string("baseitems")))
            .WillByDefault(Return(std::shared_ptr<TwoDA>(baseItems.build())));

        game = std::make_unique<Game>(GetParam(), std::filesystem::path {}, engine.options(), engine.services(), console);
        game->initLocalServices();
        ON_CALL(engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(sceneGraph));
        routines = std::make_unique<Routines>(GetParam(), game.get(), &engine.services());
        routines->init();
        area = game->newArea();
        TestGameModule::setActiveModuleArea(*game, area);
    }

    std::shared_ptr<Creature> addCreature(const glm::vec3 &position = glm::vec3(0.0f)) {
        auto creature = game->newCreature();
        creature->setPosition(position);
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
    static Variable at(float x, float y) {
        return Variable::ofLocation(std::make_shared<Location>(glm::vec3(x, y, 0.0f), 0.0f));
    }

    TestEngine engine;
    StubConsole console;
    NiceMock<scene::MockSceneGraph> sceneGraph;
    std::unique_ptr<Game> game;
    std::unique_ptr<Routines> routines;
    std::shared_ptr<Area> area;
    ExecutionContext execution;
};

constexpr int kObjectTypeCreature = 1;
constexpr int kObjectTypeWaypoint = 32;

} // namespace

// Creature queries

TEST_P(Tranche3Routines, saving_throw_queries_follow_the_creatures_fortitude_reflex_and_will) {
    auto creature = addCreature();
    creature->applyEffect(std::make_shared<SavingThrowIncreaseEffect>(1, 3, SavingThrowType::All), DurationType::Permanent);
    creature->applyEffect(std::make_shared<SavingThrowIncreaseEffect>(3, 5, SavingThrowType::All), DurationType::Permanent);

    EXPECT_EQ(creature->getSavingThrow(1), call("GetFortitudeSavingThrow", {object(creature)}).intValue);
    EXPECT_EQ(creature->getSavingThrow(2), call("GetReflexSavingThrow", {object(creature)}).intValue);
    EXPECT_EQ(creature->getSavingThrow(3), call("GetWillSavingThrow", {object(creature)}).intValue);
    EXPECT_EQ(3, call("GetFortitudeSavingThrow", {object(creature)}).intValue - call("GetReflexSavingThrow", {object(creature)}).intValue);
    EXPECT_EQ(5, call("GetWillSavingThrow", {object(creature)}).intValue - call("GetReflexSavingThrow", {object(creature)}).intValue);
    for (const char *name : {"GetFortitudeSavingThrow", "GetReflexSavingThrow", "GetWillSavingThrow"}) {
        EXPECT_EQ(0, call(name, {Variable::ofObject(kObjectInvalid)}).intValue) << name;
    }
}

TEST_P(Tranche3Routines, armour_class_is_the_creatures_defense_and_zero_for_other_objects) {
    auto creature = addCreature();
    EXPECT_EQ(creature->getDefense(), call("GetAC", {object(creature)}).intValue);
    EXPECT_EQ(creature->getDefense(), call("GetAC", {object(creature), i(1)}).intValue);
    EXPECT_EQ(0, call("GetAC", {Variable::ofObject(kObjectInvalid)}).intValue);
}

TEST_P(Tranche3Routines, challenge_rating_is_a_float_and_zero_for_other_objects) {
    auto creature = addCreature();
    Variable result = call("GetChallengeRating", {object(creature)});
    EXPECT_EQ(VariableType::Float, result.type);
    EXPECT_FLOAT_EQ(creature->challengeRating(), result.floatValue);
    EXPECT_FLOAT_EQ(0.0f, call("GetChallengeRating", {Variable::ofObject(kObjectInvalid)}).floatValue);
}

TEST_P(Tranche3Routines, good_evil_value_is_set_and_clamped_to_zero_to_one_hundred) {
    auto creature = addCreature();
    call("SetGoodEvilValue", {object(creature), i(35)});
    EXPECT_EQ(35, creature->goodEvil());
    call("SetGoodEvilValue", {object(creature), i(400)});
    EXPECT_EQ(100, creature->goodEvil());
    call("SetGoodEvilValue", {object(creature), i(-20)});
    EXPECT_EQ(0, creature->goodEvil());
    call("SetGoodEvilValue", {Variable::ofObject(kObjectInvalid), i(10)}); // no effect, no throw
}

// Items

TEST_P(Tranche3Routines, weapon_ranged_tells_ranged_weapons_from_the_rest) {
    auto makeItem = [&](int baseItem) {
        auto gff = Gff::Builder()
                       .field(Gff::Field::newInt("BaseItem", baseItem))
                       .field(Gff::Field::newWord("StackSize", 1))
                       .build();
        auto item = game->newItem();
        item->deserialize(*gff, SerializedIdentityContext::templateResource());
        return item;
    };
    EXPECT_EQ(1, call("GetWeaponRanged", {object(makeItem(1))}).intValue);
    EXPECT_EQ(0, call("GetWeaponRanged", {object(makeItem(0))}).intValue);
    EXPECT_EQ(0, call("GetWeaponRanged", {object(addCreature())}).intValue);
    EXPECT_EQ(0, call("GetWeaponRanged", {Variable::ofObject(kObjectInvalid)}).intValue);
}

// Searches around a location

TEST_P(Tranche3Routines, nearest_object_to_location_counts_from_one_and_breaks_ties_by_the_search_order) {
    auto far = addCreature(glm::vec3(10.0f, 0.0f, 0.0f));
    auto near = addCreature(glm::vec3(2.0f, 0.0f, 0.0f));
    auto mid = addCreature(glm::vec3(0.0f, 5.0f, 0.0f));
    EXPECT_EQ(near->id(), call("GetNearestObjectToLocation", {i(kObjectTypeCreature), at(0.0f, 0.0f)}).objectId);
    EXPECT_EQ(mid->id(), call("GetNearestObjectToLocation", {i(kObjectTypeCreature), at(0.0f, 0.0f), i(2)}).objectId);
    EXPECT_EQ(far->id(), call("GetNearestObjectToLocation", {i(kObjectTypeCreature), at(0.0f, 0.0f), i(3)}).objectId);
    EXPECT_EQ(kObjectInvalid, call("GetNearestObjectToLocation", {i(kObjectTypeCreature), at(0.0f, 0.0f), i(4)}).objectId);
    // Measured from the location, not from the creatures.
    EXPECT_EQ(far->id(), call("GetNearestObjectToLocation", {i(kObjectTypeCreature), at(11.0f, 0.0f)}).objectId);
    EXPECT_EQ(kObjectInvalid, call("GetNearestObjectToLocation", {i(kObjectTypeWaypoint), at(0.0f, 0.0f)}).objectId);
}

TEST_P(Tranche3Routines, nearest_creature_to_location_skips_the_dead_unless_asked_for_them) {
    constexpr int kCreatureTypeIsAlive = 4;
    auto near = addCreature(glm::vec3(1.0f, 0.0f, 0.0f));
    auto far = addCreature(glm::vec3(8.0f, 0.0f, 0.0f));
    near->setCurrentHitPoints(0);
    near->setMaxHitPoints(10);
    far->setMaxHitPoints(10);
    far->setCurrentHitPoints(10);
    ASSERT_TRUE(near->isDead());

    EXPECT_EQ(far->id(), call("GetNearestCreatureToLocation", {i(kCreatureTypeIsAlive), i(1), at(0.0f, 0.0f)}).objectId);
    EXPECT_EQ(near->id(), call("GetNearestCreatureToLocation", {i(kCreatureTypeIsAlive), i(0), at(0.0f, 0.0f)}).objectId);
    EXPECT_EQ(kObjectInvalid, call("GetNearestCreatureToLocation", {i(kCreatureTypeIsAlive), i(1), at(0.0f, 0.0f), i(2)}).objectId);
}

// Encounters

TEST_P(Tranche3Routines, encounter_difficulty_reads_back_what_was_set) {
    auto encounter = game->newEncounter();
    area->add(encounter);
    call("SetEncounterDifficulty", {i(3), object(encounter)});
    EXPECT_EQ(3, call("GetEncounterDifficulty", {object(encounter)}).intValue);
    EXPECT_EQ(3, encounter->difficulty());

    // Not an encounter: nothing to set, nothing to read.
    auto creature = addCreature();
    call("SetEncounterDifficulty", {i(2), object(creature)});
    EXPECT_EQ(0, call("GetEncounterDifficulty", {object(creature)}).intValue);
}

// Transitions

TEST_P(Tranche3Routines, transition_target_is_the_waypoint_a_door_or_trigger_links_to) {
    auto waypoint = game->newWaypoint();
    waypoint->setTag("wp_exit");
    area->add(waypoint);

    auto doorGff = [](const char *linkedTo, const char *module) {
        return Gff::Builder()
            .field(Gff::Field::newByte("GenericType", 0))
            .field(Gff::Field::newByte("Static", 0))
            .field(Gff::Field::newCExoString("LinkedTo", linkedTo))
            .field(Gff::Field::newResRef("LinkedToModule", module))
            .field(Gff::Field::newFloat("X", 0.0f))
            .field(Gff::Field::newFloat("Y", 0.0f))
            .field(Gff::Field::newFloat("Z", 0.0f))
            .build();
    };
    auto door = game->newDoor();
    door->deserialize(*doorGff("WP_Exit", ""), SerializedIdentityContext::templateResource());
    EXPECT_EQ(waypoint->id(), call("GetTransitionTarget", {object(door)}).objectId);

    auto otherModule = game->newDoor();
    otherModule->deserialize(*doorGff("wp_exit", "elsewhere"), SerializedIdentityContext::templateResource());
    EXPECT_EQ(kObjectInvalid, call("GetTransitionTarget", {object(otherModule)}).objectId);

    auto dangling = game->newDoor();
    dangling->deserialize(*doorGff("no_such_tag", ""), SerializedIdentityContext::templateResource());
    EXPECT_EQ(kObjectInvalid, call("GetTransitionTarget", {object(dangling)}).objectId);

    EXPECT_EQ(kObjectInvalid, call("GetTransitionTarget", {object(addCreature())}).objectId);
}

// Area fog

TEST_P(Tranche3Routines, area_fog_colour_is_stored_clamped_and_pushed_to_the_scene) {
    scene::FogProperties pushed;
    EXPECT_CALL(sceneGraph, setFog(_)).WillOnce(SaveArg<0>(&pushed));
    call("SetAreaFogColor", {object(area), Variable::ofFloat(0.25f), Variable::ofFloat(2.0f), Variable::ofFloat(-1.0f)});
    EXPECT_EQ(glm::vec3(0.25f, 1.0f, 0.0f), area->fogColor());
    EXPECT_EQ(glm::vec3(0.25f, 1.0f, 0.0f), pushed.color);
}

INSTANTIATE_TEST_SUITE_P(BothGames, Tranche3Routines, Values(GameID::KotOR, GameID::TSL));
