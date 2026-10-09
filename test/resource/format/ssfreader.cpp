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

#include "reone/resource/format/ssfreader.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/stringbuilder.h"

using namespace reone;
using namespace reone::resource;

TEST(SsfReader, should_read_ssf) {
    auto bytes = StringBuilder()
                     .append("SSF V1.1")
                     .append("\x0c\x00\x00\x00", 4) // offset to table
                     .append("\x01\x00\x00\x00", 4)
                     .append("\xff\xff\xff\xff", 4)
                     .string();
    auto in = MemoryInputStream(bytes);
    auto reader = SsfReader(in);

    reader.load();

    ASSERT_EQ(2ll, reader.soundSet().size());
    EXPECT_EQ(1, reader.soundSet()[0]);
    EXPECT_EQ(-1, reader.soundSet()[1]);
}

// Found by fuzzing: a table offset past the end made `length - offset` wrap to a
// huge entry count, which was handed to vector::reserve (std::length_error).
TEST(SsfReader, should_reject_a_table_offset_past_the_end_of_the_file) {
    auto bytes = StringBuilder()
                     .append("SSF V1.1")
                     .append("\xff\xff\x00\x00", 4)
                     .string();
    auto in = MemoryInputStream(bytes);
    auto reader = SsfReader(in);

    EXPECT_THROW(reader.load(), ValidationException);
}
