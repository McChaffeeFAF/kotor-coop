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

#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/game.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;

namespace {

class FixedTickGame : public testing::Test {
protected:
    void SetUp() override {
        _engine.init();
        // The engine's own mode: whole fixed ticks from an accumulator.
        _engine.gameOptions().variableStep = false;
        _engine.gameOptions().maxTicksPerFrame = 5;
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
    }

    void frames(int count, float frameTime) {
        for (int i = 0; i < count; ++i) {
            _game->update(frameTime);
        }
    }

    TestEngine _engine;
    StubConsole _console;
    std::unique_ptr<Game> _game;
};

} // namespace

TEST_F(FixedTickGame, tick_count_follows_simulated_time_not_frame_count) {
    frames(60, 1.0f / 60.0f);
    EXPECT_EQ(30u, _game->tickCount());
    EXPECT_EQ(60u, _game->frameCount());

    frames(144, 1.0f / 144.0f);
    EXPECT_EQ(60u, _game->tickCount());
}

TEST_F(FixedTickGame, a_long_frame_runs_at_most_the_clamp) {
    _game->update(2.0f);
    EXPECT_EQ(5u, _game->tickCount());
}

TEST_F(FixedTickGame, closed_gate_runs_only_granted_ticks) {
    _game->setTickGate(true);
    frames(30, 1.0f / 60.0f);
    EXPECT_EQ(0u, _game->tickCount());

    _game->grantTicks(3);
    frames(30, 1.0f / 60.0f);
    EXPECT_EQ(3u, _game->tickCount());
    EXPECT_EQ(0u, _game->tickAllowance());

    // A grant is spent at the tick rate, never faster: four ticks at 144 Hz
    // frames take about 19 frames, and a single long frame still clamps.
    _game->grantTicks(4);
    frames(10, 1.0f / 144.0f);
    EXPECT_EQ(5u, _game->tickCount());
    _game->update(1.0f);
    EXPECT_EQ(7u, _game->tickCount());

    _game->setTickGate(false);
    frames(2, 1.0f / 60.0f);
    EXPECT_EQ(8u, _game->tickCount());
}
