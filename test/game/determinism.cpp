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

// Determinism of the fixed-step simulation. Co-op relies on the host's
// simulation being a pure function of its inputs: the same world, orders and
// gameplay seed must give the same state, tick by tick, in every process and on
// every platform. Each scenario here builds a small world from synthetic data,
// runs it for a number of 30 Hz ticks and records a hash of the whole state
// after each tick.
//
//  - The same scenario twice in one process must agree (state that leaks between
//    runs, uninitialised members and iteration orders that follow addresses show
//    up here).
//  - The recorded hashes in test/data/determinism_golden.txt must still match
//    (drift between builds, compilers and platforms shows up here).
//
// Environment:
//   KOTOR_DETERMINISM_DUMP=<file>    write every scenario's per-tick hashes to <file>
//   KOTOR_DETERMINISM_UPDATE=1       rewrite the golden file from this run
// tools/cloud/determinism.sh wraps both and compares two dumps.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/json.hpp>

#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/action/attackobject.h"
#include "reone/game/action/movetopoint.h"
#include "reone/game/combat.h"
#include "reone/game/companionbrain.h"
#include "reone/game/effect/damage.h"
#include "reone/game/effect/heal.h"
#include "reone/game/effect/regenerate.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/game/pathfinder.h"
#include "reone/graphics/walkmesh.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/scene/collision.h"
#include "reone/system/randomutil.h"

using namespace reone;
using namespace reone::game;
using namespace testing;

namespace json = boost::json;

namespace {

constexpr float kTick = 1.0f / 30.0f;
constexpr int kPistol = 12;

uint64_t fnv1a(const std::string &text, uint64_t hash = 14695981039346656037ull) {
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

// A wide corridor, x in [-30, 30] and y in [-6, 6], as one room.
void loadArena(Pathfinder &pf) {
    graphics::Walkmesh walkmesh;
    walkmesh.vertices = {
        glm::vec3(-30.0f, -6.0f, 0.0f),
        glm::vec3(30.0f, -6.0f, 0.0f),
        glm::vec3(30.0f, 6.0f, 0.0f),
        glm::vec3(-30.0f, 6.0f, 0.0f)};
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

std::shared_ptr<resource::TwoDA> pistolBaseItems() {
    resource::TwoDA::Builder builder;
    builder.columns({"maxattackrange", "crithitmult", "critthreat", "damageflags", "dietoroll",
                     "equipableslots", "itemclass", "numdice", "weapontype", "weaponwield",
                     "ammunitiontype", "bodyvar"});
    for (int i = 0; i <= kPistol; ++i) {
        if (i == kPistol) {
            builder.row({"23", "2", "2", "2", "6", "48", "w_blstrpstl", "1", "4", "4", "-1", ""});
        } else {
            builder.row({"", "", "", "", "", "", "", "", "", "", "", ""});
        }
    }
    return std::shared_ptr<resource::TwoDA>(builder.build());
}

/** A small world the scenarios populate, and the fixed-step loop that runs it. */
struct World {
    TestEngine engine;
    NiceMock<scene::MockSceneGraph> sceneGraph;
    StubConsole console;
    std::unique_ptr<Game> game;
    std::shared_ptr<Area> area;
    std::vector<std::shared_ptr<Creature>> creatures;

    World() {
        engine.init();
        ON_CALL(engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(sceneGraph));
        ON_CALL(sceneGraph, testElevation(_, _))
            .WillByDefault(Invoke([](const glm::vec3 &position, scene::Collision &collision) {
                if (glm::abs(position.x) > 30.0f || glm::abs(position.y) > 6.0f) {
                    return false;
                }
                collision.intersection = position;
                collision.intersection.z = 0.0f;
                collision.material = 0;
                collision.user = nullptr;
                return true;
            }));
        ON_CALL(engine.resourceModule().twoDas(), get("creaturespeed")).WillByDefault(Return(creatureSpeeds()));
        ON_CALL(engine.resourceModule().twoDas(), get("baseitems")).WillByDefault(Return(pistolBaseItems()));
        game = std::make_unique<Game>(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
        area = game->newArea();
        TestGameModule::setActiveModuleArea(*game, area);
        loadArena(area->pathfinder());
    }

    std::shared_ptr<Creature> spawn(const glm::vec3 &position, Faction faction = Faction::Friendly1, int hitPoints = 40) {
        auto creature = game->newCreature();
        creature->setWalkRate(0);
        creature->setPosition(position);
        creature->setFaction(faction);
        creature->setMaxHitPoints(hitPoints);
        creature->setCurrentHitPoints(hitPoints);
        area->add(creature);
        creatures.push_back(creature);
        return creature;
    }

    void arm(const std::shared_ptr<Creature> &creature) {
        auto gff = resource::Gff::Builder()
                       .field(resource::Gff::Field::newCExoString("Tag", "pistol"))
                       .field(resource::Gff::Field::newInt("BaseItem", kPistol))
                       .field(resource::Gff::Field::newWord("StackSize", 1))
                       .build();
        auto pistol = game->newItem();
        pistol->deserialize(*gff, SerializedIdentityContext::templateResource());
        creature->equip(InventorySlots::rightWeapon, pistol);
    }

    /** One fixed tick, in the order the engine runs it: objects, then combat, then the counter. */
    void step() {
        auto objects = area->objects();
        for (auto &object : objects) {
            object->update(kTick);
        }
        game->combat().update(kTick);
        TestGameModule::advanceTick(*game);
    }

    /**
     * Everything the simulation decided so far, as one number: the state dump
     * (positions, facings, hit points, actions, factions, ...) without its render
     * frame counter, and the gameplay random stream's position and next value.
     */
    uint64_t digest() const {
        json::value state = json::parse(game->dumpStateJson(1.0e9f));
        state.as_object().erase("frame");
        state.as_object()["rng"].as_object().erase("seed"); // an input, not state
        uint64_t hash = fnv1a(json::serialize(state));
        RandomGenerator probe = gameplayRandom(); // a copy: looking does not draw
        // An untouched stream has nothing to report beyond its seed, which is
        // an input, not state.
        uint64_t draws = gameplayRandom().draws();
        hash = fnv1a(std::to_string(draws) + ":" + (draws ? std::to_string(probe.next()) : ""), hash);
        return hash;
    }
};

using Scenario = std::function<void(World &)>;

struct Trace {
    std::vector<uint64_t> hashes; // after tick 1, 2, ...
    uint64_t draws {0};           // gameplay random draws over the whole run
};

Trace run(const Scenario &scenario, int ticks, uint32_t seed) {
    setRandomSeed(seed);
    setCosmeticRandomSeed(seed ^ 0x5a5a5a5au);
    World world;
    scenario(world);
    Trace trace;
    for (int tick = 0; tick < ticks; ++tick) {
        world.step();
        trace.hashes.push_back(world.digest());
    }
    trace.draws = gameplayRandom().draws();
    return trace;
}

// Scenarios

// Six creatures cross the arena in both directions at once: pathing, steering,
// creature avoidance and the unstuck rule, with no randomness expected.
void crowd(World &world) {
    for (int i = 0; i < 6; ++i) {
        float y = -4.0f + 1.6f * static_cast<float>(i);
        bool left = i % 2 == 0;
        auto creature = world.spawn(glm::vec3(left ? -25.0f : 25.0f, y, 0.0f));
        creature->addAction(world.game->newAction<MoveToPointAction>(glm::vec3(left ? 25.0f : -25.0f, -y * 0.5f, 0.0f)));
    }
}

// Two armed pairs fight: attack rolls, damage, combat rounds, hostility and the
// actions the creatures queue as they go.
void duels(World &world) {
    for (int pair = 0; pair < 2; ++pair) {
        float y = pair == 0 ? -3.0f : 3.0f;
        auto a = world.spawn(glm::vec3(-6.0f, y, 0.0f), Faction::Friendly1, 60);
        auto b = world.spawn(glm::vec3(6.0f, y, 0.0f), Faction::Hostile1, 60);
        world.arm(a);
        world.arm(b);
        a->addAction(world.game->newAction<AttackObjectAction>(b));
        b->addAction(world.game->newAction<AttackObjectAction>(a));
    }
}

// Timed effects: regeneration and instant damage and healing on a group, with
// the effect timers running on the simulation clock.
void effects(World &world) {
    for (int i = 0; i < 5; ++i) {
        auto creature = world.spawn(glm::vec3(static_cast<float>(i) * 2.0f - 4.0f, 0.0f, 0.0f), Faction::Friendly1, 30 + 5 * i);
        creature->setCurrentHitPoints(10 + i);
        creature->applyEffect(world.game->newEffect<RegenerateEffect>(1 + i, 0.5f + 0.25f * static_cast<float>(i)), DurationType::Temporary, 8.0f);
        creature->applyEffect(world.game->newEffect<DamageEffect>(3 + i, DamageType::Slashing, DamagePower::Normal), DurationType::Instant);
        creature->applyEffect(world.game->newEffect<HealEffect>(2 * i), DurationType::Instant);
    }
}

// Dice: the gameplay stream feeds rolls made by sim code, interleaved with
// movement that must not disturb it.
void dice(World &world) {
    for (int i = 0; i < 4; ++i) {
        auto creature = world.spawn(glm::vec3(-20.0f + 3.0f * static_cast<float>(i), 0.0f, 0.0f), Faction::Friendly1, 100);
        creature->addAction(world.game->newAction<MoveToPointAction>(glm::vec3(20.0f, 3.0f - static_cast<float>(i), 0.0f)));
    }
    // Rolls like the ones attack and save checks make.
    for (int i = 0; i < 20; ++i) {
        world.creatures[static_cast<size_t>(i) % world.creatures.size()]->damage(randomInt(0, 3), nullptr);
    }
}

struct NamedScenario {
    const char *name;
    Scenario scenario;
    int ticks;
};

const std::vector<NamedScenario> &scenarios() {
    static const std::vector<NamedScenario> all {
        {"crowd", crowd, 600},
        {"duels", duels, 450},
        {"effects", effects, 300},
        {"dice", dice, 300},
    };
    return all;
}

constexpr uint32_t kSeed = 20260702;

// Golden hashes: one line per scenario and checkpoint, "scenario tick hash".
std::filesystem::path goldenPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "data" / "determinism_golden.txt";
}

std::string hex(uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

constexpr int kCheckpointEvery = 30;

} // namespace

TEST(Determinism, the_same_scenario_twice_gives_the_same_state_every_tick) {
    for (const auto &entry : scenarios()) {
        Trace first = run(entry.scenario, entry.ticks, kSeed);
        Trace second = run(entry.scenario, entry.ticks, kSeed);
        ASSERT_EQ(first.hashes.size(), second.hashes.size()) << entry.name;
        for (size_t tick = 0; tick < first.hashes.size(); ++tick) {
            ASSERT_EQ(first.hashes[tick], second.hashes[tick]) << entry.name << " diverged at tick " << tick + 1;
        }
        EXPECT_EQ(first.draws, second.draws) << entry.name;
    }
}

TEST(Determinism, scenarios_that_roll_dice_follow_the_seed_and_the_rest_do_not) {
    for (const auto &entry : scenarios()) {
        Trace a = run(entry.scenario, entry.ticks, kSeed);
        Trace b = run(entry.scenario, entry.ticks, kSeed + 1);
        if (a.draws == 0 && b.draws == 0) {
            EXPECT_EQ(a.hashes, b.hashes) << entry.name << " draws no gameplay randomness, so the seed must not matter";
        } else {
            EXPECT_NE(a.hashes.back(), b.hashes.back()) << entry.name << " rolls dice, so a different seed must change the outcome";
        }
    }
}

TEST(Determinism, scenarios_actually_change_the_world) {
    // A hash that never moves would pass every comparison above.
    for (const auto &entry : scenarios()) {
        Trace trace = run(entry.scenario, entry.ticks, kSeed);
        std::set<uint64_t> distinct(trace.hashes.begin(), trace.hashes.end());
        EXPECT_GT(distinct.size(), trace.hashes.size() / 4) << entry.name;
    }
}

// std::sort is free to order equal keys however it likes, and libstdc++, libc++
// and MSVC's STL do not agree once a range outgrows the insertion-sort cutoff.
TEST(Determinism, equidistant_creatures_are_found_in_id_order) {
    World world;
    auto centre = world.spawn(glm::vec3(0.0f));
    std::vector<uint32_t> ids;
    for (int i = 0; i < 48; ++i) {
        ids.push_back(world.spawn(glm::vec3(10.0f, 0.0f, 0.0f))->id());
    }
    ASSERT_TRUE(std::is_sorted(ids.begin(), ids.end()));
    for (int nth = 0; nth < 48; ++nth) {
        auto found = world.area->getNearestCreature(centre, {}, nth + 1);
        ASSERT_TRUE(found) << nth;
        EXPECT_EQ(ids[nth], found->id()) << "nth " << nth;
    }
}

TEST(Determinism, per_tick_hashes_can_be_dumped_for_a_cross_platform_comparison) {
    const char *path = std::getenv("KOTOR_DETERMINISM_DUMP");
    if (!path) {
        GTEST_SKIP() << "set KOTOR_DETERMINISM_DUMP=<file> to write every scenario's per-tick hashes";
    }
    std::ofstream out(path);
    ASSERT_TRUE(out.good()) << path;
    for (const auto &entry : scenarios()) {
        Trace trace = run(entry.scenario, entry.ticks, kSeed);
        for (size_t tick = 0; tick < trace.hashes.size(); ++tick) {
            out << entry.name << ' ' << tick + 1 << ' ' << hex(trace.hashes[tick]) << '\n';
        }
    }
}

TEST(DeterminismGolden, hashes_match_the_ones_recorded_on_linux) {
    std::map<std::string, std::string> recorded; // "scenario tick" -> hash
    std::ostringstream fresh;
    for (const auto &entry : scenarios()) {
        Trace trace = run(entry.scenario, entry.ticks, kSeed);
        for (size_t tick = kCheckpointEvery; tick <= trace.hashes.size(); tick += kCheckpointEvery) {
            fresh << entry.name << ' ' << tick << ' ' << hex(trace.hashes[tick - 1]) << '\n';
        }
    }

    if (std::getenv("KOTOR_DETERMINISM_UPDATE")) {
        std::filesystem::create_directories(goldenPath().parent_path());
        std::ofstream(goldenPath()) << "# scenario tick hash, from test/game/determinism.cpp; regenerate with KOTOR_DETERMINISM_UPDATE=1\n"
                                    << fresh.str();
        GTEST_SKIP() << "wrote " << goldenPath();
    }

    std::ifstream in(goldenPath());
    ASSERT_TRUE(in.good()) << "no golden file at " << goldenPath() << "; run with KOTOR_DETERMINISM_UPDATE=1 to create it";
    std::string line;
    std::vector<std::string> expected;
    while (std::getline(in, line)) {
        if (!line.empty() && line[0] != '#') {
            expected.push_back(line);
        }
    }
    std::vector<std::string> actual;
    std::istringstream freshIn(fresh.str());
    while (std::getline(freshIn, line)) {
        actual.push_back(line);
    }
    ASSERT_EQ(expected.size(), actual.size()) << "scenarios or tick counts changed: regenerate the golden file";
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(expected[i], actual[i])
            << "the simulation drifted from the recorded run (first difference shown above): if this is a deliberate "
               "change, regenerate with KOTOR_DETERMINISM_UPDATE=1; if it is a different compiler or platform, that is the "
               "nondeterminism this test exists to catch";
        if (expected[i] != actual[i]) {
            break;
        }
    }
}
