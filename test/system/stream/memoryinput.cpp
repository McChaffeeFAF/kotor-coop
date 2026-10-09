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

#include "../../checkutil.h"

using namespace reone;

TEST(MemoryInputStream, should_read_from_byte_buffer) {
    // given
    auto bytes = ByteBuffer {'H', 'e', 'l', 'l', 'o', ',', ' ', 'w', 'o', 'r', 'l', 'd', '!'};
    auto stream = MemoryInputStream(bytes);
    auto buf = ByteBuffer(16, '\0');
    auto expectedContents = std::string("Hello, world!");

    // when
    stream.seek(0, SeekOrigin::End);
    size_t position1 = stream.position();
    stream.seek(0, SeekOrigin::Begin);
    int readByteResult1 = stream.readByte();
    stream.seek(-1, SeekOrigin::Current);
    size_t position2 = stream.position();
    int readResult = stream.read(&buf[0], 16);
    std::string contents;
    contents.insert(contents.begin(), buf.begin(), buf.begin() + 13);
    int readByteResult2 = stream.readByte();

    // then
    EXPECT_EQ(13ll, position1);
    EXPECT_EQ('H', readByteResult1);
    EXPECT_EQ(0ll, position2);
    EXPECT_EQ(13, readResult);
    EXPECT_EQ(16ll, buf.size());
    EXPECT_EQ(expectedContents, contents) << notEqualMessage(expectedContents, contents);
    EXPECT_EQ(-1, readByteResult2);
}

// Found by fuzzing: a seek outside the buffer left the position past the end,
// and the next read computed `length - position` as a huge unsigned count.
TEST(MemoryInputStream, should_report_end_of_stream_after_seeking_past_the_end) {
    // given
    auto bytes = ByteBuffer {'a', 'b', 'c'};
    auto stream = MemoryInputStream(bytes);
    char buf[8] = {};

    // when
    stream.seek(100, SeekOrigin::Begin);
    int pastEnd = stream.read(buf, 8);
    stream.seek(100, SeekOrigin::Current);
    int pastEndAgain = stream.read(buf, 8);

    // then
    EXPECT_EQ(3ll, stream.position());
    EXPECT_EQ(0, pastEnd);
    EXPECT_EQ(0, pastEndAgain);
    EXPECT_EQ(-1, stream.readByte());
}

TEST(MemoryInputStream, should_clamp_a_seek_before_the_start) {
    // given
    auto bytes = ByteBuffer {'a', 'b', 'c'};
    auto stream = MemoryInputStream(bytes);
    char buf[8] = {};

    // when
    stream.seek(-5, SeekOrigin::Begin);
    int numRead = stream.read(buf, 2);

    // then
    EXPECT_EQ(2, numRead);
    EXPECT_EQ('a', buf[0]);
    EXPECT_EQ('b', buf[1]);
}

TEST(MemoryInputStream, should_ignore_non_positive_read_lengths) {
    // given
    auto bytes = ByteBuffer {'a'};
    auto stream = MemoryInputStream(bytes);
    char buf[1] = {};

    // when
    int zero = stream.read(buf, 0);
    int negative = stream.read(buf, -3);

    // then
    EXPECT_EQ(0, zero);
    EXPECT_EQ(0, negative);
    EXPECT_EQ(0ll, stream.position());
}
