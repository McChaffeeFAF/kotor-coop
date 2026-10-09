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

// NWScript routines implemented in tranche 1 (docs/routines_missing.md): math,
// strings, vectors, unit conversion, location distance, lock and item queries.
// Each runs against K1 and K2, which register the same indices.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "../fixtures/engine.h"
#include "../fixtures/resource.h"

#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/door.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
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

class Tranche1Routines : public TestWithParam<GameID> {
protected:
    Tranche1Routines() :
        game(GetParam(), "", engine.options(), engine.services(), console),
        routines(GetParam(), &game, &engine.services()) {
        // Doors and placeables look their model up by appearance row; one model-less row each.
        for (const char *table : {"genericdoors", "placeables"}) {
            TwoDA::Builder builder;
            builder.columns({"modelname"});
            builder.row({""});
            ON_CALL(engine.resourceModule().twoDas(), get(std::string(table)))
                .WillByDefault(Return(std::shared_ptr<TwoDA>(builder.build())));
        }
        routines.init();
    }

    Variable call(const std::string &name, std::vector<Variable> args = {}) {
        int index = routines.getIndexByName(name);
        EXPECT_GE(index, 0) << name << " is not registered";
        return routines.get(index).invoke(args, ctx);
    }

    float number(const std::string &name, std::vector<Variable> args) { return call(name, std::move(args)).floatValue; }
    std::string text(const std::string &name, std::vector<Variable> args) { return call(name, std::move(args)).strValue; }
    int integer(const std::string &name, std::vector<Variable> args) { return call(name, std::move(args)).intValue; }

    static Variable f(float v) { return Variable::ofFloat(v); }
    static Variable s(std::string v) { return Variable::ofString(std::move(v)); }
    static Variable i(int v) { return Variable::ofInt(v); }
    static Variable location(float x, float y, float z) {
        return Variable::ofLocation(std::make_shared<Location>(glm::vec3(x, y, z), 0.0f));
    }

    // Its own engine: the shared one keeps the expectations earlier tests left on its mocks.
    struct InitedEngine : TestEngine {
        InitedEngine() { init(); }
    } engine;
    StubConsole console;
    Game game;
    Routines routines;
    ExecutionContext ctx;
};

} // namespace

// Math

TEST_P(Tranche1Routines, fabs_drops_the_sign) {
    EXPECT_FLOAT_EQ(2.5f, number("fabs", {f(-2.5f)}));
    EXPECT_FLOAT_EQ(2.5f, number("fabs", {f(2.5f)}));
    EXPECT_FLOAT_EQ(0.0f, number("fabs", {f(0.0f)}));
}

TEST_P(Tranche1Routines, trigonometry_works_in_radians) {
    const float pi = 3.14159265f;
    EXPECT_NEAR(1.0f, number("cos", {f(0.0f)}), 1e-6);
    EXPECT_NEAR(-1.0f, number("cos", {f(pi)}), 1e-6);
    EXPECT_NEAR(1.0f, number("sin", {f(pi / 2)}), 1e-6);
    EXPECT_NEAR(0.0f, number("sin", {f(0.0f)}), 1e-6);
    EXPECT_NEAR(1.0f, number("tan", {f(pi / 4)}), 1e-5);
    EXPECT_NEAR(pi / 4, number("atan", {f(1.0f)}), 1e-6);
    EXPECT_NEAR(pi / 2, number("acos", {f(0.0f)}), 1e-6);
    EXPECT_NEAR(pi / 2, number("asin", {f(1.0f)}), 1e-6);
}

TEST_P(Tranche1Routines, arc_functions_give_zero_outside_their_domain) {
    for (float v : {1.0001f, -1.0001f, 10.0f, -1e9f}) {
        EXPECT_FLOAT_EQ(0.0f, number("acos", {f(v)})) << v;
        EXPECT_FLOAT_EQ(0.0f, number("asin", {f(v)})) << v;
    }
    EXPECT_NEAR(0.0f, number("acos", {f(1.0f)}), 1e-6);
    EXPECT_NEAR(3.14159265f, number("acos", {f(-1.0f)}), 1e-6);
}

TEST_P(Tranche1Routines, log_is_natural_and_zero_for_non_positive_values) {
    EXPECT_NEAR(1.0f, number("log", {f(2.71828183f)}), 1e-6);
    EXPECT_NEAR(0.0f, number("log", {f(1.0f)}), 1e-6);
    EXPECT_FLOAT_EQ(0.0f, number("log", {f(0.0f)}));
    EXPECT_FLOAT_EQ(0.0f, number("log", {f(-3.0f)}));
}

TEST_P(Tranche1Routines, pow_raises_to_a_power_and_zero_for_zero_to_a_negative_power) {
    EXPECT_FLOAT_EQ(8.0f, number("pow", {f(2.0f), f(3.0f)}));
    EXPECT_FLOAT_EQ(0.25f, number("pow", {f(2.0f), f(-2.0f)}));
    EXPECT_FLOAT_EQ(1.0f, number("pow", {f(5.0f), f(0.0f)}));
    EXPECT_FLOAT_EQ(0.0f, number("pow", {f(0.0f), f(-1.0f)}));
    EXPECT_FLOAT_EQ(0.0f, number("pow", {f(0.0f), f(2.0f)}));
}

TEST_P(Tranche1Routines, sqrt_is_zero_for_a_negative_value_rather_than_nan) {
    EXPECT_FLOAT_EQ(3.0f, number("sqrt", {f(9.0f)}));
    EXPECT_FLOAT_EQ(0.0f, number("sqrt", {f(0.0f)}));
    EXPECT_FLOAT_EQ(0.0f, number("sqrt", {f(-4.0f)}));
}

TEST_P(Tranche1Routines, math_never_returns_nan_for_ordinary_inputs) {
    for (const char *name : {"fabs", "cos", "sin", "tan", "acos", "asin", "atan", "log", "sqrt"}) {
        for (float v : {-1e30f, -2.0f, -1.0f, 0.0f, 0.5f, 1.0f, 2.0f, 1e30f}) {
            EXPECT_FALSE(std::isnan(number(name, {f(v)}))) << name << "(" << v << ")";
        }
    }
}

// Strings

TEST_P(Tranche1Routines, case_conversion_covers_letters_only) {
    EXPECT_EQ("HELLO, WORLD 42!", text("GetStringUpperCase", {s("Hello, World 42!")}));
    EXPECT_EQ("hello, world 42!", text("GetStringLowerCase", {s("Hello, World 42!")}));
    EXPECT_EQ("", text("GetStringUpperCase", {s("")}));
    EXPECT_EQ("", text("GetStringLowerCase", {s("")}));
    // Bytes above 0x7f are not letters in the C locale and pass through.
    EXPECT_EQ("\xe9\xc9", text("GetStringUpperCase", {s("\xe9\xc9")}));
}

TEST_P(Tranche1Routines, string_to_float_reads_a_number_and_gives_zero_otherwise) {
    EXPECT_FLOAT_EQ(3.5f, number("StringToFloat", {s("3.5")}));
    EXPECT_FLOAT_EQ(-0.25f, number("StringToFloat", {s("-0.25")}));
    EXPECT_FLOAT_EQ(12.0f, number("StringToFloat", {s("12")}));
    EXPECT_FLOAT_EQ(0.0f, number("StringToFloat", {s("")}));
    EXPECT_FLOAT_EQ(0.0f, number("StringToFloat", {s("abc")}));
    EXPECT_FLOAT_EQ(1.5f, number("StringToFloat", {s("1.5abc")}));
}

// Vectors, units, locations

TEST_P(Tranche1Routines, vector_magnitude_is_the_length) {
    EXPECT_FLOAT_EQ(5.0f, number("VectorMagnitude", {Variable::ofVector(glm::vec3(3.0f, 4.0f, 0.0f))}));
    EXPECT_FLOAT_EQ(0.0f, number("VectorMagnitude", {Variable::ofVector(glm::vec3(0.0f))}));
    EXPECT_NEAR(std::sqrt(3.0f), number("VectorMagnitude", {Variable::ofVector(glm::vec3(1.0f, -1.0f, 1.0f))}), 1e-6);
}

TEST_P(Tranche1Routines, vector_to_angle_is_degrees_counter_clockwise_from_east) {
    auto angle = [&](float x, float y) { return number("VectorToAngle", {Variable::ofVector(glm::vec3(x, y, 0.0f))}); };
    EXPECT_NEAR(0.0f, angle(1.0f, 0.0f), 1e-4);
    EXPECT_NEAR(90.0f, angle(0.0f, 1.0f), 1e-4);
    EXPECT_NEAR(180.0f, angle(-1.0f, 0.0f), 1e-4);
    EXPECT_NEAR(270.0f, angle(0.0f, -1.0f), 1e-4);
    EXPECT_NEAR(45.0f, angle(2.0f, 2.0f), 1e-4);
    EXPECT_NEAR(315.0f, angle(1.0f, -1.0f), 1e-4);
    EXPECT_GE(angle(1.0f, -1e-6f), 0.0f);
    EXPECT_LT(angle(1.0f, -1e-6f), 360.0f);
}

TEST_P(Tranche1Routines, length_conversions_use_the_international_units) {
    EXPECT_NEAR(0.3048f, number("FeetToMeters", {f(1.0f)}), 1e-6);
    EXPECT_NEAR(3.048f, number("FeetToMeters", {f(10.0f)}), 1e-5);
    EXPECT_NEAR(0.9144f, number("YardsToMeters", {f(1.0f)}), 1e-6);
    EXPECT_NEAR(0.0f, number("YardsToMeters", {f(0.0f)}), 1e-9);
}

TEST_P(Tranche1Routines, a_turn_is_sixty_seconds) {
    EXPECT_FLOAT_EQ(60.0f, number("TurnsToSeconds", {i(1)}));
    EXPECT_FLOAT_EQ(300.0f, number("TurnsToSeconds", {i(5)}));
    EXPECT_FLOAT_EQ(0.0f, number("TurnsToSeconds", {i(0)}));
}

TEST_P(Tranche1Routines, location_distances_are_euclidean_in_three_and_two_dimensions) {
    auto a = location(0.0f, 0.0f, 0.0f);
    auto b = location(3.0f, 4.0f, 12.0f);
    EXPECT_FLOAT_EQ(13.0f, number("GetDistanceBetweenLocations", {a, b}));
    EXPECT_FLOAT_EQ(5.0f, number("GetDistanceBetweenLocations2D", {a, b}));
    EXPECT_FLOAT_EQ(13.0f, number("GetDistanceBetweenLocations", {b, a})) << "symmetric";
    EXPECT_FLOAT_EQ(0.0f, number("GetDistanceBetweenLocations", {b, b}));
    EXPECT_FLOAT_EQ(0.0f, number("GetDistanceBetweenLocations2D", {b, b}));
}

// Locks and items

TEST_P(Tranche1Routines, lock_queries_read_a_doors_lock) {
    auto gff = Gff::Builder()
                   .field(Gff::Field::newByte("GenericType", 0))
                   .field(Gff::Field::newByte("Static", 0))
                   .field(Gff::Field::newByte("Locked", 1))
                   .field(Gff::Field::newByte("Lockable", 1))
                   .field(Gff::Field::newByte("KeyRequired", 1))
                   .field(Gff::Field::newCExoString("KeyName", "Test_Key"))
                   .field(Gff::Field::newByte("OpenLockDC", 27))
                   .field(Gff::Field::newByte("CloseLockDC", 31))
                   .field(Gff::Field::newFloat("X", 0.0f))
                   .field(Gff::Field::newFloat("Y", 0.0f))
                   .field(Gff::Field::newFloat("Z", 0.0f))
                   .build();
    auto door = game.newDoor();
    door->deserialize(*gff, SerializedIdentityContext::templateResource());
    auto id = Variable::ofObject(door->id());

    EXPECT_EQ(1, integer("GetLockKeyRequired", {id}));
    EXPECT_EQ("test_key", text("GetLockKeyTag", {id}));
    EXPECT_EQ(1, integer("GetLockLockable", {id}));
    EXPECT_EQ(27, integer("GetLockUnlockDC", {id}));
    EXPECT_EQ(31, integer("GetLockLockDC", {id}));
}

TEST_P(Tranche1Routines, lock_queries_read_a_placeables_lock) {
    auto gff = Gff::Builder()
                   .field(Gff::Field::newByte("Lockable", 1))
                   .field(Gff::Field::newByte("Locked", 1))
                   .field(Gff::Field::newByte("KeyRequired", 0))
                   .field(Gff::Field::newCExoString("KeyName", "chest_key"))
                   .field(Gff::Field::newByte("OpenLockDCk", 12))
                   .field(Gff::Field::newByte("CloseLockDC", 14))
                   .field(Gff::Field::newFloat("X", 0.0f))
                   .field(Gff::Field::newFloat("Y", 0.0f))
                   .field(Gff::Field::newFloat("Z", 0.0f))
                   .build();
    auto placeable = game.newPlaceable();
    placeable->deserialize(*gff, SerializedIdentityContext::templateResource());
    auto id = Variable::ofObject(placeable->id());

    EXPECT_EQ(0, integer("GetLockKeyRequired", {id}));
    EXPECT_EQ("chest_key", text("GetLockKeyTag", {id}));
    EXPECT_EQ(1, integer("GetLockLockable", {id}));
    EXPECT_EQ(12, integer("GetLockUnlockDC", {id}));
    EXPECT_EQ(14, integer("GetLockLockDC", {id}));
}

TEST_P(Tranche1Routines, lock_queries_on_anything_without_a_lock_give_the_empty_answer) {
    auto creature = game.newCreature();
    auto id = Variable::ofObject(creature->id());

    EXPECT_EQ(0, integer("GetLockKeyRequired", {id}));
    EXPECT_EQ("", text("GetLockKeyTag", {id}));
    EXPECT_EQ(0, integer("GetLockLockable", {id}));
    EXPECT_EQ(0, integer("GetLockUnlockDC", {id}));
    EXPECT_EQ(0, integer("GetLockLockDC", {id}));
}

TEST_P(Tranche1Routines, get_lock_key_tag_returns_a_string) {
    // The scripts declare `string GetLockKeyTag(object)`; the VM sizes the value it
    // pushes from the registered return type, so it must be a string.
    EXPECT_EQ(VariableType::String, routines.get(routines.getIndexByName("GetLockKeyTag")).returnType());
}

TEST_P(Tranche1Routines, identified_reads_and_writes_an_items_flag) {
    auto item = game.newItem();
    auto id = Variable::ofObject(item->id());

    call("SetIdentified", {id, i(0)});
    EXPECT_EQ(0, integer("GetIdentified", {id}));
    call("SetIdentified", {id, i(1)});
    EXPECT_EQ(1, integer("GetIdentified", {id}));
    EXPECT_TRUE(item->isIdentified());
}

TEST_P(Tranche1Routines, print_routines_only_write_to_the_log_and_return_nothing) {
    auto creature = game.newCreature();
    EXPECT_EQ(VariableType::Void, call("PrintInteger", {i(-7)}).type);
    EXPECT_EQ(VariableType::Void, call("PrintFloat", {f(1.5f), i(8), i(3)}).type);
    EXPECT_EQ(VariableType::Void, call("PrintFloat", {f(-1e30f), i(1000), i(1000)}).type) << "oversized width and decimals are bounded";
    EXPECT_EQ(VariableType::Void, call("PrintObject", {Variable::ofObject(creature->id())}).type);
    EXPECT_EQ(VariableType::Void, call("PrintVector", {Variable::ofVector(glm::vec3(1.0f, 2.0f, 3.0f)), i(1)}).type);
    EXPECT_EQ(VariableType::Void, call("PrintVector", {Variable::ofVector(glm::vec3(0.0f)), i(0)}).type);
}

INSTANTIATE_TEST_SUITE_P(RetailGames, Tranche1Routines, Values(GameID::KotOR, GameID::TSL));
