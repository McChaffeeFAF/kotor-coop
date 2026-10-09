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

#include "reone/resource/format/lytreader.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stream/memoryinput.h"

using namespace reone;
using namespace reone::resource;

TEST(LytReader, should_read_rooms) {
    auto bytes = std::string("beginlayout\n  roomcount 1\n    Room_A 1.5 2.5 0\ndonelayout\n");
    auto in = MemoryInputStream(bytes);
    auto reader = LytReader();

    reader.load(in);

    ASSERT_EQ(1ll, reader.layout().rooms.size());
    EXPECT_EQ("room_a", reader.layout().rooms[0].name);
    EXPECT_FLOAT_EQ(1.5f, reader.layout().rooms[0].position.x);
}

// Found by fuzzing: a room line with fewer than four tokens indexed past the end
// of the token list (heap-buffer-overflow in appendRoom).
TEST(LytReader, should_reject_a_room_line_with_missing_coordinates) {
    auto bytes = std::string("beginlayout\nroomcount 1\nroom_a 1\n");
    auto in = MemoryInputStream(bytes);
    auto reader = LytReader();

    EXPECT_THROW(reader.load(in), ValidationException);
}

TEST(LytReader, should_reject_a_count_that_is_not_a_number) {
    auto bytes = std::string("beginlayout\nroomcount many\n");
    auto in = MemoryInputStream(bytes);
    auto reader = LytReader();

    EXPECT_THROW(reader.load(in), ValidationException);
}

TEST(LytReader, should_reject_a_count_keyword_without_a_value) {
    auto bytes = std::string("beginlayout\nroomcount\n");
    auto in = MemoryInputStream(bytes);
    auto reader = LytReader();

    EXPECT_THROW(reader.load(in), ValidationException);
}

TEST(LytReader, should_not_size_an_allocation_from_a_room_count) {
    auto bytes = std::string("beginlayout\nroomcount 2147483647\n");
    auto in = MemoryInputStream(bytes);
    auto reader = LytReader();

    EXPECT_NO_THROW(reader.load(in));
}
