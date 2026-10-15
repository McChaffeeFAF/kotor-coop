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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <glm/gtc/quaternion.hpp>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../fixtures/resource.h"

#include "reone/game/coopminigame.h"
#include "reone/game/game.h"
#include "reone/game/minigame.h"
#include "reone/game/party.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

constexpr float kTick = 1.0f / 30.0f;

net::MiniGameControl control(uint32_t seq, float steer, bool fire = false) {
    net::MiniGameControl c;
    c.seq = seq;
    c.steer = steer;
    c.fire = fire;
    return c;
}

class QuietHost : public IMiniGameHost {
public:
    std::function<void(const std::string &)> onScript;
    void runScript(const std::string &resRef, uint32_t) override {
        if (onScript) {
            onScript(resRef);
        }
    }
    void playMiniGameAnimation(const MiniGameActor &, const std::string &, bool, bool, bool) override {}
    void onMiniGameActorDied(const MiniGameActor &) override {}
};

MiniGameTrack course() {
    return MiniGameTrack(glm::translate(glm::vec3(95.0f, 0.0f, 0.0f)), glm::vec3(5.0f, 0.0f, 2.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                         {{0.0f, glm::vec3(0.0f)}, {48.0f, glm::vec3(0.0f, 4980.0f, 0.0f)}}, {}, 48.0f);
}

MinigameSpec raceSpec() {
    MinigameSpec spec;
    spec.type = MinigameType::SwoopRace;
    spec.movementPerSec = 100.0f;
    spec.lateralAccel = 300.0f;
    spec.useInertia = true;
    spec.player.models = {{"v_superbike", true}};
    spec.player.hitPoints = 999;
    spec.player.maxHitPoints = 999;
    spec.player.sphereRadius = 3.0f;
    spec.player.accelSecs = 25.0f;
    spec.player.numLoops = 1;
    spec.player.slots[static_cast<size_t>(MiniGameSlot::OnCreate)] = "oncreate";
    return spec;
}

// A race for the player alone: the creation script gives it speed and a lateral limit, as the
// K1 swoop scripts do, and nothing else runs.
struct Race {
    QuietHost host;
    std::unique_ptr<MiniGameSession> session;

    Race() {
        session = std::make_unique<MiniGameSession>(host);
        host.onScript = [this](const std::string &) {
            session->setPlayerSpeed(60.0f);
            session->setPlayerMaxSpeed(120.0f);
            session->setAccelPerSecond(10.0f);
            session->setLateralAccel(300.0f);
            session->setTunnelPos(glm::vec3(20.0f, 0.0f, 0.0f));
            session->setTunnelNeg(glm::vec3(-20.0f, 0.0f, 0.0f));
        };
        session->load(raceSpec(), course(), {}, {});
        session->setObjectId(0, 100);
        session->start();
    }
};

} // namespace

TEST(CoopMiniGame, nobody_remote_is_listened_to_when_the_host_plays) {
    CoopMiniGame game;
    EXPECT_EQ(CoopMiniGame::Verdict::NotRunning, game.submit(1, control(1, 1.0f), 0)) << "nothing is running";
    game.begin(net::MiniGameKind::SwoopRace, "tar_m03mg", 0, "", false);
    EXPECT_TRUE(game.running());
    EXPECT_FALSE(game.remoteOwned());
    EXPECT_EQ(CoopMiniGame::Verdict::NotRunning, game.submit(1, control(1, 1.0f), 0));
    MiniGameInput input;
    input.steer = 0.25f;
    game.apply(input, 1);
    EXPECT_FLOAT_EQ(0.25f, input.steer) << "the host's own keys are left alone";
}

TEST(CoopMiniGame, the_owner_steers_and_nobody_else_does) {
    CoopMiniGame game;
    game.begin(net::MiniGameKind::SwoopRace, "tar_m03mg", 7, "Alice", true);
    EXPECT_TRUE(game.remoteOwned());
    EXPECT_EQ("Alice", game.owner());
    EXPECT_EQ(CoopMiniGame::Verdict::NotOwner, game.submit(8, control(1, 1.0f), 10));
    EXPECT_EQ(CoopMiniGame::Verdict::NotOwner, game.submit(0, control(1, 1.0f), 10));
    MiniGameInput input;
    game.apply(input, 10);
    EXPECT_FLOAT_EQ(0.0f, input.steer) << "another player's input never lands";

    EXPECT_EQ(CoopMiniGame::Verdict::Accepted, game.submit(7, control(1, -0.5f, true), 10));
    game.apply(input, 10);
    EXPECT_FLOAT_EQ(-0.5f, input.steer);
    EXPECT_TRUE(input.fire);
}

TEST(CoopMiniGame, an_old_or_repeated_input_is_dropped) {
    CoopMiniGame game;
    game.begin(net::MiniGameKind::SwoopRace, "m", 7, "Alice", true);
    EXPECT_EQ(CoopMiniGame::Verdict::Accepted, game.submit(7, control(5, 0.5f), 10));
    EXPECT_EQ(CoopMiniGame::Verdict::Stale, game.submit(7, control(5, -1.0f), 10)) << "the same number again";
    EXPECT_EQ(CoopMiniGame::Verdict::Stale, game.submit(7, control(4, -1.0f), 10)) << "an older one";
    EXPECT_EQ(CoopMiniGame::Verdict::Accepted, game.submit(7, control(6, 0.25f), 11));
    MiniGameInput input;
    game.apply(input, 11);
    EXPECT_FLOAT_EQ(0.25f, input.steer);
}

TEST(CoopMiniGame, input_is_held_for_half_a_second_then_lets_go) {
    CoopMiniGame game;
    game.begin(net::MiniGameKind::SwoopRace, "m", 7, "Alice", true);
    game.submit(7, control(1, 1.0f, true), 100);
    MiniGameInput input;
    game.apply(input, 100 + kMiniGameInputHoldTicks);
    EXPECT_FLOAT_EQ(1.0f, input.steer) << "still held at the limit";
    EXPECT_TRUE(input.fire);
    game.apply(input, 100 + kMiniGameInputHoldTicks + 1);
    EXPECT_FLOAT_EQ(0.0f, input.steer) << "a player who stopped sending lets go";
    EXPECT_FALSE(input.fire);
    game.submit(7, control(2, -1.0f), 200);
    game.apply(input, 201);
    EXPECT_FLOAT_EQ(-1.0f, input.steer) << "and picks up again with the next input";
}

TEST(CoopMiniGame, aim_deltas_add_up_are_limited_and_used_once) {
    CoopMiniGame game;
    game.begin(net::MiniGameKind::Turret, "m12ab", 7, "Alice", false);
    net::MiniGameControl c = control(1, 0.0f);
    c.pitchDelta = 30.0f;
    c.yawDelta = -20.0f;
    c.pitchRate = 1.0f;
    game.submit(7, c, 5);
    c.seq = 2;
    c.pitchDelta = 40.0f;
    c.yawDelta = -10.0f;
    game.submit(7, c, 5);
    MiniGameInput input;
    game.apply(input, 5);
    EXPECT_FLOAT_EQ(70.0f, input.pitchDelta);
    EXPECT_FLOAT_EQ(-30.0f, input.yawDelta);
    EXPECT_FLOAT_EQ(1.0f, input.pitchRate);
    game.apply(input, 6);
    EXPECT_FLOAT_EQ(0.0f, input.pitchDelta) << "a delta is consumed by the tick that used it";
    EXPECT_FLOAT_EQ(0.0f, input.yawDelta);
    for (uint32_t seq = 3; seq < 30; ++seq) {
        c.seq = seq;
        c.pitchDelta = net::kMaxMiniGameDelta;
        game.submit(7, c, 6);
    }
    game.apply(input, 6);
    EXPECT_FLOAT_EQ(90.0f, input.pitchDelta) << "a flood cannot turn the turret more than a quarter turn at once";
}

TEST(CoopMiniGame, handing_over_and_letting_go_move_the_controls) {
    CoopMiniGame game;
    game.begin(net::MiniGameKind::Turret, "m12ab", 0, "", false);
    game.handOver(7, "Alice");
    EXPECT_TRUE(game.remoteOwned());
    EXPECT_EQ(CoopMiniGame::Verdict::Accepted, game.submit(7, control(1, 0.0f, true), 3));
    game.handOver(9, "Bob");
    EXPECT_EQ(CoopMiniGame::Verdict::NotOwner, game.submit(7, control(2, 0.0f, true), 4)) << "Alice no longer drives it";
    EXPECT_EQ(CoopMiniGame::Verdict::Accepted, game.submit(9, control(1, 0.5f), 4)) << "Bob starts from a clean sequence";
    EXPECT_EQ(CoopMiniGame::Verdict::NotOwner, game.quit(7));
    EXPECT_EQ(CoopMiniGame::Verdict::Accepted, game.quit(9));
    EXPECT_FALSE(game.remoteOwned()) << "the host has it again";
    EXPECT_EQ(CoopMiniGame::Verdict::NotRunning, game.quit(9));
    MiniGameInput input;
    input.steer = 0.75f;
    game.apply(input, 5);
    EXPECT_FLOAT_EQ(0.75f, input.steer);
    game.handOver(0, "");
    CoopMiniGame idle;
    idle.handOver(7, "Alice");
    EXPECT_FALSE(idle.running()) << "nothing to hand over";
}

TEST(CoopMiniGame, a_finished_race_pays_its_driver_and_nothing_else_does) {
    EXPECT_EQ(100, miniGamePrize(net::MiniGameKind::SwoopRace, true, false, 100));
    EXPECT_EQ(0, miniGamePrize(net::MiniGameKind::SwoopRace, false, false, 100)) << "the bike was destroyed";
    EXPECT_EQ(0, miniGamePrize(net::MiniGameKind::SwoopRace, true, true, 100)) << "the driver quit";
    EXPECT_EQ(0, miniGamePrize(net::MiniGameKind::Turret, true, false, 100)) << "turrets pay nothing";
    EXPECT_EQ(0, miniGamePrize(net::MiniGameKind::SwoopRace, true, false, -5));

    CoopMiniGame race;
    race.begin(net::MiniGameKind::SwoopRace, "tar_m03mg", 7, "Alice", true);
    auto won = race.end(900, 30.0f, false, 250);
    EXPECT_EQ(7u, won.client);
    EXPECT_EQ("Alice", won.owner);
    EXPECT_TRUE(won.finished);
    EXPECT_FALSE(won.quit);
    EXPECT_TRUE(won.sandboxed);
    EXPECT_EQ(250, won.prize);
    EXPECT_FLOAT_EQ(30.0f, won.elapsed);
    EXPECT_EQ(900u, won.ticks);
    EXPECT_FALSE(race.running());

    race.begin(net::MiniGameKind::SwoopRace, "tar_m03mg", 7, "Alice", true);
    auto crashed = race.end(100, 3.0f, true, 250);
    EXPECT_FALSE(crashed.finished);
    EXPECT_EQ(0, crashed.prize);

    race.begin(net::MiniGameKind::SwoopRace, "tar_m03mg", 7, "Alice", true);
    race.quit(7);
    auto left = race.end(100, 3.0f, false, 250);
    EXPECT_TRUE(left.quit);
    EXPECT_FALSE(left.finished);
    EXPECT_EQ(0, left.prize);
    EXPECT_EQ(0u, left.client) << "it was the host's by then";
    EXPECT_TRUE(left.sandboxed) << "a guest's race stays sandboxed even when the host finishes it";

    race.begin(net::MiniGameKind::SwoopRace, "tar_m03mg", 0, "", true);
    auto hosts = race.end(100, 3.0f, false, 250);
    EXPECT_EQ(0, hosts.prize) << "the host's own race pays through its scripts, not this";
    EXPECT_FALSE(hosts.sandboxed) << "and its story effects stay";
}

TEST(CoopMiniGame, a_run_is_a_function_of_the_owners_inputs) {
    auto drive = [](const std::vector<std::pair<uint64_t, float>> &steering) {
        Race race;
        CoopMiniGame game;
        game.begin(net::MiniGameKind::SwoopRace, "tar_m03mg", 7, "Alice", true);
        uint32_t seq = 0;
        std::vector<glm::vec3> trace;
        for (uint64_t tick = 1; tick <= 120; ++tick) {
            for (const auto &[at, steer] : steering) {
                if (at == tick) {
                    game.submit(7, control(++seq, steer, true), tick);
                }
            }
            MiniGameInput input;
            game.apply(input, tick);
            race.session->update(kTick, input);
            trace.push_back(race.session->player()->position);
        }
        return trace;
    };
    auto a = drive({{5, 1.0f}, {40, -1.0f}, {80, 0.0f}});
    auto b = drive({{5, 1.0f}, {40, -1.0f}, {80, 0.0f}});
    auto c = drive({{5, -1.0f}, {40, 1.0f}, {80, 0.0f}});
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i], b[i]) << "tick " << i + 1 << ": the same inputs give the same race";
    }
    EXPECT_NE(a.back().x, c.back().x) << "other inputs give another one";
    EXPECT_GT(a[30].x, a[0].x) << "steering right moves the bike right";
    EXPECT_LT(c[30].x, c[0].x);
}

TEST(CoopMiniGame, the_state_sent_to_clients_describes_the_simulation) {
    Race race;
    MiniGameInput input;
    for (int i = 0; i < 45; ++i) {
        race.session->update(kTick, input);
    }
    auto state = captureMiniGameState(*race.session, "tar_m03mg", "Alice");
    EXPECT_TRUE(state.active);
    EXPECT_EQ(net::MiniGameKind::SwoopRace, state.kind);
    EXPECT_EQ("tar_m03mg", state.module);
    EXPECT_EQ("Alice", state.owner);
    EXPECT_EQ(45u, state.tick);
    EXPECT_NEAR(1.5f, state.elapsed, 1e-3f);
    EXPECT_GT(state.speed, 60.0f);
    EXPECT_EQ(999, state.hp);
    EXPECT_EQ(999, state.maxHp);
    EXPECT_FALSE(state.finished);
    ASSERT_EQ(1u, state.actors.size());
    EXPECT_EQ(net::MiniGameRole::Player, state.actors[0].kind);
    EXPECT_FALSE(state.actors[0].dead);
    EXPECT_FLOAT_EQ(race.session->player()->position.y, state.actors[0].position.y);
    net::MiniGameState back;
    ASSERT_TRUE(net::decode(net::encode(state), back));
    EXPECT_EQ(state.tick, back.tick);

    race.session->kill(*race.session->player());
    EXPECT_TRUE(captureMiniGameState(*race.session, "tar_m03mg", "Alice").finished) << "a destroyed bike ends the player's part";
}

class CoopMiniGameStoryTest : public Test {
protected:
    void SetUp() override {
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _game->initLocalServices();
    }

    TestEngine _engine;
    StubConsole _console;
    std::unique_ptr<Game> _game;
};

TEST_F(CoopMiniGameStoryTest, a_guests_race_leaves_the_story_as_it_was) {
    _game->setGlobalBoolean("TAR_RACE_WON", false);
    _game->setGlobalNumber("TAR_RACE_STATE", 1);
    _game->setGlobalString("TAR_NOTE", "before");
    _game->party().giveGold(500);
    _game->party().setXP(1000);
    auto saved = captureStory(*_game);

    // The minigame's scripts run in the host's game and change the story.
    _game->setGlobalBoolean("TAR_RACE_WON", true);
    _game->setGlobalBoolean("TAR_NEW_FLAG", true);
    _game->setGlobalNumber("TAR_RACE_STATE", 3);
    _game->setGlobalNumber("TAR_NEW_COUNT", 9);
    _game->setGlobalString("TAR_NOTE", "after");
    _game->setGlobalString("TAR_NEW_NOTE", "new");
    _game->party().giveGold(300);
    _game->party().setXP(1400);

    restoreStory(*_game, saved);
    EXPECT_FALSE(_game->getGlobalBoolean("TAR_RACE_WON"));
    EXPECT_FALSE(_game->getGlobalBoolean("TAR_NEW_FLAG")) << "a flag it created reads as never set";
    EXPECT_EQ(1, _game->getGlobalNumber("TAR_RACE_STATE"));
    EXPECT_EQ(0, _game->getGlobalNumber("TAR_NEW_COUNT"));
    EXPECT_EQ("before", _game->getGlobalString("TAR_NOTE"));
    EXPECT_EQ("", _game->getGlobalString("TAR_NEW_NOTE"));
    EXPECT_EQ(500, _game->party().gold());
    EXPECT_EQ(1000, _game->party().xp());

    // Spent credits come back too.
    _game->party().takeGold(200);
    restoreStory(*_game, saved);
    EXPECT_EQ(500, _game->party().gold());
}

// The guest's client: playing the host's states back

namespace {

net::MiniGameState stateAt(uint32_t tick, float x, float y) {
    net::MiniGameState s;
    s.module = "tar_m03mg";
    s.tick = tick;
    s.elapsed = tick * kTick;
    s.actors.push_back({net::MiniGameRole::Player, 10, false, {x, y, 0.0f}});
    return s;
}

} // namespace

TEST(CoopMiniGameReplay, the_clock_runs_behind_the_newest_state_and_blends_between_states) {
    MiniGameReplay replay;
    replay.push(stateAt(30, 0.0f, 0.0f));
    replay.advance(kTick);
    EXPECT_FLOAT_EQ(30.0f, replay.clock()) << "one state: nothing to blend, nothing extrapolated";
    replay.push(stateAt(33, 3.0f, 30.0f));
    replay.push(stateAt(36, 6.0f, 60.0f));
    for (int i = 0; i < 30; ++i) {
        replay.advance(kTick);
        // A new state every third tick, as the host sends them.
        if (i % 3 == 2) {
            uint32_t tick = 36 + 3 * (i / 3 + 1);
            replay.push(stateAt(tick, tick - 30.0f, (tick - 30.0f) * 10.0f));
        }
    }
    float newest = static_cast<float>(replay.newest().tick);
    // Just after a state arrives the newest has jumped one update (3 ticks) ahead of the clock.
    EXPECT_GE(replay.clock(), newest - MiniGameReplay::kDelayTicks - kMiniGameStateEveryTicks - 0.5f) << "about one and a half updates behind";
    EXPECT_LE(replay.clock(), newest - MiniGameReplay::kDelayTicks + 0.5f);
    auto s = replay.sample();
    ASSERT_EQ(1u, s.actors.size());
    EXPECT_NEAR(replay.clock() - 30.0f, s.actors[0].position.x, 1e-3f) << "positions blended at the clock";
    EXPECT_NEAR((replay.clock() - 30.0f) * 10.0f, s.actors[0].position.y, 1e-2f);
    EXPECT_LE(replay.buffered(), 12u);
}

TEST(CoopMiniGameReplay, a_stall_snaps_old_and_repeated_states_are_dropped_and_another_minigame_starts_over) {
    MiniGameReplay replay;
    replay.push(stateAt(30, 0.0f, 0.0f));
    replay.push(stateAt(33, 1.0f, 0.0f));
    replay.advance(kTick);
    replay.push(stateAt(31, 9.0f, 9.0f));
    replay.push(stateAt(33, 9.0f, 9.0f));
    EXPECT_EQ(33u, replay.newest().tick);
    EXPECT_EQ(1.0f, replay.newest().actors[0].position.x) << "an older or repeated state changes nothing";
    replay.push(stateAt(200, 5.0f, 5.0f));
    replay.advance(kTick);
    EXPECT_NEAR(200.0f - MiniGameReplay::kDelayTicks, replay.clock(), 1e-3f) << "after a stall the clock starts at the target";
    auto turret = stateAt(5, 0.0f, 0.0f);
    turret.kind = net::MiniGameKind::Turret;
    turret.module = "m12ab";
    replay.push(turret);
    EXPECT_EQ(1u, replay.buffered()) << "another minigame starts over";
    auto final = stateAt(6, 0.0f, 0.0f);
    final.kind = net::MiniGameKind::Turret;
    final.module = "m12ab";
    final.active = false;
    replay.push(final);
    EXPECT_FALSE(replay.newest().active) << "the final state is kept";
}

TEST(CoopMiniGameReplay, blending_takes_the_turret_yaw_the_short_way_and_leaves_wrecks_where_they_died) {
    net::MiniGameState a = stateAt(10, 0.0f, 0.0f);
    net::MiniGameState b = stateAt(13, 10.0f, 0.0f);
    a.kind = b.kind = net::MiniGameKind::Turret;
    a.offset = {10.0f, 0.0f, 350.0f};
    b.offset = {20.0f, 0.0f, 10.0f};
    a.actors.push_back({net::MiniGameRole::Enemy, 0, true, {5.0f, 5.0f, 5.0f}});
    b.actors.push_back({net::MiniGameRole::Enemy, 0, true, {50.0f, 50.0f, 50.0f}});
    glm::quat turn = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    b.actors[0].rotation = {turn.x, turn.y, turn.z, turn.w};
    auto m = blendMiniGameStates(a, b, 0.5f);
    EXPECT_NEAR(15.0f, m.offset.x, 1e-4f);
    EXPECT_NEAR(360.0f, m.offset.z, 1e-3f) << "350 to 10 passes through 0, not 180";
    EXPECT_NEAR(5.0f, m.actors[0].position.x, 1e-4f);
    glm::quat half(m.actors[0].rotation.w, m.actors[0].rotation.x, m.actors[0].rotation.y, m.actors[0].rotation.z);
    EXPECT_NEAR(45.0f, glm::degrees(glm::angle(half)), 0.5f) << "rotations are blended";
    EXPECT_EQ(5.0f, m.actors[1].position.x) << "a wreck is not moved";
    EXPECT_EQ(b.tick, blendMiniGameStates(a, b, 1.0f).tick);
}

TEST(CoopMiniGameReplay, a_session_placed_from_the_state_draws_the_bike_where_the_host_has_it) {
    // The host's race, run a while with steering.
    Race host;
    MiniGameInput input;
    input.steer = 1.0f;
    for (int i = 0; i < 40; ++i) {
        host.session->update(kTick, input);
    }
    input.fire = true;
    auto state = captureMiniGameState(*host.session, "tar_m03mg", "Alice", &input);
    EXPECT_TRUE(state.firing);
    net::MiniGameState wire;
    ASSERT_TRUE(net::decode(net::encode(state), wire));

    // The client's copy of the same module, never simulated: placed from the state.
    QuietHost quiet;
    MiniGameSession client(quiet);
    client.load(raceSpec(), course(), {}, {});
    auto died = applyMiniGameState(client, wire);
    EXPECT_TRUE(died.empty());
    const auto &h = *host.session->player();
    const auto &c = *client.player();
    EXPECT_NEAR(h.position.x, c.position.x, 1e-3f);
    EXPECT_NEAR(h.position.y, c.position.y, 1e-3f);
    // What the view draws: the hook moved by the offset (SwoopRace::placeActors).
    glm::vec3 hostDrawn(h.hook * glm::vec4(host.session->offset(), 1.0f));
    glm::vec3 clientDrawn(c.hook * glm::vec4(client.offset(), 1.0f));
    EXPECT_NEAR(0.0f, glm::distance(hostDrawn, clientDrawn), 1e-2f);
    EXPECT_NEAR(host.session->playerSpeed(), client.playerSpeed(), 1e-3f);
    EXPECT_EQ(0u, client.tickCount()) << "nothing was simulated";

    wire.actors[0].dead = true;
    died = applyMiniGameState(client, wire);
    ASSERT_EQ(1u, died.size());
    EXPECT_TRUE(client.player()->dead);
    EXPECT_TRUE(applyMiniGameState(client, wire).empty()) << "a death is reported once";
}

TEST(CoopMiniGameReplay, the_owners_input_becomes_a_control_message_and_the_end_a_line_of_text) {
    MiniGameInput input;
    input.steer = 3.0f;
    input.fire = true;
    input.yawRate = -1.0f;
    input.pitchDelta = 90.0f;
    auto c = miniGameControlFrom(input);
    EXPECT_EQ(1.0f, c.steer);
    EXPECT_TRUE(c.fire);
    EXPECT_EQ(-1.0f, c.yawRate);
    EXPECT_EQ(net::kMaxMiniGameDelta, c.pitchDelta);
    net::MiniGameControl back;
    EXPECT_TRUE(net::decode(net::encode(c), back)) << "what the client sends always decodes";

    auto race = stateAt(900, 0.0f, 0.0f);
    race.active = false;
    race.finished = true;
    race.elapsed = 62.3f;
    race.prize = 100;
    race.owner = "Alice";
    EXPECT_EQ("Your race is over: finished in 1:02.3, won 100 credits", miniGameResultText(race, true));
    EXPECT_EQ("Alice's race is over: finished in 1:02.3, won 100 credits", miniGameResultText(race, false));
    race.finished = false;
    race.prize = 0;
    EXPECT_EQ("Your race is over: the swoop was wrecked", miniGameResultText(race, true));
    auto turret = stateAt(900, 0.0f, 0.0f);
    turret.kind = net::MiniGameKind::Turret;
    turret.active = false;
    turret.finished = true;
    turret.hp = 5;
    turret.maxHp = 10;
    turret.actors.push_back({net::MiniGameRole::Enemy, 0, true, {}});
    turret.actors.push_back({net::MiniGameRole::Enemy, 0, true, {}});
    turret.actors.push_back({net::MiniGameRole::Enemy, 4, false, {}});
    EXPECT_EQ("The host's turret run is over: 2 of 3 destroyed", miniGameResultText(turret, false));
}

TEST(CoopMiniGameReplay, the_owners_turret_aim_is_drawn_ahead_by_what_the_host_has_not_used) {
    MiniGameAimPrediction aim;
    auto sent = [&](uint32_t seq, float pitch, float yaw) {
        net::MiniGameControl c;
        c.seq = seq;
        c.pitchDelta = pitch;
        c.yawDelta = yaw;
        aim.sent(c);
    };
    sent(1, 2.0f, 5.0f);
    sent(2, 0.0f, 0.0f); // nothing to predict
    sent(3, -1.0f, 6.0f);
    EXPECT_EQ(2u, aim.pending());
    net::MiniGameState s;
    s.kind = net::MiniGameKind::Turret;
    s.offset = {10.0f, 0.0f, 355.0f};
    s.controlSeq = 1; // the host has used the first
    aim.apply(s);
    EXPECT_FLOAT_EQ(9.0f, s.offset.x);
    EXPECT_NEAR(1.0f, s.offset.z, 1e-4f) << "yaw stays in [0, 360)";
    aim.acknowledge(3);
    EXPECT_EQ(0u, aim.pending());
    net::MiniGameState race;
    race.offset = {1.0f, 0.0f, 0.0f};
    sent(4, 5.0f, 5.0f);
    aim.apply(race);
    EXPECT_EQ(1.0f, race.offset.x) << "a swoop's offset is not an aim";
}

TEST(CoopMiniGameReplay, keyboard_turret_aim_is_drawn_ahead_too) {
    MiniGameAimPrediction aim;
    net::MiniGameControl c;
    c.seq = 5;
    c.pitchRate = 1.0f;
    aim.sent(c, 1.5f, -0.5f); // what the host's rate integrator gives this tick
    c.seq = 6;
    aim.sent(c, 2.0f, 0.0f);
    net::MiniGameState s;
    s.kind = net::MiniGameKind::Turret;
    s.offset = {0.0f, 0.0f, 10.0f};
    s.controlSeq = 5;
    aim.apply(s);
    EXPECT_FLOAT_EQ(2.0f, s.offset.x) << "only the step the host has not used yet";
    EXPECT_FLOAT_EQ(10.0f, s.offset.z);
    s.controlSeq = 4;
    s.offset = {0.0f, 0.0f, 10.0f};
    aim.apply(s);
    EXPECT_FLOAT_EQ(3.5f, s.offset.x);
    EXPECT_FLOAT_EQ(9.5f, s.offset.z);
}
