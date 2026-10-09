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

// Simulation benchmarks. A co-op host runs the whole simulation plus
// networking at 30 Hz, so a tick has ~33 ms to spend. Each benchmark builds a
// synthetic scene (no game data), runs it, and reports the time per run and
// per tick together with a digest of the final state: an optimisation must
// leave the digests unchanged.
//
//   bench_sim [filter] [--reps N] [--json FILE] [--list]
//
// tools/cloud/bench.sh builds and runs it.

#include <gmock/gmock.h>

#include <boost/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../test/fixtures/engine.h"
#include "../test/fixtures/game.h"
#include "../test/fixtures/scene.h"
#include "../test/fixtures/script.h"

#include "reone/game/action/attackobject.h"
#include "reone/game/action/movetopoint.h"
#include "reone/game/combat.h"
#include "reone/game/coopsession.h"
#include "reone/game/effect/damage.h"
#include "reone/game/effect/heal.h"
#include "reone/game/effect/regenerate.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/pathfinder.h"
#include "reone/graphics/walkmesh.h"
#include "reone/net/protocol.h"
#include "reone/net/transport.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/scene/collision.h"
#include "reone/script/executioncontext.h"
#include "reone/script/program.h"
#include "reone/script/virtualmachine.h"
#include "reone/system/randomutil.h"

using namespace reone;
using namespace reone::game;
using namespace testing;

namespace json = boost::json;

// The test fixtures declare these friends of Game and Module; the unit tests
// define them in several files, so the benchmark defines the three it needs.
void reone::game::TestGameModule::advanceTick(Game &game) {
    ++game._tickCount;
}

void reone::game::TestGameModule::setActiveModuleArea(Game &game, std::shared_ptr<Area> area) {
    game._module = game.newModule();
    game._module->_area = std::move(area);
}

void reone::game::TestGameModule::setCurrentScreen(Game &game, int screen) {
    game.changeScreen(static_cast<Game::Screen>(screen));
}

namespace {

constexpr float kTick = 1.0f / 30.0f;
constexpr int kPistol = 12;
constexpr uint32_t kSeed = 20260702;

uint64_t fnv1a(const std::string &text, uint64_t hash = 14695981039346656037ull) {
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hex(uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
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

/** A small world the scenes populate, and the fixed-step loop that runs it. */
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
        EXPECT_CALL(engine.resourceModule().scripts(), get(_))
            .Times(AnyNumber())
            .WillRepeatedly(Return(std::shared_ptr<script::ScriptProgram>()));
        ON_CALL(engine.resourceModule().twoDas(), get("creaturespeed")).WillByDefault(Return(creatureSpeeds()));
        ON_CALL(engine.resourceModule().twoDas(), get("baseitems")).WillByDefault(Return(pistolBaseItems()));
        game = std::make_unique<Game>(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
        game->initLocalServices();
        area = game->newArea();
        TestGameModule::setActiveModuleArea(*game, area);
        TestGameModule::setCurrentScreen(*game, static_cast<int>(Game::Screen::InGame));
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

    uint64_t digest() const {
        json::value state = json::parse(game->dumpStateJson(1.0e9f));
        state.as_object().erase("frame");
        state.as_object()["rng"].as_object().erase("seed");
        return fnv1a(json::serialize(state));
    }
};

struct Result {
    uint64_t digest {0};
    int ticks {0}; // units of work the run did (ticks, iterations)
    std::string note; // what the run did, to show the scene is doing real work
};

struct Bench {
    std::string name;
    std::string unit; // what "ticks" counts
    std::function<Result()> run;
};

// Scenes. Each builds its world inside the timed run: setup is part of the cost
// a mission load pays, and is small next to the run.

Result pathingCrowd(int creatures, int ticks) {
    setRandomSeed(kSeed);
    World world;
    for (int i = 0; i < creatures; ++i) {
        float y = -5.0f + 10.0f * static_cast<float>(i) / static_cast<float>(creatures);
        bool left = i % 2 == 0;
        auto creature = world.spawn(glm::vec3(left ? -25.0f : 25.0f, y, 0.0f));
        creature->addAction(world.game->newAction<MoveToPointAction>(glm::vec3(left ? 25.0f : -25.0f, -y * 0.5f, 0.0f)));
    }
    for (int tick = 0; tick < ticks; ++tick) {
        world.step();
    }
    int moved = 0;
    for (auto &creature : world.creatures) {
        moved += std::abs(creature->position().x) < 24.0f ? 1 : 0;
    }
    return {world.digest(), ticks, std::to_string(moved) + " of " + std::to_string(creatures) + " creatures left their start"};
}

Result combatPairs(int pairs, int ticks) {
    setRandomSeed(kSeed);
    setCosmeticRandomSeed(kSeed ^ 0x5a5a5a5au);
    World world;
    for (int pair = 0; pair < pairs; ++pair) {
        float y = -5.0f + 10.0f * static_cast<float>(pair) / static_cast<float>(pairs);
        auto a = world.spawn(glm::vec3(-6.0f, y, 0.0f), Faction::Friendly1, 4000);
        auto b = world.spawn(glm::vec3(6.0f, y, 0.0f), Faction::Hostile1, 4000);
        world.arm(a);
        world.arm(b);
        a->addAction(world.game->newAction<AttackObjectAction>(b));
        b->addAction(world.game->newAction<AttackObjectAction>(a));
    }
    for (int tick = 0; tick < ticks; ++tick) {
        world.step();
    }
    uint64_t draws = gameplayRandom().draws();
    return {fnv1a(std::to_string(draws), world.digest()), ticks, std::to_string(draws) + " gameplay rolls"};
}

Result effectsLoad(int creatures, int ticks) {
    setRandomSeed(kSeed);
    World world;
    for (int i = 0; i < creatures; ++i) {
        auto creature = world.spawn(glm::vec3(-28.0f + 0.25f * static_cast<float>(i % 224), -5.0f + static_cast<float>(i / 224), 0.0f),
                                    Faction::Friendly1, 400 + i);
        creature->setCurrentHitPoints(100 + i % 7);
        creature->applyEffect(world.game->newEffect<RegenerateEffect>(1 + i % 4, 0.5f + 0.25f * static_cast<float>(i % 3)), DurationType::Temporary, 20.0f);
        creature->applyEffect(world.game->newEffect<RegenerateEffect>(1, 1.0f), DurationType::Permanent);
        creature->applyEffect(world.game->newEffect<HealEffect>(i % 5), DurationType::Instant);
    }
    for (int tick = 0; tick < ticks; ++tick) {
        world.step();
    }
    return {world.digest(), ticks};
}

// i = 0; while (i < iterations) i = i + 1; -- about nine instructions an iteration.
Result scriptLoop(int iterations, int runs) {
    uint64_t checksum = 0;
    for (int run = 0; run < runs; ++run) {
        auto program = std::make_shared<script::ScriptProgram>("bench_loop");
        using script::Instruction;
        using script::InstructionType;
        program->add(Instruction::newCONSTI(0));
        program->add(Instruction::newCPTOPSP(-4, 4));
        program->add(Instruction::newCONSTI(iterations));
        program->add(Instruction(InstructionType::LTII));
        program->add(Instruction::newJZ(42));
        program->add(Instruction::newCPTOPSP(-4, 4));
        program->add(Instruction::newCONSTI(1));
        program->add(Instruction(InstructionType::ADDII));
        program->add(Instruction::newCPDOWNSP(-8, 4));
        program->add(Instruction::newMOVSP(-4));
        program->add(Instruction::newJMP(-52));
        program->add(Instruction::newMOVSP(-4));
        program->add(Instruction(InstructionType::RETN));
        script::VirtualMachine machine(program, std::make_unique<script::ExecutionContext>());
        checksum = checksum * 31 + static_cast<uint64_t>(machine.run() + 7);
    }
    return {checksum, runs * iterations};
}

// Many tiny scripts: what heartbeat and event handlers cost to start.
Result scriptStartup(int runs) {
    uint64_t checksum = 0;
    for (int run = 0; run < runs; ++run) {
        auto program = std::make_shared<script::ScriptProgram>("bench_tiny");
        program->add(script::Instruction::newCONSTI(run));
        program->add(script::Instruction::newCONSTI(3));
        program->add(script::Instruction(script::InstructionType::ADDII));
        script::VirtualMachine machine(program, std::make_unique<script::ExecutionContext>());
        checksum = checksum * 31 + static_cast<uint64_t>(machine.run() + machine.getStackVariable(0).intValue);
    }
    return {checksum, runs};
}

net::Snapshot syntheticSnapshot(int entities, bool full) {
    net::Snapshot snapshot;
    snapshot.tick = 1000;
    snapshot.generation = 3;
    snapshot.baseTick = full ? 0 : 990;
    snapshot.worldFields = full ? net::kWorldAllMask : 0;
    for (int i = 0; i < entities; ++i) {
        net::EntityRecord record;
        record.fields = full ? (net::kFieldAllMask & ~net::kFieldRemoved) : (net::kFieldPosition | net::kFieldVitals);
        auto &state = record.state;
        state.id = 100 + static_cast<uint32_t>(i);
        state.type = 1;
        state.key = "5:creature_" + std::to_string(i) + ":tag_" + std::to_string(i) + ":123:456";
        state.tag = "tag_" + std::to_string(i);
        state.resref = "n_creature" + std::to_string(i % 40);
        state.position = {static_cast<float>(i) * 0.5f, static_cast<float>(i % 17), 0.0f};
        state.facing = static_cast<float>(i) * 0.1f;
        state.anim = "walk";
        state.animTime = 0.25f * static_cast<float>(i % 8);
        state.hp = static_cast<int16_t>(40 + i % 20);
        state.maxHp = 60;
        state.rightWeapon = "g_w_blstrpstl01";
        state.armor = "g_a_clothes01";
        snapshot.entities.push_back(std::move(record));
    }
    return snapshot;
}

Result snapshotCodec(int entities, bool full, int rounds) {
    auto snapshot = syntheticSnapshot(entities, full);
    uint64_t checksum = 0;
    for (int round = 0; round < rounds; ++round) {
        snapshot.tick = static_cast<uint32_t>(1000 + round);
        auto bytes = net::encode(snapshot);
        net::Snapshot decoded;
        if (!net::decode(bytes, decoded)) {
            std::cerr << "snapshot did not decode\n";
            std::exit(2);
        }
        checksum = checksum * 31 + bytes.size() + decoded.entities.size() + decoded.tick;
        checksum ^= fnv1a(std::string(bytes.begin(), bytes.begin() + std::min<size_t>(bytes.size(), 64)));
    }
    return {checksum, rounds};
}

/** A host session with one connected raw client, as the integration tests drive it. */
Result coopHostTicks(int creatures, int ticks, bool moving) {
    setRandomSeed(kSeed);
    World world;
    auto player = world.spawn(glm::vec3(0.0f), Faction::Player);
    world.game->party().addMember(kNpcPlayer, player);
    world.game->party().setPlayer(player);
    for (int i = 0; i < creatures; ++i) {
        float y = -5.0f + 10.0f * static_cast<float>(i) / static_cast<float>(creatures);
        auto creature = world.spawn(glm::vec3(-25.0f + static_cast<float>(i % 50), y, 0.0f), Faction::Hostile1);
        if (moving) {
            creature->addAction(world.game->newAction<MoveToPointAction>(glm::vec3(25.0f, -y * 0.5f, 0.0f)));
        }
    }

    auto [hostEnd, clientEnd] = net::createLoopbackPair();
    CoopConfig config;
    config.role = CoopRole::Host;
    config.build = "build-A";
    config.k1 = {true, 1, 2, 3};
    auto session = std::make_shared<CoopSession>(*world.game, config, std::move(hostEnd));
    world.game->setCoopSession(session);
    session->pollNetwork();

    net::ConnectionId connection = 0;
    uint64_t bytesReceived = 0;
    uint64_t snapshots = 0;
    auto poll = [&]() {
        std::vector<net::TransportEvent> events;
        clientEnd->poll(events);
        for (auto &event : events) {
            if (event.type == net::TransportEventType::Connected) {
                connection = event.connection;
            } else if (event.type == net::TransportEventType::Message && !event.data.empty() &&
                       event.data[0] == static_cast<uint8_t>(net::MessageType::Snapshot)) {
                net::Snapshot snapshot;
                if (net::decode(event.data, snapshot)) {
                    clientEnd->send(connection, net::encode(net::SnapshotAck {snapshot.generation, snapshot.tick}), net::SendMode::Unreliable);
                    bytesReceived += event.data.size();
                    ++snapshots;
                }
            }
        }
    };
    poll();
    net::Hello hello;
    hello.build = "build-A";
    hello.mode = "story";
    hello.playerName = "Guest";
    hello.k1 = {true, 1, 2, 3};
    clientEnd->send(connection, net::encode(hello), net::SendMode::Reliable);
    session->pollNetwork();
    poll();

    for (int tick = 0; tick < ticks; ++tick) {
        session->pollNetwork();
        world.step();
        session->beforeTick();
        session->afterTick();
        poll();
    }
    world.game->setCoopSession(nullptr);
    return {fnv1a(std::to_string(bytesReceived) + ":" + std::to_string(snapshots), world.digest()), ticks,
            std::to_string(snapshots) + " snapshots, " + std::to_string(bytesReceived) + " bytes to the client"};
}

std::vector<Bench> benches() {
    return {
        {"path_crowd_64", "tick", [] { return pathingCrowd(64, 300); }},
        {"combat_pairs_8", "tick", [] { return combatPairs(8, 300); }},
        {"effects_400", "tick", [] { return effectsLoad(400, 300); }},
        {"script_loop_20k", "iteration", [] { return scriptLoop(20000, 5); }},
        {"script_startup_5k", "script", [] { return scriptStartup(5000); }},
        {"snapshot_full_300", "snapshot", [] { return snapshotCodec(300, true, 200); }},
        {"snapshot_delta_300", "snapshot", [] { return snapshotCodec(300, false, 1000); }},
        {"coop_host_200_idle", "tick", [] { return coopHostTicks(200, 300, false); }},
        {"coop_host_200_moving", "tick", [] { return coopHostTicks(200, 300, true); }},
    };
}

} // namespace

int main(int argc, char **argv) {
    testing::InitGoogleMock(&argc, argv);
    std::string filter;
    std::string jsonPath;
    int reps = 5;
    bool listOnly = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--reps" && i + 1 < argc) {
            reps = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--json" && i + 1 < argc) {
            jsonPath = argv[++i];
        } else if (arg == "--list") {
            listOnly = true;
        } else if (arg.rfind("--", 0) != 0) {
            filter = arg;
        }
    }

    // The mocked services say "uninteresting call" on every use; that is noise here.
    testing::FLAGS_gmock_verbose = "error";

    json::array report;
    std::printf("%-24s %10s %10s %12s  %s\n", "benchmark", "min ms", "median ms", "us/unit", "digest, notes");
    for (const auto &bench : benches()) {
        if (!filter.empty() && bench.name.find(filter) == std::string::npos) {
            continue;
        }
        if (listOnly) {
            std::printf("%s\n", bench.name.c_str());
            continue;
        }
        std::vector<double> times;
        Result result;
        bool stable = true;
        for (int rep = -1; rep < reps; ++rep) { // one untimed warm-up
            auto start = std::chrono::steady_clock::now();
            Result current = bench.run();
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (rep >= 0) {
                times.push_back(ms);
            }
            if (rep > -1 && current.digest != result.digest) {
                stable = false;
            }
            result = current;
            if (rep == -1) {
                result.digest = current.digest;
            }
        }
        std::sort(times.begin(), times.end());
        double median = times[times.size() / 2];
        double perUnit = times.front() * 1000.0 / std::max(1, result.ticks);
        std::printf("%-24s %10.2f %10.2f %12.2f  %s%s%s%s\n", bench.name.c_str(), times.front(), median, perUnit,
                    hex(result.digest).c_str(), stable ? "" : "  UNSTABLE", result.note.empty() ? "" : "  ", result.note.c_str());
        std::fflush(stdout);
        json::object entry;
        entry["name"] = bench.name;
        entry["unit"] = bench.unit;
        entry["units"] = result.ticks;
        entry["min_ms"] = times.front();
        entry["median_ms"] = median;
        entry["us_per_unit"] = perUnit;
        entry["digest"] = hex(result.digest);
        report.push_back(std::move(entry));
    }
    if (!jsonPath.empty()) {
        std::ofstream(jsonPath) << json::serialize(report) << "\n";
    }
    return 0;
}
