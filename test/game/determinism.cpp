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
#include <glm/gtc/quaternion.hpp>

#include <cstdlib>
#include <cstring>
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
#include "reone/game/location.h"
#include "reone/game/minigame.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/game/turret.h"
#include "reone/game/pathfinder.h"
#include "reone/graphics/walkmesh.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/scene/collision.h"
#include "reone/system/portablemath.h"
#include "reone/system/randomutil.h"

using namespace reone;
using namespace reone::game;
using namespace reone::game::companion;
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

// Allocations of mixed sizes, half of them freed again: whatever is built next
// lands at other addresses and in another order in memory, so state that follows
// pointer values (ordered pointer keys, address tie-breaks) shows up.
struct Shuffler {
    std::vector<std::unique_ptr<char[]>> blocks;

    explicit Shuffler(size_t amount) {
        for (size_t i = 0; i < amount * 61; ++i) {
            blocks.push_back(std::make_unique<char[]>(16 + (i * 7919) % 900));
        }
        for (size_t i = 0; i < blocks.size(); i += 2) {
            blocks[i].reset();
        }
    }
};

Trace run(const Scenario &scenario, int ticks, uint32_t seed, size_t shuffle = 0) {
    Shuffler shuffler(shuffle);
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


// A brain-driven fight, as a model: the companion brain decides each round from a
// snapshot, the decision is applied to the snapshot (consumables are used up, Force
// points spent, enemies hurt and replaced), and the gameplay stream supplies both
// the brain's tie-breaks and the enemy's damage.
uint64_t bits(float value) {
    uint32_t raw;
    std::memcpy(&raw, &value, sizeof raw);
    return raw;
}

std::string describe(const Actor &actor) {
    std::ostringstream out;
    out << actor.id << ':' << actor.hitPoints << ':' << bits(actor.position.x) << ':' << bits(actor.position.y) << ':' << actor.target << ';';
    for (const auto &family : actor.activeFamilies) {
        out << family << ',';
    }
    return out.str();
}

companion::Talent makeTalent(TalentRole role, int id, int cost = 0) {
    companion::Talent talent;
    talent.role = role;
    talent.id = id;
    talent.forceCost = cost;
    talent.rank = 1;
    return talent;
}

Actor makeActor(uint32_t id, float x, float y, int hp) {
    Actor actor;
    actor.id = id;
    actor.position = glm::vec3(x, y, 0.0f);
    actor.hitPoints = hp;
    actor.maxHitPoints = hp;
    return actor;
}

void spawnWave(CompanionView &view, int wave) {
    view.enemies.clear();
    for (int i = 0; i < 3; ++i) {
        Actor enemy = makeActor(static_cast<uint32_t>(100 + wave * 10 + i), 3.0f + 1.5f * static_cast<float>(i), -2.0f + 2.0f * static_cast<float>(i), 40 + 7 * i);
        enemy.caster = i == 1;
        enemy.boss = i == 2 && wave % 2 == 1;
        enemy.level = 4 + wave;
        view.enemies.push_back(enemy);
    }
}

Trace brainFight(uint32_t seed, size_t shuffle) {
    Shuffler shuffler(shuffle);
    setRandomSeed(seed);
    setCosmeticRandomSeed(seed ^ 0x5a5a5a5au);

    CompanionView view;
    view.self = makeActor(1, 0.0f, 0.0f, 90);
    view.forcePoints = 100;
    view.attackRange = 2.0f;
    view.leaderPosition = glm::vec3(0.0f, -2.0f, 0.0f);
    view.allies.push_back(makeActor(2, 0.0f, -2.0f, 80));
    view.allies.push_back(makeActor(3, 1.0f, -3.0f, 60));

    companion::Talent heal = makeTalent(TalentRole::Heal, 28, 25);
    heal.onCaster = true;
    heal.radius = kAreaHealRadius;
    companion::Talent medpac = makeTalent(TalentRole::Heal, 64);
    medpac.item = 500;
    medpac.itemKind = ItemKind::Medpac;
    medpac.exclusion = 0x02;
    medpac.count = 3;
    medpac.value = 30;
    companion::Talent grenade = makeTalent(TalentRole::Grenade, 87);
    grenade.item = 600;
    grenade.itemKind = ItemKind::Grenade;
    grenade.grenade = GrenadeKind::Frag;
    grenade.radius = kGrenadeRadius;
    grenade.effects = kEffectDamage;
    grenade.count = 2;
    companion::Talent stim = makeTalent(TalentRole::Buff, 70);
    stim.item = 700;
    stim.itemKind = ItemKind::Stim;
    stim.selfOnly = true;
    stim.family = "stim:a";
    stim.count = 2;
    companion::Talent stun = makeTalent(TalentRole::AttackSingle, 31, 20);
    stun.effects = kEffectDisable | kEffectDamage;
    view.talents = {heal, medpac, grenade, stim, stun};

    auto find = [](std::vector<Actor> &list, uint32_t id) -> Actor * {
        for (auto &actor : list) {
            if (actor.id == id) {
                return &actor;
            }
        }
        return nullptr;
    };

    int wave = 0;
    spawnWave(view, wave);
    Trace trace;
    for (int round = 0; round < 90; ++round) {
        view.mayReposition = true;
        Decision decision = decide(view, [](int min, int max) { return gameplayRandom().uniformInt(min, max); });
        std::ostringstream log;
        log << static_cast<int>(decision.kind) << ' ' << decision.target << ' ' << decision.talent.id << ' ' << decision.talent.item << ' ' << decision.reason;
        if (decision.moveFirst) {
            log << ' ' << bits(decision.moveFirst->x) << ' ' << bits(decision.moveFirst->y);
            view.self.position = *decision.moveFirst;
        }
        Actor *target = find(view.enemies, decision.target);
        if (!target) {
            target = find(view.allies, decision.target);
        }
        if (decision.kind == DecisionKind::Attack && target) {
            target->hitPoints -= 8;
        } else if (decision.kind == DecisionKind::TalentOnObject) {
            view.forcePoints = std::max(0, view.forcePoints - decision.talent.forceCost);
            for (auto &owned : view.talents) {
                if (decision.talent.item != 0 && owned.item == decision.talent.item) {
                    --owned.count;
                }
            }
            switch (decision.talent.role) {
            case TalentRole::Heal:
                if (decision.talent.onCaster) {
                    view.self.hitPoints = std::min(view.self.maxHitPoints, view.self.hitPoints + 20);
                    for (auto &ally : view.allies) {
                        ally.hitPoints = std::min(ally.maxHitPoints, ally.hitPoints + 20);
                    }
                } else if (target) {
                    target->hitPoints = std::min(target->maxHitPoints, target->hitPoints + 30);
                } else {
                    view.self.hitPoints = std::min(view.self.maxHitPoints, view.self.hitPoints + 30);
                }
                break;
            case TalentRole::Grenade:
                if (target) {
                    glm::vec3 centre = target->position;
                    for (auto &enemy : view.enemies) {
                        if (glm::distance(enemy.position, centre) <= kGrenadeRadius) {
                            enemy.hitPoints -= 15;
                        }
                    }
                }
                break;
            case TalentRole::Buff:
                view.self.activeFamilies.push_back(decision.talent.family);
                std::sort(view.self.activeFamilies.begin(), view.self.activeFamilies.end());
                view.self.activeFamilies.erase(std::unique(view.self.activeFamilies.begin(), view.self.activeFamilies.end()), view.self.activeFamilies.end());
                break;
            default:
                if (target) {
                    target->hitPoints -= 12;
                    target->disabled = (decision.talent.effects & kEffectDisable) != 0;
                }
                break;
            }
        }
        view.talents.erase(std::remove_if(view.talents.begin(), view.talents.end(), [](const companion::Talent &t) { return t.item != 0 && t.count <= 0; }), view.talents.end());

        // The enemy answers: a random member of the party takes a random hit.
        std::vector<Actor *> party {&view.self};
        for (auto &ally : view.allies) {
            party.push_back(&ally);
        }
        Actor *victim = party[static_cast<size_t>(gameplayRandom().uniformInt(0, static_cast<int>(party.size()) - 1))];
        victim->hitPoints = std::max(1, victim->hitPoints - gameplayRandom().uniformInt(0, 14));
        if (round % 12 == 11) {
            view.self.activeFamilies.clear();
            view.forcePoints = std::min(100, view.forcePoints + 30);
        }

        view.enemies.erase(std::remove_if(view.enemies.begin(), view.enemies.end(), [](const Actor &e) { return e.hitPoints <= 0; }), view.enemies.end());
        if (view.enemies.empty()) {
            spawnWave(view, ++wave);
        }

        uint64_t hash = fnv1a(log.str());
        hash = fnv1a(describe(view.self), hash);
        for (const auto &ally : view.allies) {
            hash = fnv1a(describe(ally), hash);
        }
        for (const auto &enemy : view.enemies) {
            hash = fnv1a(describe(enemy), hash);
        }
        hash = fnv1a(std::to_string(view.forcePoints) + "/" + std::to_string(view.talents.size()), hash);
        trace.hashes.push_back(hash);
    }
    trace.draws = gameplayRandom().draws();
    return trace;
}

// A minigame tick loop: a swoop course with accelerator pads and obstacles, a
// steering autopilot, fire held down, and scripts that roll the gameplay stream.
class SwoopHost : public IMiniGameHost {
public:
    std::map<std::string, std::function<void(uint32_t)>> scripts;

    void runScript(const std::string &resRef, uint32_t callerId) override {
        auto it = scripts.find(resRef);
        if (it != scripts.end()) {
            it->second(callerId);
        }
    }
    void playMiniGameAnimation(const MiniGameActor &, const std::string &, bool, bool, bool) override {}
    void onMiniGameActorDied(const MiniGameActor &) override {}
};

MiniGameTrack swoopTrack() {
    glm::quat turned = glm::angleAxis(0.9f, glm::normalize(glm::vec3(0.2f, 0.3f, 0.9f)));
    return MiniGameTrack(glm::translate(glm::vec3(95.0f, 0.0f, 0.0f)),
                         glm::vec3(5.0f, 0.0f, 2.0f),
                         glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                         {{0.0f, glm::vec3(0.0f)}, {20.0f, glm::vec3(3.0f, 2000.0f, 1.0f)}, {48.0f, glm::vec3(0.0f, 4980.0f, 0.0f)}},
                         {{0.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)}, {16.0f, turned}, {48.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)}},
                         48.0f);
}

Trace minigameTicks(uint32_t seed, size_t shuffle) {
    Shuffler shuffler(shuffle);
    setRandomSeed(seed);
    setCosmeticRandomSeed(seed ^ 0x5a5a5a5au);

    MinigameSpec spec;
    spec.type = MinigameType::SwoopRace;
    spec.movementPerSec = 100.0f;
    spec.lateralAccel = 300.0f;
    spec.useInertia = true;
    spec.player.models = {{"v_superbike", true}};
    spec.player.hitPoints = 100000;
    spec.player.maxHitPoints = 100000;
    spec.player.sphereRadius = 3.0f;
    spec.player.invincePeriod = 0.3f;
    spec.player.accelSecs = 25.0f;
    spec.player.numLoops = 1;
    spec.player.slots[static_cast<size_t>(MiniGameSlot::OnHeartbeat)] = "heartbeat";
    spec.player.slots[static_cast<size_t>(MiniGameSlot::OnHitFollower)] = "pad";
    spec.player.slots[static_cast<size_t>(MiniGameSlot::OnHitObstacle)] = "rock";
    MinigameGunBankSpec bank;
    bank.bullet.rateOfFire = 0.2f;
    bank.bullet.lifespan = 0.5f;
    bank.bullet.targetType = 3;
    spec.player.gunBanks.push_back(bank);
    std::vector<MiniGameTrack> padTracks;
    for (int i = 0; i < 8; ++i) {
        MinigameEnemySpec pad;
        pad.models = {{"mgf_accelpad01", true}};
        pad.hitPoints = 1;
        pad.maxHitPoints = 1;
        pad.sphereRadius = 3.0f;
        pad.numLoops = -1;
        pad.trigger = true;
        spec.enemies.push_back(pad);
        padTracks.push_back(MiniGameTrack::fixed(glm::vec3(90.0f + 9.0f * static_cast<float>((i * 5) % 4), 150.0f + 160.0f * static_cast<float>(i), 0.0f)));
    }
    MinigameObstacleSpec rock;
    rock.name = "rock";
    spec.obstacles.push_back(rock);
    std::vector<MiniGameSession::ObstaclePlacement> rocks;
    for (int i = 0; i < 4; ++i) {
        MiniGameSession::ObstaclePlacement placement;
        placement.name = "rock";
        placement.position = glm::vec3(100.0f, 300.0f + 300.0f * static_cast<float>(i), 0.0f);
        placement.hasHitbump = true;
        placement.hitbumpMin = placement.position + glm::vec3(-4.0f, -2.0f, -2.0f);
        placement.hitbumpMax = placement.position + glm::vec3(4.0f, 2.0f, 4.0f);
        rocks.push_back(placement);
    }

    SwoopHost host;
    MiniGameSession session(host);
    session.load(spec, swoopTrack(), std::move(padTracks), std::move(rocks));
    for (size_t i = 0; i < session.actors().size(); ++i) {
        session.setObjectId(i, static_cast<uint32_t>(100 + i));
    }
    host.scripts["heartbeat"] = [&](uint32_t) {
        float speed = session.playerSpeed();
        session.setLateralAccel(speed < 300.0f ? speed : 300.0f);
    };
    host.scripts["pad"] = [&](uint32_t) {
        auto player = session.player();
        if (!player->invulnerable()) {
            session.setAccelPerSecond(session.accelPerSecond() * (1.0f + 0.05f * static_cast<float>(randomInt(1, 3))));
            session.setPlayerSpeed(session.playerSpeed() * 1.05f);
        }
        player->invulnerableTime = player->invincePeriod;
        // A lasting consequence of the roll: the speed boost saturates at the cap.
        session.changeHitPoints(*player, -randomInt(1, 50));
        if (auto pad = session.actorByObjectId(player->lastFollowerHit)) {
            session.changeHitPoints(*pad, -100);
        }
    };
    host.scripts["rock"] = [&](uint32_t) {
        session.changeHitPoints(*session.player(), -randomInt(5, 20));
    };
    session.setPlayerSpeed(60.0f);
    session.setPlayerMaxSpeed(220.0f);
    session.setAccelPerSecond(12.0f);
    session.setTunnelPos(glm::vec3(20.0f, 0.0f, 0.0f));
    session.setTunnelNeg(glm::vec3(-20.0f, 0.0f, 0.0f));
    session.start();

    Trace trace;
    MiniGameInput input;
    input.fire = true;
    for (int tick = 0; tick < 900; ++tick) {
        input.steer = static_cast<float>(((tick / 37) % 5) - 2) * 0.5f;
        session.update(kTick, input);
        std::ostringstream out;
        out << session.tickCount() << ' ' << bits(session.playerSpeed()) << ' ' << bits(session.offset().x) << ' '
            << bits(session.lateralVelocity().x);
        for (const auto &actor : session.actors()) {
            out << '|' << actor.hitPoints << ' ' << actor.dead << ' ' << bits(actor.position.x) << ' ' << bits(actor.position.y)
                << ' ' << bits(actor.position.z) << ' ' << bits(actor.hook[0][0]) << ' ' << bits(actor.hook[1][2]);
        }
        trace.hashes.push_back(fnv1a(out.str()));
    }
    trace.draws = gameplayRandom().draws();
    return trace;
}

struct Source {
    const char *name;
    std::function<Trace(uint32_t seed, size_t shuffle)> trace;
};

// Every scenario above plus the models: what the dump and the golden file cover.
const std::vector<Source> &sources() {
    static const std::vector<Source> all = [] {
        std::vector<Source> list;
        for (const auto &entry : scenarios()) {
            list.push_back({entry.name, [&entry](uint32_t seed, size_t shuffle) { return run(entry.scenario, entry.ticks, seed, shuffle); }});
        }
        list.push_back({"brain", brainFight});
        list.push_back({"minigame", minigameTicks});
        return list;
    }();
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
    for (const auto &entry : sources()) {
        Trace first = entry.trace(kSeed, 0);
        Trace second = entry.trace(kSeed, 0);
        ASSERT_EQ(first.hashes.size(), second.hashes.size()) << entry.name;
        for (size_t tick = 0; tick < first.hashes.size(); ++tick) {
            ASSERT_EQ(first.hashes[tick], second.hashes[tick]) << entry.name << " diverged at tick " << tick + 1;
        }
        EXPECT_EQ(first.draws, second.draws) << entry.name;
    }
}

TEST(Determinism, scenarios_that_roll_dice_follow_the_seed_and_the_rest_do_not) {
    for (const auto &entry : sources()) {
        Trace a = entry.trace(kSeed, 0);
        Trace b = entry.trace(kSeed + 1, 0);
        if (a.draws == 0 && b.draws == 0) {
            EXPECT_EQ(a.hashes, b.hashes) << entry.name << " draws no gameplay randomness, so the seed must not matter";
        } else {
            // The run, not only its last tick: the swoop course ends at the speed cap
            // whatever the pads rolled, but the ride there differs.
            EXPECT_NE(a.hashes, b.hashes) << entry.name << " rolls dice, so a different seed must change the outcome";
        }
    }
}

TEST(Determinism, scenarios_actually_change_the_world) {
    // A hash that never moves would pass every comparison above.
    for (const auto &entry : sources()) {
        Trace trace = entry.trace(kSeed, 0);
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
    for (const auto &entry : sources()) {
        Trace trace = entry.trace(kSeed, 0);
        for (size_t tick = 0; tick < trace.hashes.size(); ++tick) {
            out << entry.name << ' ' << tick + 1 << ' ' << hex(trace.hashes[tick]) << '\n';
        }
    }
}

TEST(DeterminismGolden, hashes_match_the_ones_recorded_on_linux) {
    std::map<std::string, std::string> recorded; // "scenario tick" -> hash
    std::ostringstream fresh;
    for (const auto &entry : sources()) {
        Trace trace = entry.trace(kSeed, 0);
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

// The same run with the heap laid out differently: nothing may follow addresses.
TEST(Determinism, a_shuffled_allocation_order_gives_the_same_state_every_tick) {
    for (const auto &entry : sources()) {
        Trace plain = entry.trace(kSeed, 0);
        for (size_t shuffle : {1u, 9u, 40u}) {
            Trace shuffled = entry.trace(kSeed, shuffle);
            ASSERT_EQ(plain.hashes.size(), shuffled.hashes.size()) << entry.name;
            for (size_t tick = 0; tick < plain.hashes.size(); ++tick) {
                ASSERT_EQ(plain.hashes[tick], shuffled.hashes[tick]) << entry.name << " shuffle " << shuffle << " diverged at tick " << tick + 1;
            }
            EXPECT_EQ(plain.draws, shuffled.draws) << entry.name;
        }
    }
}

TEST(Determinism, the_brain_fight_and_the_minigame_actually_roll_dice) {
    // Guard against scenarios that quietly do nothing.
    Trace brain = brainFight(kSeed, 0);
    Trace minigame = minigameTicks(kSeed, 0);
    EXPECT_GT(brain.draws, 90u);
    EXPECT_GT(minigame.draws, 0u) << "pads or rocks were never hit: the scripts never rolled";
}

// Fixes: trigonometry on the simulation path goes through portable::.

TEST(Determinism, a_location_built_from_an_orientation_uses_the_portable_arctangent) {
    for (float angle = -3.1f; angle < 3.1f; angle += 0.37f) {
        glm::vec3 direction(portable::cosf(angle), portable::sinf(angle), 0.0f);
        Location location(glm::vec3(0.0f), direction);
        EXPECT_EQ(location.facing(), portable::atan2f(direction.y, direction.x));
        EXPECT_NEAR(location.facing(), angle, 1e-5f);
        glm::vec3 saved = Location(glm::vec3(0.0f), angle).saveOrientation();
        EXPECT_EQ(saved.x, portable::cosf(angle));
        EXPECT_EQ(saved.y, portable::sinf(angle));
    }
}

TEST(Determinism, the_turret_aim_orientation_is_built_from_portable_trigonometry) {
    TurretAim aim;
    for (float yaw = -3.0f; yaw < 3.0f; yaw += 0.41f) {
        for (float pitch = -0.6f; pitch < 0.6f; pitch += 0.23f) {
            aim.set(pitch, yaw);
            glm::quat expected = glm::angleAxis(aim.yaw(), glm::vec3(0.0f, 0.0f, 1.0f)) *
                                 glm::angleAxis(aim.pitch(), glm::vec3(1.0f, 0.0f, 0.0f));
            glm::quat got = aim.orientation();
            EXPECT_NEAR(got.w, expected.w, 1e-6f);
            EXPECT_NEAR(got.x, expected.x, 1e-6f);
            EXPECT_NEAR(got.y, expected.y, 1e-6f);
            EXPECT_NEAR(got.z, expected.z, 1e-6f);
        }
    }
}

TEST(Determinism, a_track_hook_blends_orientations_to_the_expected_angle) {
    glm::quat from(1.0f, 0.0f, 0.0f, 0.0f);
    glm::quat to = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));
    MiniGameTrack track(glm::mat4(1.0f), glm::vec3(0.0f), from, {}, {{0.0f, from}, {10.0f, to}}, 10.0f);
    for (float t : {0.0f, 2.5f, 5.0f, 7.5f, 10.0f}) {
        glm::mat4 hook = track.hookAt(t);
        glm::vec3 forward(hook * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        float expectedAngle = glm::half_pi<float>() * t / 10.0f;
        EXPECT_NEAR(forward.x, std::cos(expectedAngle), 1e-5f) << t;
        EXPECT_NEAR(forward.y, std::sin(expectedAngle), 1e-5f) << t;
    }
}
