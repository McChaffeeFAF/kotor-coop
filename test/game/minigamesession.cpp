/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// The minigame simulation, driven through a scripted host that stands in for
// the module scripts. Behaviour: design note minigames.

#include <gtest/gtest.h>

#include <functional>
#include <map>

#include "reone/game/minigame.h"
#include "reone/system/fixedstep.h"

using namespace reone;
using namespace reone::game;

namespace {

constexpr float kTick = 1.0f / 30.0f;

class ScriptedHost : public IMiniGameHost {
public:
    using Script = std::function<void(uint32_t caller)>;

    std::map<std::string, Script> scripts;
    std::vector<std::pair<std::string, uint32_t>> runs;
    std::vector<std::string> animations;
    std::vector<std::string> died;

    void runScript(const std::string &resRef, uint32_t callerId) override {
        runs.emplace_back(resRef, callerId);
        auto it = scripts.find(resRef);
        if (it != scripts.end()) {
            it->second(callerId);
        }
    }
    void playMiniGameAnimation(const MiniGameActor &actor, const std::string &name, bool, bool, bool) override {
        animations.push_back(name);
    }
    void onMiniGameActorDied(const MiniGameActor &actor) override {
        died.push_back(actor.name);
    }

    size_t count(const std::string &resRef) const {
        size_t n = 0;
        for (const auto &run : runs) {
            if (run.first == resRef) {
                ++n;
            }
        }
        return n;
    }
};

MiniGameScripts slots(std::initializer_list<std::pair<MiniGameSlot, std::string>> entries) {
    MiniGameScripts result;
    for (const auto &entry : entries) {
        result[static_cast<size_t>(entry.first)] = entry.second;
    }
    return result;
}

// A swoop course shaped like Taris: the hook runs 4980 units along +Y in 48 s
// from a track placed at (95, 0, 0) with the hook resting at (5, 0, 2).
MiniGameTrack tarisTrack() {
    return MiniGameTrack(glm::translate(glm::vec3(95.0f, 0.0f, 0.0f)),
                         glm::vec3(5.0f, 0.0f, 2.0f),
                         glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                         {{0.0f, glm::vec3(0.0f)}, {48.0f, glm::vec3(0.0f, 4980.0f, 0.0f)}},
                         {},
                         48.0f);
}

MinigameSpec swoopSpec(size_t pads) {
    MinigameSpec spec;
    spec.type = MinigameType::SwoopRace;
    spec.movementPerSec = 100.0f;
    spec.lateralAccel = 300.0f;
    spec.useInertia = true;
    spec.player.models = {{"v_superbike", true}};
    spec.player.hitPoints = 999;
    spec.player.maxHitPoints = 999;
    spec.player.sphereRadius = 3.0f;
    spec.player.invincePeriod = 0.3f;
    spec.player.accelSecs = 25.0f;
    spec.player.numLoops = 1;
    spec.player.slots = slots({{MiniGameSlot::OnCreate, "oncreate"},
                               {MiniGameSlot::OnHeartbeat, "heartbeat"},
                               {MiniGameSlot::OnFire, "onfire"},
                               {MiniGameSlot::OnHitFollower, "accelpad"},
                               {MiniGameSlot::OnHitObstacle, "obstacle"}});
    MinigameGunBankSpec bank;
    bank.bullet.rateOfFire = 0.01f;
    bank.bullet.lifespan = 0.01f;
    bank.bullet.targetType = 3;
    spec.player.gunBanks.push_back(bank);
    for (size_t i = 0; i < pads; ++i) {
        MinigameEnemySpec pad;
        pad.models = {{"mgf_accelpad01", true}};
        pad.hitPoints = 1;
        pad.maxHitPoints = 1;
        pad.sphereRadius = 3.0f;
        pad.numLoops = -1;
        pad.trigger = true;
        spec.enemies.push_back(pad);
    }
    return spec;
}

// The K1 swoop scripts, reduced to what reaches the simulation (from
// tar_m03mg oncreate / heartbeat / onfire / accelpad / obstacle).
struct SwoopScripts {
    MiniGameSession *session {nullptr};
    int gear {-6};
    float clock {0.0f}; // the game clock the heartbeat reads
    float countdownStart {0.0f};
    float raceStart {0.0f};
    float raceTime {-1.0f};
    int boosts {0};

    void install(ScriptedHost &host) {
        host.scripts["oncreate"] = [this](uint32_t) {
            gear = -6;
            session->setPlayerSpeed(0.0f);
        };
        host.scripts["heartbeat"] = [this](uint32_t) {
            if (gear == -6) {
                countdownStart = clock;
                gear = -5;
            }
            float elapsed = clock - countdownStart;
            if (gear < 0 && elapsed > 5.0f) {
                raceStart = clock;
                gear = 0;
            }
            auto player = session->player();
            if (gear > -1 && gear < 6 && player->position.y > 3800.0f) {
                raceTime = clock - raceStart;
                session->setMinSpeed(0.0f);
                session->setPlayerMaxSpeed(0.0f);
                gear = 6;
            }
            float speed = session->playerSpeed();
            session->setLateralAccel(speed < 300.0f ? speed : 300.0f);
        };
        host.scripts["onfire"] = [this](uint32_t) {
            static const float kMin[] = {35.0f, 60.0f, 100.0f, 150.0f, 210.0f};
            static const float kMax[] = {70.0f, 120.0f, 190.0f, 225.0f, 420.0f};
            static const float kAccel[] = {20.0f, 8.0f, 6.0f, 3.0f, 2.0f};
            float speed = session->playerSpeed();
            bool shift = gear == 0 || (gear >= 1 && gear <= 4 && speed > kMin[gear - 1]);
            if (!shift) {
                return;
            }
            session->setMinSpeed(kMin[gear]);
            session->setPlayerMaxSpeed(kMax[gear]);
            session->setAccelPerSecond(kAccel[gear]);
            if (gear == 0) {
                session->setTunnelPos(glm::vec3(20.0f, 0.0f, 0.0f));
                session->setTunnelNeg(glm::vec3(-20.0f, 0.0f, 0.0f));
            }
            ++gear;
        };
        host.scripts["accelpad"] = [this](uint32_t caller) {
            auto player = session->player();
            if (!player->invulnerable() && gear != 5) {
                session->setAccelPerSecond(session->accelPerSecond() * 1.1f);
                session->setPlayerSpeed(session->playerSpeed() * 1.05f);
                ++boosts;
            }
            player->invulnerableTime = player->invincePeriod;
            if (auto pad = session->actorByObjectId(player->lastFollowerHit)) {
                session->changeHitPoints(*pad, -100);
            }
        };
    }
};

std::unique_ptr<MiniGameSession> makeSwoop(ScriptedHost &host, size_t pads, const std::vector<glm::vec3> &padPositions) {
    auto session = std::make_unique<MiniGameSession>(host);
    std::vector<MiniGameTrack> padTracks;
    for (const auto &position : padPositions) {
        padTracks.push_back(MiniGameTrack::fixed(position));
    }
    session->load(swoopSpec(pads), tarisTrack(), std::move(padTracks), {});
    for (size_t i = 0; i < session->actors().size(); ++i) {
        session->setObjectId(i, static_cast<uint32_t>(100 + i));
    }
    return session;
}

// One full race through a fixed-step clock fed at a given render rate.
struct RaceResult {
    float raceTime {-1.0f};
    uint64_t ticks {0};
    int boosts {0};
    glm::vec3 finish {0.0f};
};

RaceResult runRace(double frameSeconds) {
    std::vector<glm::vec3> pads {{111.573f, 190.006f, 0.0f}, {81.483f, 338.016f, 0.0f}, {113.217f, 528.379f, 0.0f},
                                 {102.557f, 582.581f, 0.0f}, {90.152f, 724.358f, 0.0f}, {107.983f, 852.101f, 0.0f}};
    ScriptedHost host;
    auto session = makeSwoop(host, pads.size(), pads);
    SwoopScripts scripts;
    scripts.session = session.get();
    scripts.install(host);
    session->start();

    FixedStepClock clock(30);
    MiniGameInput input;
    input.fire = true;
    RaceResult result;
    for (int frame = 0; frame < 200000 && scripts.raceTime < 0.0f; ++frame) {
        int steps = clock.advance(frameSeconds);
        for (int s = 0; s < steps && scripts.raceTime < 0.0f; ++s) {
            // Steer for the next pad ahead, as the harness autopilot does.
            auto player = session->player();
            float targetX = player->position.x;
            float bestY = 1e9f;
            for (int i = 0; i < session->enemyCount(); ++i) {
                auto pad = session->enemy(i);
                if (pad->position.y > player->position.y + 2.0f && pad->position.y < bestY) {
                    bestY = pad->position.y;
                    targetX = pad->position.x;
                }
            }
            float error = targetX - player->position.x;
            input.steer = glm::clamp(error / 4.0f - session->lateralVelocity().x / 100.0f, -1.0f, 1.0f);
            scripts.clock += clock.stepSeconds();
            session->update(clock.stepSeconds(), input);
        }
    }
    result.raceTime = scripts.raceTime;
    result.ticks = session->tickCount();
    result.boosts = scripts.boosts;
    result.finish = session->player()->position;
    return result;
}

} // namespace

TEST(MiniGameSession, speed_moves_toward_the_maximum_at_the_acceleration) {
    ScriptedHost host;
    auto session = makeSwoop(host, 0, {});
    session->setPlayerSpeed(0.0f);
    session->setPlayerMaxSpeed(70.0f);
    session->setAccelPerSecond(20.0f);
    MiniGameInput input;
    for (int i = 0; i < 30; ++i) {
        session->update(kTick, input);
    }
    EXPECT_NEAR(session->playerSpeed(), 20.0f, 1e-3f);
    for (int i = 0; i < 300; ++i) {
        session->update(kTick, input);
    }
    EXPECT_FLOAT_EQ(session->playerSpeed(), 70.0f);

    // From above it comes down to the maximum, and stops there.
    session->setPlayerMaxSpeed(50.0f);
    for (int i = 0; i < 300; ++i) {
        session->update(kTick, input);
    }
    EXPECT_FLOAT_EQ(session->playerSpeed(), 50.0f);
}

TEST(MiniGameSession, the_minimum_speed_is_only_stored) {
    ScriptedHost host;
    auto session = makeSwoop(host, 0, {});
    session->setPlayerSpeed(10.0f);
    session->setPlayerMaxSpeed(10.0f);
    session->setMinSpeed(60.0f);
    session->update(kTick, MiniGameInput());
    EXPECT_FLOAT_EQ(session->playerSpeed(), 10.0f);
    EXPECT_FLOAT_EQ(session->minSpeed(), 60.0f);
}

TEST(MiniGameSession, accel_secs_turns_into_acceleration_per_second) {
    ScriptedHost host;
    auto spec = swoopSpec(0);
    spec.player.minimumSpeed = 10.0f;
    spec.player.maximumSpeed = 60.0f;
    spec.player.accelSecs = 25.0f;
    MiniGameSession a(host);
    a.load(spec, tarisTrack(), {}, {});
    EXPECT_FLOAT_EQ(a.accelPerSecond(), 2.0f);
    spec.player.accelSecs = 0.0f;
    a.load(spec, tarisTrack(), {}, {});
    EXPECT_FLOAT_EQ(a.accelPerSecond(), 50.0f);
    spec.player.accelSecs = -1.0f;
    a.load(spec, tarisTrack(), {}, {});
    EXPECT_FLOAT_EQ(a.accelPerSecond(), -1.0f);
}

TEST(MiniGameSession, the_track_plays_at_speed_over_one_hundred) {
    ScriptedHost host;
    auto session = makeSwoop(host, 0, {});
    session->setPlayerSpeed(100.0f);
    session->setPlayerMaxSpeed(100.0f);
    for (int i = 0; i < 30; ++i) {
        session->update(kTick, MiniGameInput());
    }
    // One second at speed 100 is one second of the 48 s, 4980 unit track.
    EXPECT_NEAR(session->player()->position.y, 4980.0f / 48.0f, 0.05f);
    EXPECT_NEAR(session->player()->position.x, 100.0f, 1e-3f);
    EXPECT_NEAR(session->player()->position.z, 2.0f, 1e-3f);
}

TEST(MiniGameSession, steering_is_held_inside_the_signed_tunnel) {
    ScriptedHost host;
    auto session = makeSwoop(host, 0, {});
    session->setTunnelPos(glm::vec3(20.0f, 0.0f, 0.0f));
    session->setTunnelNeg(glm::vec3(-20.0f, 0.0f, 0.0f));
    MiniGameInput input;
    input.steer = 1.0f;
    for (int i = 0; i < 90; ++i) {
        session->update(kTick, input);
    }
    EXPECT_FLOAT_EQ(session->offset().x, 20.0f);
    // Hitting the wall stops the lateral drift.
    EXPECT_FLOAT_EQ(session->lateralVelocity().x, 0.0f);
    input.steer = -1.0f;
    for (int i = 0; i < 90; ++i) {
        session->update(kTick, input);
    }
    EXPECT_FLOAT_EQ(session->offset().x, -20.0f);
}

TEST(MiniGameSession, with_inertia_the_lateral_speed_settles_at_movement_per_second) {
    ScriptedHost host;
    auto session = makeSwoop(host, 0, {});
    session->setTunnelPos(glm::vec3(1e6f, 0.0f, 0.0f));
    session->setTunnelNeg(glm::vec3(-1e6f, 0.0f, 0.0f));
    MiniGameInput input;
    input.steer = 0.5f;
    session->update(kTick, input);
    EXPECT_LT(session->lateralVelocity().x, 50.0f); // it has to build up
    for (int i = 0; i < 300; ++i) {
        session->update(kTick, input);
    }
    EXPECT_NEAR(session->lateralVelocity().x, 50.0f, 0.01f);
}

TEST(MiniGameSession, creation_scripts_run_player_first_then_enemies) {
    ScriptedHost host;
    auto spec = swoopSpec(2);
    spec.enemies[0].slots = slots({{MiniGameSlot::OnCreate, "padcreate"}});
    spec.enemies[1].slots = slots({{MiniGameSlot::OnCreate, "padcreate"}});
    MiniGameSession session(host);
    session.load(spec, tarisTrack(), {MiniGameTrack::fixed(glm::vec3(0.0f)), MiniGameTrack::fixed(glm::vec3(0.0f))}, {});
    for (size_t i = 0; i < session.actors().size(); ++i) {
        session.setObjectId(i, static_cast<uint32_t>(10 + i));
    }
    session.start();
    ASSERT_EQ(host.runs.size(), 3u);
    EXPECT_EQ(host.runs[0], std::make_pair(std::string("oncreate"), 10u));
    EXPECT_EQ(host.runs[1], std::make_pair(std::string("padcreate"), 11u));
    EXPECT_EQ(host.runs[2], std::make_pair(std::string("padcreate"), 12u));
    session.start(); // once only
    EXPECT_EQ(host.runs.size(), 3u);
}

TEST(MiniGameSession, the_heartbeat_runs_every_update) {
    ScriptedHost host;
    auto session = makeSwoop(host, 0, {});
    for (int i = 0; i < 7; ++i) {
        session->update(kTick, MiniGameInput());
    }
    EXPECT_EQ(host.count("heartbeat"), 7u);
}

TEST(MiniGameSession, fire_runs_the_owners_on_fire_while_the_gun_is_ready) {
    ScriptedHost host;
    auto session = makeSwoop(host, 0, {});
    MiniGameInput input;
    input.fire = true;
    for (int i = 0; i < 5; ++i) {
        session->update(kTick, input);
    }
    // The 0.01 s swoop gun is ready again on every 1/30 s update.
    EXPECT_EQ(host.count("onfire"), 5u);
    input.fire = false;
    session->update(kTick, input);
    EXPECT_EQ(host.count("onfire"), 5u);
}

TEST(MiniGameSession, a_trigger_pad_raises_on_hit_follower_on_both_and_dies_to_the_script) {
    ScriptedHost host;
    std::vector<glm::vec3> pads {{100.0f, 30.0f, 2.0f}};
    auto session = makeSwoop(host, 1, pads);
    SwoopScripts scripts;
    scripts.session = session.get();
    scripts.gear = 1;
    scripts.install(host);
    session->setPlayerSpeed(100.0f);
    session->setPlayerMaxSpeed(100.0f);
    session->setAccelPerSecond(10.0f);
    int before = session->enemyCount();
    for (int i = 0; i < 30; ++i) {
        session->update(kTick, MiniGameInput());
    }
    EXPECT_EQ(scripts.boosts, 1);
    EXPECT_NEAR(session->accelPerSecond(), 11.0f, 1e-4f);
    EXPECT_EQ(session->player()->lastFollowerHit, 101u);
    EXPECT_EQ(session->actors()[1].lastFollowerHit, 100u);
    // -100 HP on a 1 HP pad kills it and takes it out of the enemy list.
    EXPECT_TRUE(session->actors()[1].dead);
    EXPECT_EQ(session->enemyCount(), before - 1);
    ASSERT_EQ(host.died.size(), 1u);
}

TEST(MiniGameSession, the_invulnerability_window_counts_down_by_frame_time) {
    ScriptedHost host;
    auto session = makeSwoop(host, 0, {});
    auto player = session->player();
    player->invulnerableTime = player->invincePeriod;
    EXPECT_TRUE(player->invulnerable());
    for (int i = 0; i < 8; ++i) {
        session->update(kTick, MiniGameInput());
    }
    EXPECT_TRUE(player->invulnerable()); // 0.3 s is 9 updates
    session->update(kTick, MiniGameInput());
    session->update(kTick, MiniGameInput());
    EXPECT_FALSE(player->invulnerable());
    EXPECT_FLOAT_EQ(player->invulnerableTime, 0.0f);
}

TEST(MiniGameSession, damage_defaults_and_scripted_overrides) {
    ScriptedHost host;
    auto spec = swoopSpec(1);
    spec.enemies[0].hitPoints = 100;
    spec.enemies[0].maxHitPoints = 100;
    spec.enemies[0].slots = slots({{MiniGameSlot::OnDeath, "sthdeath"}});
    spec.player.slots = slots({{MiniGameSlot::OnDamage, "hawkhit"}});
    MiniGameSession session(host);
    session.load(spec, tarisTrack(), {MiniGameTrack::fixed(glm::vec3(0.0f))}, {});
    session.setObjectId(0, 1);
    session.setObjectId(1, 2);
    auto &player = session.actors()[0];
    auto &enemy = session.actors()[1];

    // Without a script the change applies: a loss plays "damage".
    session.changeHitPoints(enemy, -30);
    EXPECT_EQ(enemy.hitPoints, 70);
    EXPECT_EQ(enemy.lastHPChange, -30);
    EXPECT_EQ(host.animations.front(), "damage");
    // A gain is capped at the maximum.
    session.changeHitPoints(enemy, 500);
    EXPECT_EQ(enemy.hitPoints, 100);

    // With an OnDamage script only the script runs; it asks for the default.
    int hawkhits = 0;
    host.scripts["hawkhit"] = [&](uint32_t) {
        ++hawkhits;
        session.defaultDamage(player);
    };
    session.changeHitPoints(player, -10);
    EXPECT_EQ(hawkhits, 1);
    EXPECT_EQ(player.hitPoints, 989);

    // Death runs OnDeath once; the script asks for the default death.
    int deaths = 0;
    host.scripts["sthdeath"] = [&](uint32_t) {
        ++deaths;
        session.defaultDeath(enemy);
    };
    session.changeHitPoints(enemy, -100);
    session.changeHitPoints(enemy, -100);
    EXPECT_EQ(deaths, 1);
    EXPECT_TRUE(enemy.dead);
    EXPECT_EQ(session.enemyCount(), 0);
    EXPECT_EQ(session.enemy(0), nullptr);
}

TEST(MiniGameSession, the_enemy_list_shifts_when_an_enemy_dies) {
    ScriptedHost host;
    std::vector<glm::vec3> pads {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    auto session = makeSwoop(host, 3, pads);
    ASSERT_EQ(session->enemyCount(), 3);
    session->kill(*session->enemy(0));
    ASSERT_EQ(session->enemyCount(), 2);
    EXPECT_EQ(session->enemy(0)->objectId, 102u);
    EXPECT_EQ(session->enemy(1)->objectId, 103u);
}

TEST(MiniGameSession, bullets_hit_through_on_hit_bullet_or_the_default) {
    ScriptedHost host;
    std::vector<glm::vec3> pads {{0.0f, 0.0f, 0.0f}};
    auto session = makeSwoop(host, 1, pads);
    auto &enemy = session->actors()[1];
    enemy.hitPoints = 100;
    session->bulletHit(enemy, 30, 2, 100, "hull");
    EXPECT_EQ(enemy.hitPoints, 70);
    EXPECT_EQ(enemy.lastBulletHitDamage, 30);
    EXPECT_EQ(enemy.lastBulletHitShooter, 100u);
    EXPECT_EQ(enemy.lastBulletHitPart, "hull");
}

TEST(MiniGameSession, a_swoop_obstacle_runs_on_hit_obstacle_once_per_crossing) {
    ScriptedHost host;
    auto session = std::make_unique<MiniGameSession>(host);
    MiniGameSession::ObstaclePlacement buoy;
    buoy.name = "m03mg_mgo22";
    buoy.position = glm::vec3(100.0f, 60.0f, 0.0f);
    buoy.hasHitbump = true;
    buoy.hitbumpMin = glm::vec3(97.0f, 58.0f, -1.0f);
    buoy.hitbumpMax = glm::vec3(103.0f, 62.0f, 1.0f);
    session->load(swoopSpec(0), tarisTrack(), {}, {buoy});
    session->setObjectId(0, 1);
    session->setObjectId(1, 2);
    session->setPlayerSpeed(100.0f);
    session->setPlayerMaxSpeed(100.0f);
    for (int i = 0; i < 60; ++i) {
        session->update(kTick, MiniGameInput());
    }
    EXPECT_EQ(host.count("obstacle"), 1u);
    EXPECT_EQ(session->player()->lastObstacleHit, 2u);
}

TEST(MiniGameSession, a_segment_that_passes_a_follower_still_touches_it) {
    // At speed the bike covers more than a pad's diameter in one update; the
    // test is between movement segments, not end points.
    EXPECT_NEAR(miniGameSegmentDistance(glm::vec3(0.0f, -10.0f, 0.0f), glm::vec3(0.0f, 10.0f, 0.0f),
                                        glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
                1.0f, 1e-5f);
    EXPECT_TRUE(miniGameSegmentHitsBox2D(glm::vec3(0.0f, -10.0f, 5.0f), glm::vec3(0.0f, 10.0f, 5.0f),
                                         glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f)));
    EXPECT_FALSE(miniGameSegmentHitsBox2D(glm::vec3(3.0f, -10.0f, 0.0f), glm::vec3(3.0f, 10.0f, 0.0f),
                                          glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f)));
}

TEST(MiniGameSession, the_turret_offset_wraps_on_an_infinite_axis) {
    ScriptedHost host;
    MinigameSpec spec;
    spec.type = MinigameType::Turret;
    spec.player.tunnelXNeg = 2.0f;
    spec.player.tunnelXPos = 45.0f;
    spec.player.tunnelInfinite = glm::vec3(0.0f, 0.0f, 1.0f);
    MiniGameSession session(host);
    session.load(spec, MiniGameTrack::fixed(glm::vec3(0.0f)), {}, {});
    session.setPlayerOffset(glm::vec3(80.0f, 0.0f, 370.0f));
    EXPECT_FLOAT_EQ(session.offset().x, 45.0f);
    EXPECT_FLOAT_EQ(session.offset().z, 10.0f);
    session.setPlayerOffset(glm::vec3(0.0f, 0.0f, -20.0f));
    EXPECT_FLOAT_EQ(session.offset().x, 2.0f);
    EXPECT_FLOAT_EQ(session.offset().z, 340.0f);
}

TEST(MiniGameSession, a_scripted_race_finishes_with_the_same_time_at_two_frame_rates) {
    RaceResult at60 = runRace(1.0 / 60.0);
    RaceResult at144 = runRace(1.0 / 144.0);
    ASSERT_GT(at60.raceTime, 0.0f);
    EXPECT_EQ(at60.raceTime, at144.raceTime);
    EXPECT_EQ(at60.ticks, at144.ticks);
    EXPECT_EQ(at60.boosts, at144.boosts);
    EXPECT_EQ(at60.finish, at144.finish);
    EXPECT_GT(at60.boosts, 0);
    EXPECT_GT(at60.finish.y, 3800.0f);
    // Five gears and a few pads: in the range of the Taris beat time (38.43 s).
    EXPECT_GT(at60.raceTime, 25.0f);
    EXPECT_LT(at60.raceTime, 50.0f);
}

TEST(PazaakCards, model_variation_selects_the_card) {
    // G_I_PazCard_001 (+1) is ModelVariation 7, _007 (-1) is 13, _013 (+-1) is 1.
    EXPECT_EQ(pazaakCardIndexForModelVariation(7, false), 0);
    EXPECT_EQ(pazaakCardIndexForModelVariation(12, false), 5);
    EXPECT_EQ(pazaakCardIndexForModelVariation(13, false), 6);
    EXPECT_EQ(pazaakCardIndexForModelVariation(18, false), 11);
    EXPECT_EQ(pazaakCardIndexForModelVariation(1, false), 12);
    EXPECT_EQ(pazaakCardIndexForModelVariation(6, false), 17);
    EXPECT_EQ(pazaakCardIndexForModelVariation(0, false), -1);
    EXPECT_EQ(pazaakCardIndexForModelVariation(19, false), -1);
    EXPECT_EQ(pazaakCardIndexForModelVariation(19, true), 22);
}
