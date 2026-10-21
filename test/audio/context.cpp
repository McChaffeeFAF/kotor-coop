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

#include <AL/alext.h>

#include "reone/audio/context.h"

using namespace reone::audio;

TEST(AudioContext, hrtf_is_off_by_default) {
    auto attributes = contextAttributes(false);
    ASSERT_EQ(3u, attributes.size());
    EXPECT_EQ(ALC_HRTF_SOFT, attributes[0]);
    EXPECT_EQ(ALC_FALSE, attributes[1]);
    EXPECT_EQ(0, attributes[2]); // zero-terminated
}

TEST(AudioContext, hrtf_can_be_asked_for) {
    auto attributes = contextAttributes(true);
    ASSERT_EQ(3u, attributes.size());
    EXPECT_EQ(ALC_TRUE, attributes[1]);
}
