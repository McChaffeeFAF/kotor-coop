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

// Campaign batch C09: AI and combat routines (objects in a shape,
// encounters, GetIsInCombat, GetLastAttacker). The notes these pin are in
// design note c09_ai_combat.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"

#include "reone/game/action/usetalentonobject.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/encounter.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/game/shapequery.h"
#include "reone/game/talent.h"
#include "reone/resource/gff.h"
#include "reone/scene/collision.h"
#include "reone/script/executioncontext.h"
#include "reone/script/program.h"
#include "reone/system/randomutil.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

constexpr int kShapeSpellCylinder = 0;
constexpr int kShapeCone = 1;
constexpr int kShapeCube = 2;
constexpr int kShapeSpellCone = 3;
constexpr int kShapeSphere = 4;

constexpr int kObjectTypeCreature = 1;
constexpr int kObjectTypePlaceable = 64;
constexpr int kObjectTypeAll = 32767;

ShapeQuery query(int shape, float size, glm::vec3 target, glm::vec3 origin = glm::vec3(0.0f)) {
    ShapeQuery q;
    q.shape = shape;
    q.size = size;
    q.target = target;
    q.origin = origin;
    prepareShapeQuery(q);
    return q;
}

// --- Shape rules ---------------------------------------------------------------

TEST(C09ShapeQuery, sphere_is_a_closed_3d_ball_around_the_target) {
    auto q = query(kShapeSphere, 5.0f, glm::vec3(10.0f, 0.0f, 0.0f));
    EXPECT_TRUE(shapeContains(q, glm::vec3(15.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(shapeContains(q, glm::vec3(10.0f, 3.0f, 3.0f)));
    EXPECT_FALSE(shapeContains(q, glm::vec3(10.0f, 4.0f, 4.0f)));
    EXPECT_FALSE(shapeContains(q, glm::vec3(10.0f, 0.0f, 5.1f)));
}

TEST(C09ShapeQuery, unknown_shape_behaves_as_a_sphere) {
    auto q = query(9, 2.0f, glm::vec3(0.0f));
    EXPECT_TRUE(shapeContains(q, glm::vec3(1.9f, 0.0f, 0.0f)));
    EXPECT_FALSE(shapeContains(q, glm::vec3(2.1f, 0.0f, 0.0f)));
}

TEST(C09ShapeQuery, cube_size_is_half_the_side_and_the_far_faces_are_open) {
    auto q = query(kShapeCube, 2.0f, glm::vec3(0.0f));
    EXPECT_TRUE(shapeContains(q, glm::vec3(-2.0f, -2.0f, -2.0f)));
    EXPECT_TRUE(shapeContains(q, glm::vec3(1.9f, 1.9f, 1.9f)));
    EXPECT_FALSE(shapeContains(q, glm::vec3(2.0f, 0.0f, 0.0f)));
    EXPECT_FALSE(shapeContains(q, glm::vec3(0.0f, 0.0f, 2.5f)));
}

TEST(C09ShapeQuery, spell_cylinder_runs_size_metres_from_origin_with_fixed_width) {
    // Aimed along +x at a target 3 m away; the cylinder is still 10 m long.
    auto q = query(kShapeSpellCylinder, 10.0f, glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(0.0f));
    EXPECT_NEAR(10.0f, q.end.x, 1e-4f);
    EXPECT_TRUE(shapeContains(q, glm::vec3(8.0f, 1.4f, 0.0f)));
    EXPECT_FALSE(shapeContains(q, glm::vec3(8.0f, 1.6f, 0.0f)));
    EXPECT_FALSE(shapeContains(q, glm::vec3(10.5f, 0.0f, 0.0f)));
    EXPECT_FALSE(shapeContains(q, glm::vec3(-1.0f, 0.0f, 0.0f)));
}

TEST(C09ShapeQuery, spell_cone_is_sixty_degrees_wide_and_size_long) {
    auto q = query(kShapeSpellCone, 10.0f, glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(0.0f));
    EXPECT_TRUE(shapeContains(q, glm::vec3(8.0f, 4.0f, 0.0f)));   // ~26.6 degrees
    EXPECT_FALSE(shapeContains(q, glm::vec3(5.0f, 4.0f, 0.0f)));  // ~38.7 degrees
    EXPECT_FALSE(shapeContains(q, glm::vec3(10.5f, 0.0f, 0.0f)));
    EXPECT_FALSE(shapeContains(q, glm::vec3(-3.0f, 0.0f, 0.0f)));
}

TEST(C09ShapeQuery, cone_widens_to_size_at_the_target) {
    auto q = query(kShapeCone, 4.0f, glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(0.0f));
    EXPECT_TRUE(shapeContains(q, glm::vec3(10.0f, 3.9f, 0.0f)));
    EXPECT_TRUE(shapeContains(q, glm::vec3(5.0f, 1.9f, 0.0f)));
    EXPECT_FALSE(shapeContains(q, glm::vec3(5.0f, 2.1f, 0.0f)));
    EXPECT_FALSE(shapeContains(q, glm::vec3(-5.0f, 0.0f, 0.0f)));
}

TEST(C09ShapeQuery, x_search_range_covers_origin_and_target_for_cones) {
    auto sphere = query(kShapeSphere, 2.0f, glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(0.0f));
    EXPECT_FLOAT_EQ(8.0f, sphere.minX);
    EXPECT_FLOAT_EQ(12.0f, sphere.maxX);
    auto cone = query(kShapeSpellCone, 2.0f, glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(0.0f));
    EXPECT_FLOAT_EQ(-2.0f, cone.minX);
    EXPECT_FLOAT_EQ(12.0f, cone.maxX);
}

TEST(C09ShapeQuery, object_filter_is_a_type_mask_and_all_accepts_anything) {
    EXPECT_TRUE(shapeFilterAccepts(kObjectTypeCreature, 1));
    EXPECT_FALSE(shapeFilterAccepts(kObjectTypeCreature, 64));
    EXPECT_TRUE(shapeFilterAccepts(kObjectTypeCreature | kObjectTypePlaceable, 64));
    EXPECT_FALSE(shapeFilterAccepts(kObjectTypeCreature | kObjectTypePlaceable, 8));
    EXPECT_TRUE(shapeFilterAccepts(kObjectTypeAll, 0x1001));
}

// --- Routine fixture -----------------------------------------------------------

class C09Test : public Test {
protected:
    void SetUp() override {
        requests().clear();
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _game->initLocalServices();
        _routines = std::make_unique<Routines>(GameID::KotOR, _game.get(), &_engine.services());
        _routines->init();
        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(_sceneGraph));
        // Walkable ground everywhere except x > 100 (for spawn searches).
        ON_CALL(_sceneGraph, testElevation(_, _))
            .WillByDefault(Invoke([](const glm::vec3 &position, scene::Collision &collision) {
                if (position.x > 100.0f) {
                    return false;
                }
                collision.intersection = glm::vec3(position.x, position.y, 0.0f);
                collision.user = nullptr;
                return true;
            }));
        ON_CALL(_engine.gameModule().reputes(), getIsEnemy(An<Faction>(), An<Faction>()))
            .WillByDefault(Invoke([](Faction source, Faction target) {
                bool sourceHostile = source == Faction::Hostile1;
                bool targetHostile = target == Faction::Hostile1;
                return sourceHostile != targetHostile;
            }));
        EXPECT_CALL(_engine.resourceModule().scripts(), get(_))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([](const std::string &key) {
                requests().push_back(key);
                return std::shared_ptr<script::ScriptProgram>();
            }));
        _area = _game->newArea();
        TestGameModule::setActiveModuleArea(*_game, _area);
    }

    static std::vector<std::string> &requests() {
        static std::vector<std::string> log;
        return log;
    }

    int requestCount(const std::string &resRef) const {
        return static_cast<int>(std::count(requests().begin(), requests().end(), resRef));
    }

    std::shared_ptr<Creature> addCreature(const std::string &tag, glm::vec3 position, Faction faction = Faction::Hostile1) {
        auto creature = _game->newCreature();
        creature->setTag(tag);
        creature->setPosition(position);
        creature->setFaction(faction);
        _area->add(creature);
        return creature;
    }

    std::shared_ptr<Creature> addPlayer(glm::vec3 position) {
        auto creature = addCreature("player", position, Faction::Player);
        creature->setCurrentHitPoints(10);
        _game->party().addMember(kNpcPlayer, creature);
        _game->party().setPlayer(creature);
        return creature;
    }

    Variable call(const std::string &name, std::vector<Variable> args, const std::shared_ptr<Object> &caller = nullptr,
                  std::vector<Argument> extra = {}) {
        if (caller) {
            _execution.args.erase(
                std::remove_if(_execution.args.begin(), _execution.args.end(), [](const Argument &arg) {
                    return arg.kind == ArgKind::Caller;
                }),
                _execution.args.end());
            _execution.args.insert(_execution.args.begin(), Argument(ArgKind::Caller, Variable::ofObject(caller->id())));
        }
        for (auto &arg : extra) {
            _execution.args.push_back(arg);
        }
        _execution.routines = _routines.get();
        return _routines->get(_routines->getIndexByName(name)).invoke(args, _execution);
    }

    std::vector<Variable> shapeArgs(int shape, float size, glm::vec3 target, int lineOfSight, int filter, glm::vec3 origin = glm::vec3(0.0f)) {
        return {Variable::ofInt(shape),
                Variable::ofFloat(size),
                Variable::ofLocation(std::make_shared<Location>(target, 0.0f)),
                Variable::ofInt(lineOfSight),
                Variable::ofInt(filter),
                Variable::ofVector(origin)};
    }

    std::vector<uint32_t> iterateShape(std::vector<Variable> args, const std::shared_ptr<Object> &caller = nullptr) {
        std::vector<uint32_t> result;
        uint32_t id = call("GetFirstObjectInShape", args, caller).objectId;
        while (id != kObjectInvalid && result.size() < 32) {
            result.push_back(id);
            id = call("GetNextObjectInShape", args, caller).objectId;
        }
        return result;
    }

    std::shared_ptr<Encounter> addEncounter(std::vector<Gff::Field> extra) {
        auto encounter = _game->newEncounter();
        std::vector<Gff::Field> fields {
            Gff::Field::newInt("Faction", static_cast<int>(Faction::Hostile1)),
            Gff::Field::newFloat("XPosition", 0.0f),
            Gff::Field::newFloat("YPosition", 0.0f),
            Gff::Field::newList("Geometry", squareGeometry(10.0f))};
        for (auto &field : extra) {
            fields.push_back(std::move(field));
        }
        auto gff = std::make_shared<Gff>(0xffffffff, std::move(fields));
        encounter->deserialize(*gff, SerializedIdentityContext::templateResource());
        _area->add(encounter);
        return encounter;
    }

    static std::vector<std::shared_ptr<Gff>> squareGeometry(float side) {
        std::vector<std::shared_ptr<Gff>> points;
        for (auto [px, py] : std::vector<std::pair<float, float>> {{0, 0}, {side, 0}, {side, side}, {0, side}}) {
            points.push_back(Gff::Builder()
                                 .field(Gff::Field::newFloat("X", px))
                                 .field(Gff::Field::newFloat("Y", py))
                                 .field(Gff::Field::newFloat("Z", 0.0f))
                                 .build());
        }
        return points;
    }

    static std::vector<std::shared_ptr<Gff>> creatureList(std::vector<std::tuple<std::string, float, bool>> rows) {
        std::vector<std::shared_ptr<Gff>> list;
        for (auto &[resRef, cr, single] : rows) {
            list.push_back(Gff::Builder()
                               .field(Gff::Field::newResRef("ResRef", resRef))
                               .field(Gff::Field::newFloat("CR", cr))
                               .field(Gff::Field::newByte("SingleSpawn", single ? 1 : 0))
                               .build());
        }
        return list;
    }

    static std::vector<std::shared_ptr<Gff>> spawnPoints(std::vector<glm::vec3> points) {
        std::vector<std::shared_ptr<Gff>> list;
        for (auto &point : points) {
            list.push_back(Gff::Builder()
                               .field(Gff::Field::newFloat("X", point.x))
                               .field(Gff::Field::newFloat("Y", point.y))
                               .field(Gff::Field::newFloat("Z", point.z))
                               .field(Gff::Field::newFloat("Orientation", 0.0f))
                               .build());
        }
        return list;
    }

    std::vector<std::shared_ptr<Creature>> spawnedBy(const Encounter &encounter) {
        std::vector<std::shared_ptr<Creature>> result;
        for (auto &object : _area->getObjectsByType(ObjectType::Creature)) {
            auto creature = std::static_pointer_cast<Creature>(object);
            if (creature->encounterId() == encounter.id()) {
                result.push_back(creature);
            }
        }
        return result;
    }

    TestEngine _engine;
    StubConsole _console;
    NiceMock<scene::MockSceneGraph> _sceneGraph;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
    std::shared_ptr<Area> _area;
    ExecutionContext _execution;
};

// --- Get{First,Next}ObjectInShape -----------------------------------------------

TEST_F(C09Test, shape_iteration_honours_the_object_filter) {
    auto creature = addCreature("c", glm::vec3(1.0f, 0.0f, 0.0f));
    auto placeable = _game->newPlaceable();
    placeable->setPosition(glm::vec3(2.0f, 0.0f, 0.0f));
    _area->add(placeable);

    auto creaturesOnly = iterateShape(shapeArgs(kShapeSphere, 5.0f, glm::vec3(0.0f), 0, kObjectTypeCreature));
    EXPECT_EQ(std::vector<uint32_t>({creature->id()}), creaturesOnly);

    auto both = iterateShape(shapeArgs(kShapeSphere, 5.0f, glm::vec3(0.0f), 0, kObjectTypeCreature | kObjectTypePlaceable));
    EXPECT_EQ(std::vector<uint32_t>({creature->id(), placeable->id()}), both);
}

TEST_F(C09Test, shape_iteration_walks_objects_in_ascending_x) {
    auto east = addCreature("east", glm::vec3(3.0f, 0.0f, 0.0f));
    auto west = addCreature("west", glm::vec3(-3.0f, 0.0f, 0.0f));
    auto middle = addCreature("middle", glm::vec3(0.5f, 1.0f, 0.0f));
    addCreature("far", glm::vec3(30.0f, 0.0f, 0.0f));

    auto found = iterateShape(shapeArgs(kShapeSphere, 5.0f, glm::vec3(0.0f), 0, kObjectTypeCreature));
    EXPECT_EQ(std::vector<uint32_t>({west->id(), middle->id(), east->id()}), found);
}

TEST_F(C09Test, next_object_in_shape_tests_its_own_arguments_after_the_cursor) {
    auto a = addCreature("a", glm::vec3(1.0f, 0.0f, 0.0f));
    addCreature("b", glm::vec3(2.0f, 0.0f, 0.0f));
    auto c = addCreature("c", glm::vec3(3.0f, 0.0f, 0.0f));

    EXPECT_EQ(a->id(), call("GetFirstObjectInShape", shapeArgs(kShapeSphere, 5.0f, glm::vec3(0.0f), 0, 1)).objectId);
    // A smaller sphere around c skips b but keeps the cursor.
    EXPECT_EQ(c->id(), call("GetNextObjectInShape", shapeArgs(kShapeSphere, 0.5f, glm::vec3(3.0f, 0.0f, 0.0f), 0, 1)).objectId);
    EXPECT_EQ(kObjectInvalid, call("GetNextObjectInShape", shapeArgs(kShapeSphere, 5.0f, glm::vec3(0.0f), 0, 1)).objectId);
}

TEST_F(C09Test, nested_shape_scan_lets_the_outer_scan_carry_on) {
    // GN_FindGrenadeTarget: a 40 m scan that runs a 4 m scan around each
    // candidate. The outer Next resumes after the inner scan's last result.
    auto a = addCreature("a", glm::vec3(1.0f, 0.0f, 0.0f));
    auto b = addCreature("b", glm::vec3(2.0f, 0.0f, 0.0f));
    auto far = addCreature("far", glm::vec3(20.0f, 0.0f, 0.0f));
    auto outer = shapeArgs(kShapeSphere, 40.0f, glm::vec3(0.0f), 0, 1);

    EXPECT_EQ(a->id(), call("GetFirstObjectInShape", outer).objectId);
    auto inner = shapeArgs(kShapeSphere, 4.0f, a->position(), 0, 1);
    EXPECT_EQ(a->id(), call("GetFirstObjectInShape", inner).objectId);
    EXPECT_EQ(b->id(), call("GetNextObjectInShape", inner).objectId);
    EXPECT_EQ(kObjectInvalid, call("GetNextObjectInShape", inner).objectId);

    EXPECT_EQ(far->id(), call("GetNextObjectInShape", outer).objectId);
    EXPECT_EQ(kObjectInvalid, call("GetNextObjectInShape", outer).objectId);
}

TEST_F(C09Test, next_object_in_shape_without_first_is_invalid) {
    addCreature("a", glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(kObjectInvalid, call("GetNextObjectInShape", shapeArgs(kShapeSphere, 5.0f, glm::vec3(0.0f), 0, 1)).objectId);
}

TEST_F(C09Test, line_of_sight_is_only_checked_when_asked) {
    auto behindWall = addCreature("hidden", glm::vec3(4.0f, 0.0f, 0.0f));
    ON_CALL(_sceneGraph, testLineOfSight(_, _, _))
        .WillByDefault(Invoke([](const glm::vec3 &origin, const glm::vec3 &, scene::Collision &collision) {
            collision.user = nullptr;
            collision.intersection = origin + glm::vec3(1.0f, 0.0f, 0.0f);
            return true;
        }));

    EXPECT_EQ(behindWall->id(), call("GetFirstObjectInShape", shapeArgs(kShapeSphere, 5.0f, glm::vec3(0.0f), 0, 1)).objectId);
    EXPECT_EQ(kObjectInvalid, call("GetFirstObjectInShape", shapeArgs(kShapeSphere, 5.0f, glm::vec3(0.0f), 1, 1)).objectId);
}

TEST_F(C09Test, line_of_sight_is_raised_only_vertically) {
    auto target = addCreature("t", glm::vec3(4.0f, 0.0f, 0.0f));
    std::vector<std::pair<glm::vec3, glm::vec3>> rays;
    ON_CALL(_sceneGraph, testLineOfSight(_, _, _))
        .WillByDefault(Invoke([&rays](const glm::vec3 &origin, const glm::vec3 &dest, scene::Collision &) {
            rays.emplace_back(origin, dest);
            return false;
        }));

    EXPECT_EQ(target->id(), call("GetFirstObjectInShape", shapeArgs(kShapeSphere, 5.0f, glm::vec3(0.0f), 1, 1)).objectId);
    ASSERT_EQ(1u, rays.size());
    EXPECT_FLOAT_EQ(0.0f, rays[0].first.x);
    EXPECT_FLOAT_EQ(0.0f, rays[0].first.y);
    EXPECT_FLOAT_EQ(1.7f, rays[0].first.z);
    EXPECT_FLOAT_EQ(4.0f, rays[0].second.x);
    EXPECT_FLOAT_EQ(1.7f, rays[0].second.z);
}

TEST_F(C09Test, spell_cone_starts_at_the_caller_not_vorigin) {
    auto caster = addCreature("caster", glm::vec3(0.0f), Faction::Player);
    auto ahead = addCreature("ahead", glm::vec3(5.0f, 0.5f, 0.0f));
    addCreature("behind", glm::vec3(-5.0f, 0.0f, 0.0f));

    // vOrigin points the other way; the caller's position wins.
    auto found = iterateShape(shapeArgs(kShapeSpellCone, 10.0f, glm::vec3(8.0f, 0.0f, 0.0f), 0, 1, glm::vec3(20.0f, 0.0f, 0.0f)), caster);
    EXPECT_EQ(std::vector<uint32_t>({ahead->id()}), found);
}

// --- Encounters -------------------------------------------------------------------

TEST_F(C09Test, encounter_choice_takes_single_spawns_then_fills_from_the_pool) {
    auto encounter = addEncounter({
        Gff::Field::newInt("MaxCreatures", 4),
        Gff::Field::newInt("RecCreatures", 4),
        Gff::Field::newList("CreatureList", creatureList({{"grunt", 2.0f, false}, {"boss", 8.0f, true}, {"lieutenant", 5.0f, true}}))});
    setRandomSeed(1);

    auto chosen = encounter->chooseSpawns();

    ASSERT_EQ(4u, chosen.size());
    EXPECT_EQ("boss", chosen[0]);
    EXPECT_EQ("lieutenant", chosen[1]);
    EXPECT_EQ("grunt", chosen[2]);
    EXPECT_EQ("grunt", chosen[3]);
}

TEST_F(C09Test, encounter_choice_count_lies_between_rec_and_max) {
    auto encounter = addEncounter({
        Gff::Field::newInt("MaxCreatures", 6),
        Gff::Field::newInt("RecCreatures", 3),
        Gff::Field::newList("CreatureList", creatureList({{"rakghoul", 2.0f, false}, {"rakghoul_big", 3.0f, true}}))});
    std::set<size_t> counts;
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        setRandomSeed(seed);
        auto chosen = encounter->chooseSpawns();
        EXPECT_GE(chosen.size(), 3u);
        EXPECT_LE(chosen.size(), 6u);
        EXPECT_EQ("rakghoul_big", chosen.front());
        EXPECT_EQ(1, std::count(chosen.begin(), chosen.end(), "rakghoul_big"));
        counts.insert(chosen.size());
    }
    EXPECT_GT(counts.size(), 1u);
}

TEST_F(C09Test, encounter_of_only_single_spawns_never_repeats_one) {
    auto encounter = addEncounter({
        Gff::Field::newInt("MaxCreatures", 5),
        Gff::Field::newInt("RecCreatures", 5),
        Gff::Field::newList("CreatureList", creatureList({{"a", 1.0f, true}, {"b", 6.0f, true}}))});
    setRandomSeed(3);
    EXPECT_EQ(std::vector<std::string>({"b", "a"}), encounter->chooseSpawns());
}

TEST_F(C09Test, active_encounter_spawns_when_a_hostile_party_member_enters) {
    auto player = addPlayer(glm::vec3(-20.0f, 0.0f, 0.0f));
    auto encounter = addEncounter({
        Gff::Field::newByte("Active", 1),
        Gff::Field::newByte("PlayerOnly", 1),
        Gff::Field::newInt("MaxCreatures", 2),
        Gff::Field::newInt("RecCreatures", 2),
        Gff::Field::newList("CreatureList", creatureList({{"vulkar", 1.0f, false}})),
        Gff::Field::newList("SpawnPointList", spawnPoints({glm::vec3(30.0f, 5.0f, 0.0f)}))});
    setRandomSeed(1);

    encounter->update(0.0f);
    EXPECT_FALSE(encounter->isStarted());

    player->setPosition(glm::vec3(5.0f, 5.0f, 0.0f));
    encounter->update(0.0f); // trigger, then the first spawn next update
    EXPECT_TRUE(encounter->isStarted());
    encounter->update(0.0f);
    encounter->update(0.0f);

    auto spawned = spawnedBy(*encounter);
    ASSERT_EQ(2u, spawned.size());
    EXPECT_EQ(2, encounter->numberSpawned());
    EXPECT_FLOAT_EQ(30.0f, spawned[0]->position().x);
    EXPECT_EQ(1, call("GetIsEncounterCreature", {Variable::ofObject(spawned[0]->id())}).intValue);
    EXPECT_EQ(0, call("GetIsEncounterCreature", {Variable::ofObject(player->id())}).intValue);
}

TEST_F(C09Test, player_only_encounter_ignores_a_hostile_non_party_creature) {
    auto encounter = addEncounter({
        Gff::Field::newByte("Active", 1),
        Gff::Field::newByte("PlayerOnly", 1),
        Gff::Field::newInt("MaxCreatures", 1),
        Gff::Field::newInt("RecCreatures", 1),
        Gff::Field::newList("CreatureList", creatureList({{"x", 1.0f, false}}))});
    addCreature("stranger", glm::vec3(5.0f, 5.0f, 0.0f), Faction::Player);
    encounter->update(0.0f);
    EXPECT_FALSE(encounter->isStarted());
}

TEST_F(C09Test, encounter_is_not_set_off_by_a_creature_friendly_to_its_faction) {
    auto encounter = addEncounter({
        Gff::Field::newByte("Active", 1),
        Gff::Field::newByte("PlayerOnly", 0),
        Gff::Field::newInt("MaxCreatures", 1),
        Gff::Field::newInt("RecCreatures", 1),
        Gff::Field::newList("CreatureList", creatureList({{"x", 1.0f, false}}))});
    addCreature("ally", glm::vec3(5.0f, 5.0f, 0.0f), Faction::Hostile1);
    encounter->update(0.0f);
    EXPECT_FALSE(encounter->isStarted());
}

TEST_F(C09Test, set_encounter_active_spawns_at_once_for_an_occupant) {
    auto player = addPlayer(glm::vec3(5.0f, 5.0f, 0.0f));
    auto encounter = addEncounter({
        Gff::Field::newByte("Active", 0),
        Gff::Field::newByte("PlayerOnly", 1),
        Gff::Field::newInt("MaxCreatures", 1),
        Gff::Field::newInt("RecCreatures", 1),
        Gff::Field::newList("CreatureList", creatureList({{"shyrack", 6.0f, false}}))});
    encounter->update(0.0f); // the player is now an occupant; inactive, so nothing
    EXPECT_FALSE(encounter->isStarted());
    EXPECT_EQ(0, call("GetEncounterActive", {Variable::ofObject(encounter->id())}).intValue);

    call("SetEncounterActive", {Variable::ofInt(1), Variable::ofObject(encounter->id())});

    EXPECT_EQ(1, call("GetEncounterActive", {Variable::ofObject(encounter->id())}).intValue);
    EXPECT_TRUE(encounter->isStarted());
    EXPECT_EQ(1u, encounter->pendingSpawns().size());
}

TEST_F(C09Test, set_encounter_active_on_a_creature_does_nothing) {
    auto creature = addCreature("end_bridgesith", glm::vec3(0.0f));
    call("SetEncounterActive", {Variable::ofInt(1), Variable::ofObject(creature->id())});
    EXPECT_EQ(0, call("GetEncounterActive", {Variable::ofObject(creature->id())}).intValue);
}

TEST_F(C09Test, set_encounter_active_defaults_to_the_caller) {
    auto encounter = addEncounter({Gff::Field::newByte("Active", 0)});
    call("SetEncounterActive", {Variable::ofInt(1), Variable::ofObject(kObjectSelf)}, encounter);
    EXPECT_TRUE(encounter->isActive());
}

TEST_F(C09Test, last_spawned_death_exhausts_and_switches_the_encounter_off) {
    auto player = addPlayer(glm::vec3(5.0f, 5.0f, 0.0f));
    auto encounter = addEncounter({
        Gff::Field::newByte("Active", 1),
        Gff::Field::newByte("PlayerOnly", 1),
        Gff::Field::newInt("MaxCreatures", 2),
        Gff::Field::newInt("RecCreatures", 2),
        Gff::Field::newResRef("OnExhausted", "c09_exhausted"),
        Gff::Field::newList("CreatureList", creatureList({{"g", 1.0f, false}}))});
    for (int i = 0; i < 4; ++i) {
        encounter->update(0.0f);
    }
    auto spawned = spawnedBy(*encounter);
    ASSERT_EQ(2u, spawned.size());

    spawned[0]->notifyEncounterGone(true);
    spawned[0]->notifyEncounterGone(true); // counted once
    EXPECT_EQ(1, encounter->numberSpawned());
    EXPECT_EQ(0, requestCount("c09_exhausted"));

    spawned[1]->notifyEncounterGone(true);
    EXPECT_EQ(0, encounter->numberSpawned());
    EXPECT_EQ(1, requestCount("c09_exhausted"));
    EXPECT_FALSE(encounter->isActive());
    EXPECT_FALSE(encounter->isStarted());

    // Switched off, it does not respawn while the player stays inside.
    encounter->update(0.0f);
    EXPECT_EQ(2u, spawnedBy(*encounter).size());
}

TEST_F(C09Test, removing_a_spawned_creature_alive_does_not_exhaust) {
    addPlayer(glm::vec3(5.0f, 5.0f, 0.0f));
    auto encounter = addEncounter({
        Gff::Field::newByte("Active", 1),
        Gff::Field::newByte("PlayerOnly", 1),
        Gff::Field::newInt("MaxCreatures", 1),
        Gff::Field::newInt("RecCreatures", 1),
        Gff::Field::newResRef("OnExhausted", "c09_exhausted"),
        Gff::Field::newList("CreatureList", creatureList({{"g", 1.0f, false}}))});
    encounter->update(0.0f);
    encounter->update(0.0f);
    auto spawned = spawnedBy(*encounter);
    ASSERT_EQ(1u, spawned.size());

    spawned[0]->notifyEncounterGone(false);

    EXPECT_EQ(0, encounter->numberSpawned());
    EXPECT_EQ(0, requestCount("c09_exhausted"));
    EXPECT_TRUE(encounter->isActive());
}

TEST_F(C09Test, resetting_encounter_rearms_after_reset_time) {
    auto player = addPlayer(glm::vec3(5.0f, 5.0f, 0.0f));
    auto encounter = addEncounter({
        Gff::Field::newByte("Active", 1),
        Gff::Field::newByte("PlayerOnly", 1),
        Gff::Field::newByte("Reset", 1),
        Gff::Field::newInt("ResetTime", 30),
        Gff::Field::newInt("Respawns", -1),
        Gff::Field::newInt("SpawnOption", 1),
        Gff::Field::newInt("MaxCreatures", 1),
        Gff::Field::newInt("RecCreatures", 1),
        Gff::Field::newList("CreatureList", creatureList({{"g", 1.0f, false}}))});
    encounter->update(0.0f);
    encounter->update(0.0f);
    spawnedBy(*encounter)[0]->notifyEncounterGone(true);
    EXPECT_FALSE(encounter->isActive());

    EXPECT_TRUE(encounter->isResetPending());
    // K2 behaviour: the timer runs on game time from the exhaustion.
    TestGameModule::advanceWorldTime(*_game, 29.0f);
    encounter->update(0.0f);
    EXPECT_FALSE(encounter->isActive());
    TestGameModule::advanceWorldTime(*_game, 2.0f);
    encounter->update(0.0f); // re-armed; the player inside sets it off again
    EXPECT_TRUE(encounter->isActive());
    EXPECT_TRUE(encounter->isStarted());
    encounter->update(0.0f);
    EXPECT_EQ(2u, spawnedBy(*encounter).size());
}

TEST_F(C09Test, encounter_spawns_stand_on_free_ground_near_their_point) {
    auto player = addPlayer(glm::vec3(5.0f, 5.0f, 0.0f));
    auto encounter = addEncounter({
        Gff::Field::newByte("Active", 1),
        Gff::Field::newByte("PlayerOnly", 1),
        Gff::Field::newInt("MaxCreatures", 3),
        Gff::Field::newInt("RecCreatures", 3),
        Gff::Field::newList("CreatureList", creatureList({{"g", 1.0f, false}})),
        Gff::Field::newList("SpawnPointList", spawnPoints({glm::vec3(30.0f, 5.0f, 0.0f)}))});
    for (int i = 0; i < 4; ++i) {
        encounter->update(0.0f);
    }
    auto spawned = spawnedBy(*encounter);
    ASSERT_EQ(3u, spawned.size());
    // K2 behaviour: the point itself, then the nearest free ring spot.
    EXPECT_FLOAT_EQ(30.0f, spawned[0]->position().x);
    EXPECT_FLOAT_EQ(5.0f, spawned[0]->position().y);
    for (size_t i = 1; i < spawned.size(); ++i) {
        float offset = glm::distance(glm::vec2(spawned[i]->position()), glm::vec2(30.0f, 5.0f));
        EXPECT_GE(offset, 1.0f);
        EXPECT_LT(offset, 20.0f * 1.5f);
        for (size_t j = 0; j < i; ++j) {
            EXPECT_GT(glm::distance(spawned[i]->position(), spawned[j]->position()), 0.1f);
        }
    }
    (void)player;
}

TEST_F(C09Test, encounter_with_fewer_creatures_than_points_skips_to_the_second_point) {
    auto player = addPlayer(glm::vec3(5.0f, 5.0f, 0.0f));
    auto encounter = addEncounter({
        Gff::Field::newByte("Active", 1),
        Gff::Field::newByte("PlayerOnly", 1),
        Gff::Field::newInt("MaxCreatures", 1),
        Gff::Field::newInt("RecCreatures", 1),
        Gff::Field::newList("CreatureList", creatureList({{"g", 1.0f, false}})),
        Gff::Field::newList("SpawnPointList", spawnPoints({glm::vec3(30.0f, 5.0f, 0.0f), glm::vec3(-30.0f, 5.0f, 0.0f)}))});
    encounter->update(0.0f);
    encounter->update(0.0f);
    auto spawned = spawnedBy(*encounter);
    ASSERT_EQ(1u, spawned.size());
    // K2 quirk: the quota is 1 / 2 = 0, so the first creature moves on.
    EXPECT_FLOAT_EQ(-30.0f, spawned[0]->position().x);
    (void)player;
}

TEST_F(C09Test, encounter_spot_search_stays_within_20_m_of_its_origin) {
    auto encounter = addEncounter({});
    auto creature = addCreature("g", glm::vec3(-50.0f, 0.0f, 0.0f));
    glm::vec3 spot(0.0f);
    // Ground ends at x = 100: from 125 m nothing is in reach; from 110 m the
    // search finds ground under 100 m.
    EXPECT_FALSE(encounter->findSpawnSpot(*creature, glm::vec3(125.0f, 0.0f, 0.0f), 0.5f, spot));
    ASSERT_TRUE(encounter->findSpawnSpot(*creature, glm::vec3(110.0f, 0.0f, 0.0f), 0.5f, spot));
    EXPECT_LE(spot.x, 100.0f);
    EXPECT_GE(spot.x, 90.0f);
}

TEST_F(C09Test, encounter_loads_its_unplaced_creatures_and_reset_time) {
    auto encounter = addEncounter({
        Gff::Field::newDword("LastSpawnDay", 0),
        Gff::Field::newDword("LastSpawnTime", 500),
        Gff::Field::newList("SpawnList", {
            Gff::Builder().field(Gff::Field::newResRef("SpawnResRef", "g1")).field(Gff::Field::newFloat("SpawnCR", 2.0f)).build(),
            Gff::Builder().field(Gff::Field::newResRef("SpawnResRef", "g2")).field(Gff::Field::newFloat("SpawnCR", 3.0f)).build()})});
    EXPECT_EQ((std::vector<std::string> {"g1", "g2"}), encounter->pendingSpawns());
    EXPECT_TRUE(encounter->isResetPending());
}

// --- GetIsInCombat, GetLastAttacker ----------------------------------------------

TEST_F(C09Test, get_is_in_combat_only_count_real_needs_an_opponent) {
    auto creature = addCreature("c", glm::vec3(0.0f));
    EXPECT_EQ(0, call("GetIsInCombat", {Variable::ofObject(creature->id())}).intValue);

    creature->activateCombat();
    EXPECT_EQ(1, call("GetIsInCombat", {Variable::ofObject(creature->id())}).intValue);
    EXPECT_EQ(0, call("GetIsInCombat", {Variable::ofObject(creature->id()), Variable::ofInt(1)}).intValue);

    auto enemy = addCreature("e", glm::vec3(1.0f, 0.0f, 0.0f), Faction::Player);
    creature->setLastHostileActor(enemy->id());
    EXPECT_EQ(1, call("GetIsInCombat", {Variable::ofObject(creature->id()), Variable::ofInt(1)}).intValue);
}

TEST_F(C09Test, get_last_attacker_reads_the_attackee) {
    auto victim = addCreature("victim", glm::vec3(0.0f));
    auto attacker = addCreature("attacker", glm::vec3(1.0f, 0.0f, 0.0f), Faction::Player);
    auto bystander = addCreature("bystander", glm::vec3(2.0f, 0.0f, 0.0f));
    victim->setLastHostileActor(attacker->id());

    // From another object's script, oAttackee names whose attacker to read.
    EXPECT_EQ(attacker->id(), call("GetLastAttacker", {Variable::ofObject(victim->id())}, bystander).objectId);
    EXPECT_EQ(kObjectInvalid, call("GetLastAttacker", {Variable::ofObject(kObjectSelf)}, bystander).objectId);
    // Outside an attack event, OBJECT_SELF falls back to the caller's record.
    EXPECT_EQ(attacker->id(), call("GetLastAttacker", {Variable::ofObject(kObjectSelf)}, victim).objectId);
}

TEST_F(C09Test, get_last_attacker_prefers_the_event_attacker_for_the_caller) {
    auto victim = addCreature("victim", glm::vec3(0.0f));
    auto older = addCreature("older", glm::vec3(1.0f, 0.0f, 0.0f), Faction::Player);
    auto eventAttacker = addCreature("event", glm::vec3(2.0f, 0.0f, 0.0f), Faction::Player);
    victim->setLastHostileActor(older->id());

    auto result = call("GetLastAttacker", {Variable::ofObject(kObjectSelf)}, victim,
                       {Argument(ArgKind::LastAttacker, Variable::ofObject(eventAttacker->id()))});
    EXPECT_EQ(eventAttacker->id(), result.objectId);
}

// --- Talent actions -----------------------------------------------------------------

TEST_F(C09Test, clearing_a_talent_cancels_the_action_it_dispatched) {
    auto jedi = addCreature("jedi", glm::vec3(0.0f));
    auto target = addCreature("target", glm::vec3(1.0f, 0.0f, 0.0f), Faction::Player);
    auto talent = std::make_shared<Talent>(TalentType::Feat, 91);
    auto action = _game->newAction<UseTalentOnObjectAction>(talent, target);
    ASSERT_TRUE(action->subAction());
    jedi->addAction(action);

    jedi->clearAllActions();

    EXPECT_TRUE(action->subAction()->isCancelled());
    EXPECT_TRUE(jedi->actions().empty());
}

} // namespace
