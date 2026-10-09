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

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "reone/system/portablemath.h"

using namespace reone;

TEST(PortableMath, sin_and_cos_match_the_c_library_closely) {
    for (int i = -4000; i <= 4000; ++i) {
        double x = i * 0.0173;
        EXPECT_NEAR(std::sin(x), portable::sin(x), 1e-12) << x;
        EXPECT_NEAR(std::cos(x), portable::cos(x), 1e-12) << x;
    }
}

TEST(PortableMath, atan2_matches_the_c_library_closely_in_every_quadrant) {
    for (int i = -60; i <= 60; ++i) {
        for (int j = -60; j <= 60; ++j) {
            double y = i * 0.37, x = j * 0.29;
            EXPECT_NEAR(std::atan2(y, x), portable::atan2(y, x), 1e-12) << y << "," << x;
        }
    }
}

TEST(PortableMath, axes_and_origin) {
    EXPECT_EQ(0.0, portable::atan2(0.0, 0.0));
    EXPECT_NEAR(portable::kHalfPi, portable::atan2(1.0, 0.0), 1e-15);
    EXPECT_NEAR(-portable::kHalfPi, portable::atan2(-1.0, 0.0), 1e-15);
    EXPECT_NEAR(portable::kPi, portable::atan2(0.0, -1.0), 1e-15);
    EXPECT_EQ(0.0, portable::sin(0.0));
    EXPECT_EQ(1.0, portable::cos(0.0));
}

// Pinned bit patterns: the point of the module is that these never change with
// the platform, compiler or C library. If this fails on one platform only, the
// build is contracting multiply-adds (-ffp-contract) or using x87 arithmetic.
TEST(PortableMath, results_are_bit_identical_everywhere) {
    auto bits = [](double v) {
        uint64_t b;
        std::memcpy(&b, &v, sizeof b);
        return b;
    };
    uint64_t h = 1469598103934665603ull;
    for (int i = -500; i <= 500; ++i) {
        double x = i * 0.0731;
        for (double v : {portable::sin(x), portable::cos(x), portable::atan2(x, 1.0 + i * 0.01)}) {
            h = (h ^ bits(v)) * 1099511628211ull;
        }
    }
    EXPECT_EQ(17126258555563952885ull, h);
}
