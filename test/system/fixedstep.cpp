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

#include "reone/system/fixedstep.h"

#include <limits>
#include <stdexcept>
#include <vector>

using namespace reone;

namespace {

uint64_t runFrames(FixedStepClock &clock, double frameTime, int frames) {
    uint64_t total = 0;
    for (int i = 0; i < frames; ++i) {
        total += clock.advance(frameTime);
    }
    return total;
}

} // namespace

TEST(FixedStepClock, sixty_hz_frames_yield_one_step_every_other_frame) {
    FixedStepClock clock(30);

    std::vector<int> steps;
    for (int i = 0; i < 6; ++i) {
        steps.push_back(clock.advance(1.0f / 60.0f));
    }

    EXPECT_EQ((std::vector<int> {0, 1, 0, 1, 0, 1}), steps);
    EXPECT_EQ(3u, clock.steps());
}

TEST(FixedStepClock, one_second_of_frames_is_tick_rate_steps_at_any_frame_rate) {
    for (int fps : {15, 24, 30, 60, 75, 120, 144, 240, 1000}) {
        FixedStepClock clock(30);
        uint64_t total = runFrames(clock, 1.0f / static_cast<float>(fps), fps);
        EXPECT_EQ(30u, total) << "fps=" << fps;
        EXPECT_EQ(0u, clock.droppedSteps()) << "fps=" << fps;
    }
}

TEST(FixedStepClock, varying_frame_times_carry_the_remainder) {
    FixedStepClock clock(30);
    // 0.010 + 0.030 + 0.005 + 0.050 + 0.005 = 0.100 s = 3 steps in total.
    std::vector<double> frames {0.010, 0.030, 0.005, 0.050, 0.005};
    std::vector<int> steps;
    for (double frame : frames) {
        steps.push_back(clock.advance(frame));
    }

    EXPECT_EQ((std::vector<int> {0, 1, 0, 1, 1}), steps);
    EXPECT_EQ(3u, clock.steps());
    EXPECT_NEAR(0.0f, clock.alpha(), 1e-4f);
}

TEST(FixedStepClock, alpha_is_the_fraction_of_a_step_left_over) {
    FixedStepClock clock(10);

    EXPECT_EQ(0, clock.advance(0.025));
    EXPECT_NEAR(0.25f, clock.alpha(), 1e-5f);
    EXPECT_EQ(1, clock.advance(0.1));
    EXPECT_NEAR(0.25f, clock.alpha(), 1e-5f);
}

TEST(FixedStepClock, long_frame_is_clamped_and_the_rest_dropped) {
    FixedStepClock clock(30, 5);

    // A two-second stall is 60 steps overdue.
    EXPECT_EQ(5, clock.advance(2.0));
    EXPECT_EQ(55u, clock.droppedSteps());
    // Nothing is carried: the next ordinary frame runs as usual.
    EXPECT_EQ(0, clock.advance(1.0 / 60.0));
    EXPECT_EQ(1, clock.advance(1.0 / 60.0));
    EXPECT_EQ(6u, clock.steps());
}

TEST(FixedStepClock, per_call_limit_overrides_the_default_clamp) {
    FixedStepClock clock(30, 5);

    EXPECT_EQ(8, clock.advance(8.0 / 30.0, 8));
    EXPECT_EQ(0u, clock.droppedSteps());
    EXPECT_EQ(5, clock.advance(8.0 / 30.0));
    EXPECT_EQ(3u, clock.droppedSteps());
}

TEST(FixedStepClock, game_speed_scales_the_number_of_steps_not_their_length) {
    FixedStepClock clock(30);
    float step = clock.stepSeconds();
    uint64_t total = 0;
    for (int i = 0; i < 60; ++i) {
        total += clock.advance(4.0 * (1.0 / 60.0), 20);
    }

    EXPECT_EQ(120u, total);
    EXPECT_FLOAT_EQ(step, clock.stepSeconds());
}

TEST(FixedStepClock, bad_frame_times_count_as_zero) {
    FixedStepClock clock(30);

    EXPECT_EQ(0, clock.advance(-1.0));
    EXPECT_EQ(0, clock.advance(std::numeric_limits<double>::quiet_NaN()));
    EXPECT_EQ(0, clock.advance(std::numeric_limits<double>::infinity()));
    EXPECT_EQ(0u, clock.steps());
}

TEST(FixedStepClock, reset_and_rate_change_discard_the_fraction) {
    FixedStepClock clock(30);
    clock.advance(0.02);
    clock.reset();
    EXPECT_EQ(0, clock.advance(0.02));

    clock.setTickRate(60);
    EXPECT_FLOAT_EQ(1.0f / 60.0f, clock.stepSeconds());
    EXPECT_EQ(1, clock.advance(1.0 / 60.0));
    EXPECT_THROW(clock.setTickRate(0), std::invalid_argument);
}
