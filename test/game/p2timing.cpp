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

// Playtest 1b, batch P2-Timing (design note p2_timing): combat round length,
// walk/run playback rates and frame-rate independence.

#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/resource.h"
#include "reone/audio/clip.h"
#include "reone/graphics/options.h"
#include "reone/resource/provider/textures.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/combat.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/system/fixedstep.h"
#include "reone/system/frameperf.h"
#include "reone/system/renderdiag.h"

#include <chrono>
#include <thread>

using namespace reone;
using namespace reone::game;

namespace {

constexpr float kTick = 1.0f / 30.0f;

struct RoundTicks {
    int secondAction {-1};
    int finished {-1};
};

RoundTicks measureRound(Game &game, const CombatRound &round) {
    RoundTicks result;
    for (int tick = 1; tick <= 200 && result.finished < 0; ++tick) {
        game.combat().update(kTick);
        if (result.secondAction < 0 && round.state == CombatRound::SecondAction) {
            result.secondAction = tick;
        }
        if (round.state == CombatRound::Finished) {
            result.finished = tick;
        }
    }
    return result;
}

} // namespace

// Retail: a round is 3000 ms and each of the two attacks in a duel gets
// 1500 ms. At 30 Hz that is 45 and 90 ticks, counted from the tick that opens
// the round.
TEST(P2Timing, combat_round_is_ninety_ticks_with_the_reply_at_forty_five) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto target = game.newCreature();
    auto attack = game.newAction<AttackObjectAction>(target);
    auto reply = game.newAction<AttackObjectAction>(attacker);

    const CombatRound &round = game.combat().addAction(attack, *attacker);
    game.combat().addAction(reply, *target);
    ASSERT_TRUE(round.duel);

    RoundTicks ticks = measureRound(game, round);

    EXPECT_EQ(45, ticks.secondAction);
    EXPECT_EQ(90, ticks.finished);
}

TEST(P2Timing, lone_combat_round_is_ninety_ticks) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto target = game.newCreature();
    auto attack = game.newAction<AttackObjectAction>(target);

    const CombatRound &round = game.combat().addAction(attack, *attacker);
    RoundTicks ticks = measureRound(game, round);

    EXPECT_EQ(90, ticks.finished);
}

// One loop of a walk/run clip covers walkdist/rundist metres (appearance.2da).
// Playback is ground speed / (loop distance / clip length). Figures are K1
// data: creaturespeed.2da rows and the clip lengths of the stock models.
TEST(P2Timing, gait_playback_matches_the_ground_the_clip_covers) {
    // Human male (S_Male02 run 0.7333 s, rundist 3.96; NORM run 5.4 m/s).
    EXPECT_NEAR(1.0f, gaitPlaybackRate(5.4f, 0.733333f, 3.96f), 0.01f);
    // Human male walk (1.0667 s, walkdist 1.813; NORM walk 1.7 m/s).
    EXPECT_NEAR(1.0f, gaitPlaybackRate(1.7f, 1.06667f, 1.813f), 0.01f);
    // Human female walk (1.0 s, walkdist 1.6).
    EXPECT_NEAR(1.0625f, gaitPlaybackRate(1.7f, 1.0f, 1.6f), 0.01f);
    // Wookiee run (0.7333 s, rundist 5.4): a slower lope than a human.
    EXPECT_NEAR(0.733f, gaitPlaybackRate(5.4f, 0.733333f, 5.4f), 0.01f);
    // Rakghoul run (crun 0.7667 s, rundist 5.0; FAST run 6.0 m/s).
    EXPECT_NEAR(0.92f, gaitPlaybackRate(6.0f, 0.766667f, 5.0f), 0.01f);
    // Kath hound A run (crun 0.6667 s, rundist 5.1; VFAST run 6.5 m/s).
    EXPECT_NEAR(0.85f, gaitPlaybackRate(6.5f, 0.666667f, 5.1f), 0.01f);
    // Astromech run (1.0 s, rundist 5.4).
    EXPECT_NEAR(1.0f, gaitPlaybackRate(5.4f, 1.0f, 5.4f), 0.01f);
}

TEST(P2Timing, gait_playback_is_unscaled_without_data) {
    EXPECT_FLOAT_EQ(1.0f, gaitPlaybackRate(5.4f, 0.0f, 3.96f));
    EXPECT_FLOAT_EQ(1.0f, gaitPlaybackRate(5.4f, 0.7333f, 0.0f));
    EXPECT_FLOAT_EQ(1.0f, gaitPlaybackRate(0.0f, 0.7333f, 3.96f));
}

// Simulated time never runs ahead of wall time, at any frame rate, through a
// stall, or through a burst of fast frames after one.
TEST(P2Timing, simulation_never_runs_ahead_of_wall_time) {
    const double frameTimes[] = {1.0 / 20, 1.0 / 30, 1.0 / 60, 1.0 / 144, 1.0 / 240, 0.0213};
    for (double frame : frameTimes) {
        FixedStepClock clock;
        double wall = 0.0;
        uint64_t steps = 0;
        for (int i = 0; i < 20000; ++i) {
            double dt = frame;
            if (i == 500) {
                dt = 1.5; // a stall: a synchronous load or a breakpoint
            }
            wall += dt;
            steps += clock.advance(dt);
            ASSERT_LE(steps / 30.0, wall + 1e-6) << "frame " << frame << " at " << i;
        }
        // Only the clamp loses time: the stall owed 45 steps, 5 were paid.
        double lost = static_cast<double>(clock.droppedSteps()) / 30.0;
        EXPECT_NEAR(wall - lost, steps / 30.0, 1.0 / 30.0) << "frame " << frame;
    }
}

// A frame never runs more than one tick's worth of catch-up per missed tick,
// and a steady 60 or 144 Hz stream never runs two ticks in one frame.
TEST(P2Timing, fast_frames_never_double_step) {
    for (double frame : {1.0 / 60, 1.0 / 144}) {
        FixedStepClock clock;
        for (int i = 0; i < 10000; ++i) {
            ASSERT_LE(clock.advance(frame), 1) << "frame " << frame << " at " << i;
        }
    }
}

// Frame-time statistics: nearest-rank percentiles, the 20/33/50 ms counts,
// load frames kept out, and hitches classified by what took the time.
TEST(P2Timing, frame_stats_count_slow_frames_and_classify_hitches) {
    frameperf::reset();
    auto frame = [](uint64_t n, frameperf::Phase phase, float ms, bool load = false) {
        frameperf::beginFrame(n);
        frameperf::addPhaseTime(phase, ms);
        // A frame's length is measured, so make it take at least ms.
        auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(static_cast<int>(ms * 1000));
        while (std::chrono::steady_clock::now() < until) {
        }
        frameperf::endFrame();
        if (load) {
            frameperf::markLastFrameLoad();
        }
    };
    for (uint64_t i = 1; i <= 20; ++i) {
        frame(i, frameperf::Phase::Render, 1.0f);
    }
    frame(21, frameperf::Phase::Render, 25.0f);
    frame(22, frameperf::Phase::Resource, 60.0f);
    frame(23, frameperf::Phase::Swap, 55.0f);
    frame(24, frameperf::Phase::Sim, 300.0f, true);
    frame(25, frameperf::Phase::Render, 1.0f); // finalizes frame 24

    frameperf::Stats stats = frameperf::windowStats();
    EXPECT_EQ(23, stats.frames);
    EXPECT_EQ(1, stats.loadFrames);
    EXPECT_EQ(3, stats.over20);
    EXPECT_EQ(2, stats.over33);
    EXPECT_EQ(2, stats.over50);
    EXPECT_GE(stats.maxMs, 60.0f);
    EXPECT_LT(stats.maxMs, 300.0f);

    std::string json = frameperf::json(true);
    EXPECT_NE(std::string::npos, json.find("\"resource\":1")) << json;
    EXPECT_NE(std::string::npos, json.find("\"swap\":1")) << json;
    EXPECT_NE(std::string::npos, json.find("\"load\":1")) << json;
    EXPECT_EQ(std::string::npos, frameperf::json(false).find("\"type\":")) << "hitches were consumed";
    frameperf::reset();
}

TEST(P2Timing, percentile_is_nearest_rank) {
    float sorted[100];
    for (int i = 0; i < 100; ++i) {
        sorted[i] = static_cast<float>(i + 1);
    }
    EXPECT_FLOAT_EQ(95.0f, frameperf::percentile(sorted, 100, 0.95f));
    EXPECT_FLOAT_EQ(99.0f, frameperf::percentile(sorted, 100, 0.99f));
    EXPECT_FLOAT_EQ(1.0f, frameperf::percentile(sorted, 100, 0.0f));
    EXPECT_FLOAT_EQ(0.0f, frameperf::percentile(sorted, 0, 0.5f));
}

namespace {

// One second of 8-bit mono PCM at 8 kHz.
ByteBuffer makeWav() {
    const uint32_t samples = 8000;
    ByteBuffer wav;
    auto put = [&wav](const void *data, size_t size) {
        auto bytes = static_cast<const char *>(data);
        wav.insert(wav.end(), bytes, bytes + size);
    };
    auto u32 = [&put](uint32_t v) { put(&v, 4); };
    auto u16 = [&put](uint16_t v) { put(&v, 2); };
    put("RIFF", 4);
    u32(36 + samples);
    put("WAVE", 4);
    put("fmt ", 4);
    u32(16);
    u16(1);    // PCM
    u16(1);    // mono
    u32(8000); // sample rate
    u32(8000); // byte rate
    u16(1);    // block align
    u16(8);    // bits per sample
    put("data", 4);
    u32(samples);
    wav.insert(wav.end(), samples, static_cast<char>(0x80));
    return wav;
}

} // namespace

// Music and ambience decode off the main thread: the first calls report the
// clip as pending, and it arrives whole; a synchronous get then finds it.
TEST(P2Timing, music_clips_decode_in_the_background) {
    using namespace testing;
    resource::MockResources resources;
    resource::MockResources streamResources;
    ByteBuffer wav = makeWav();
    ON_CALL(streamResources, find(_)).WillByDefault(Return(std::nullopt));
    ON_CALL(resources, find(_)).WillByDefault([&wav](const resource::ResourceId &id) -> std::optional<resource::Resource> {
        if (id.resRef.value() == "mus_theme" && id.type == resource::ResType::Wav) {
            return resource::Resource {wav};
        }
        return std::nullopt;
    });
    resource::AudioClips clips(resources, streamResources);

    bool pending = true;
    std::shared_ptr<audio::AudioClip> clip;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        clip = clips.getInBackground("mus_theme", pending);
        if (!pending) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_FALSE(pending);
    ASSERT_TRUE(clip);
    EXPECT_NEAR(1.0f, clip->duration(), 0.01f);
    EXPECT_EQ(clip, clips.get("mus_theme"));

    auto missing = clips.getInBackground("mus_none", pending);
    EXPECT_FALSE(pending);
    EXPECT_FALSE(missing);
}

// A texture asked for in mixed case is loaded once, not on every request.
TEST(P2Timing, texture_cache_hits_whatever_the_case) {
    using namespace testing;
    resource::MockResources resources;
    graphics::GraphicsOptions options;
    int lookups = 0;
    ON_CALL(resources, find(_)).WillByDefault([&lookups](const resource::ResourceId &) -> std::optional<resource::Resource> {
        ++lookups;
        return std::nullopt;
    });
    resource::Textures textures(options, resources);
    textures.get("CM_Baremetal", graphics::TextureUsage::EnvironmentMap);
    int first = lookups;
    textures.get("CM_Baremetal", graphics::TextureUsage::EnvironmentMap);
    textures.get("cm_baremetal", graphics::TextureUsage::EnvironmentMap);
    EXPECT_GT(first, 0);
    EXPECT_EQ(first, lookups);
    renderdiag::reset();
}
