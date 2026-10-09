/*
 * Copyright (c) 2025 The reone project contributors
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

#include "reone/graphics/keyframetrack.h"

using namespace reone;
using namespace reone::graphics;

TEST(KeyframeTrack, quat) {
    KeyframeTrack<glm::quat> track;
    glm::quat q0(1.0f, 0.0f, 0.0f, 0.0f);
    glm::quat q1(-1.0f, 0.5f, 0.0f, 0.0f);

    track.add(0.0f, q0);
    track.add(1.0f, q1);

    glm::quat result;
    bool found = track.valueAtTime(0.5f, result);
    ASSERT_TRUE(found);
    ASSERT_TRUE(glm::all(glm::equal(result, glm::slerp(q0, q1, 0.5f))));
}

TEST(KeyframeTrack, float) {
    KeyframeTrack<glm::vec3> track;
    glm::vec3 v0(0.0f);
    glm::vec3 v1(4.0f);

    track.add(0.0f, v0);
    track.add(1.0f, v1);

    glm::vec3 result;
    bool found = track.valueAtTime(0.5f, result);
    ASSERT_TRUE(found);
    ASSERT_TRUE(glm::all(glm::equal(result, glm::mix(v0, v1, 0.5f))));
}

TEST(KeyframeTrack, clamps_both_ends_when_a_track_ends_before_the_clip) {
    KeyframeTrack<float> track;
    track.add(1, 2);
    track.add(2, 4);
    float value;
    ASSERT_TRUE(track.valueAtTime(-1, value));
    EXPECT_FLOAT_EQ(2, value);
    ASSERT_TRUE(track.valueAtTime(5, value));
    EXPECT_FLOAT_EQ(4, value);
    ASSERT_TRUE(track.valueAtTime(1.5f, value));
    EXPECT_FLOAT_EQ(3, value);
}

TEST(KeyframeTrack, bezier_handles_are_value_offsets_and_time_is_normalized) {
    KeyframeTrack<float> scalar;
    // Control polygon (2, 6, 8, 4); nonzero endpoints distinguish offsets
    // from absolute control points. Midpoint is (2 + 18 + 24 + 4)/8 = 6.
    scalar.addBezier(3, 2, 100, 4);
    scalar.addBezier(7, 4, 4, -100);
    float value;
    ASSERT_TRUE(scalar.valueAtTime(5, value));
    EXPECT_FLOAT_EQ(6, value);
    ASSERT_TRUE(scalar.valueAtTime(9, value));
    EXPECT_FLOAT_EQ(4, value);

    KeyframeTrack<glm::vec3> vector;
    vector.addBezier(3, {2, 0, 1}, {100, 100, 100}, {4, 0, 0});
    vector.addBezier(7, {4, 8, 1}, {4, 0, 0}, {-100, -100, -100});
    glm::vec3 position;
    ASSERT_TRUE(vector.valueAtTime(5, position));
    EXPECT_EQ(glm::vec3(6, 4, 1), position);
}
