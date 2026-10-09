
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

#include "reone/system/stream/memoryinput.h"
#include "reone/system/textreader.h"

using namespace reone;

TEST(TextReader, should_read_lines_from_byte_buffer) {
    // given
    auto bytes = ByteBuffer {'l', 'i', 'n', 'e', '1', '\r', '\n', 'l', 'i', 'n', 'e', '2', '\n', 'l', 'o', 'n', 'g', 'l', 'i', 'n', 'e'};
    auto stream = MemoryInputStream(bytes);
    auto reader = TextReader(stream);
    std::optional<std::string> line;

    // expect
    line = reader.readLine();
    EXPECT_TRUE(line);
    EXPECT_EQ(*line, std::string("line1"));

    line = reader.readLine();
    EXPECT_TRUE(line);
    EXPECT_EQ(*line, std::string("line2"));

    line = reader.readLine();
    EXPECT_TRUE(line);
    EXPECT_EQ(*line, std::string("longline"));

    line = reader.readLine();
    EXPECT_FALSE(line);
}
// Found by fuzzing: a line of 256 bytes or more with no terminator indexed one
// past the end of the read buffer, and a lone CR swallowed the byte after it.
TEST(TextReader, should_split_a_line_longer_than_the_buffer_without_reading_past_it) {
    // given
    auto bytes = std::string(300, 'x');
    auto stream = MemoryInputStream(bytes);
    auto reader = TextReader(stream);

    // when
    auto first = reader.readLine();
    auto second = reader.readLine();
    auto third = reader.readLine();

    // then
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(256ll, first->size());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(44ll, second->size());
    EXPECT_FALSE(third.has_value());
}

TEST(TextReader, should_treat_a_lone_cr_as_a_line_terminator_without_dropping_the_next_byte) {
    // given
    auto bytes = std::string("ab\rcd\r\nef");
    auto stream = MemoryInputStream(bytes);
    auto reader = TextReader(stream);

    // then
    EXPECT_EQ("ab", reader.readLine().value());
    EXPECT_EQ("cd", reader.readLine().value());
    EXPECT_EQ("ef", reader.readLine().value());
}
