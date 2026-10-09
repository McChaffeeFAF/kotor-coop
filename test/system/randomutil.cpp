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

#include "reone/system/randomutil.h"

#include <set>
#include <vector>

using namespace reone;

namespace {

std::vector<int> rollGameplay(int count) {
    std::vector<int> rolls;
    for (int i = 0; i < count; ++i) {
        rolls.push_back(randomInt(1, 20));
    }
    return rolls;
}

} // namespace

TEST(RandomGenerator, same_seed_gives_the_same_sequence) {
    RandomGenerator a(1234);
    RandomGenerator b(1234);
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(a.next(), b.next());
    }
    EXPECT_EQ(100u, a.draws());
}

TEST(RandomGenerator, sequence_is_pinned_across_platforms) {
    // The mapping is our own code, not the standard library's, so these
    // values hold on every compiler. A change here breaks saved seeds and
    // any recorded determinism baseline.
    RandomGenerator generator(1);
    std::vector<int> rolls;
    for (int i = 0; i < 8; ++i) {
        rolls.push_back(generator.uniformInt(1, 20));
    }
    // Reference values from an independent Python model of the algorithm.
    EXPECT_EQ((std::vector<int> {8, 7, 3, 5, 4, 20, 18, 13}), rolls);
}

TEST(RandomGenerator, uniform_int_covers_the_inclusive_range) {
    RandomGenerator generator(7);
    std::set<int> seen;
    for (int i = 0; i < 2000; ++i) {
        int value = generator.uniformInt(-2, 3);
        ASSERT_GE(value, -2);
        ASSERT_LE(value, 3);
        seen.insert(value);
    }
    EXPECT_EQ(6u, seen.size());
    EXPECT_EQ(5, generator.uniformInt(5, 5));
    EXPECT_EQ(5, generator.uniformInt(5, 4));
}

TEST(RandomGenerator, uniform_float_is_half_open) {
    RandomGenerator generator(9);
    for (int i = 0; i < 2000; ++i) {
        float value = generator.uniformFloat(-1.0f, 1.0f);
        ASSERT_GE(value, -1.0f);
        ASSERT_LT(value, 1.0f);
    }
}

TEST(RandomStreams, cosmetic_draws_do_not_disturb_the_gameplay_stream) {
    setRandomSeed(42);
    auto expected = rollGameplay(20);

    setRandomSeed(42);
    std::vector<int> interleaved;
    for (int i = 0; i < 20; ++i) {
        // A frame-rate-dependent number of cosmetic draws between rolls.
        for (int j = 0; j < i * 3; ++j) {
            cosmeticRandomFloat(0.0f, 1.0f);
            cosmeticRandomInt(0, 100);
        }
        interleaved.push_back(randomInt(1, 20));
    }

    EXPECT_EQ(expected, interleaved);
    EXPECT_EQ(20u, gameplayRandom().draws());
}

TEST(RandomStreams, seeding_one_stream_leaves_the_other_alone) {
    setRandomSeed(5);
    setCosmeticRandomSeed(5);
    randomInt(0, 10);
    uint64_t cosmeticDraws = cosmeticRandom().draws();
    uint32_t cosmeticNext = RandomGenerator(cosmeticRandom()).next();

    setRandomSeed(99);

    EXPECT_EQ(99u, gameplayRandom().seedValue());
    EXPECT_EQ(0u, gameplayRandom().draws());
    EXPECT_EQ(5u, cosmeticRandom().seedValue());
    EXPECT_EQ(cosmeticDraws, cosmeticRandom().draws());
    EXPECT_EQ(cosmeticNext, RandomGenerator(cosmeticRandom()).next());
}

TEST(RandomStreams, gameplay_draws_under_the_presentation_guard_are_counted) {
    uint64_t before = gameplayRandomViolations();
    {
        GameplayRandomForbidden guard;
        cosmeticRandomInt(0, 1);
        EXPECT_EQ(before, gameplayRandomViolations());
        randomInt(0, 1);
        randomFloat(0.0f, 1.0f);
    }
    EXPECT_EQ(before + 2, gameplayRandomViolations());
    randomInt(0, 1);
    EXPECT_EQ(before + 2, gameplayRandomViolations());
}
