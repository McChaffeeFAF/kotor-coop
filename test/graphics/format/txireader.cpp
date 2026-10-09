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

#include "reone/graphics/format/txireader.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/stringbuilder.h"

using namespace reone;
using namespace reone::graphics;

TEST(TxiReader, should_load_txi) {
    // given
    auto txiBytes = StringBuilder()
                        .append("blending additive\n")
                        .append("spacingr 0.015625")
                        .string();
    auto txi = MemoryInputStream(txiBytes);
    auto reader = TxiReader();

    // when
    reader.load(txi);

    // then
    auto features = reader.features();
    EXPECT_EQ(static_cast<int>(Texture::Blending::Additive), static_cast<int>(features.blending));
    EXPECT_FLOAT_EQ(features.spacingR, 0.015625f);
}

// Found by fuzzing: a value that is not a number escaped as std::invalid_argument,
// a key with no value read past the token list, and a huge coordinate count was
// handed to vector::reserve.
TEST(TxiReader, should_reject_a_value_that_is_not_a_number) {
    auto txiBytes = std::string("spacingr zzz\n");
    auto txi = MemoryInputStream(txiBytes);
    auto reader = TxiReader();

    EXPECT_THROW(reader.load(txi), ValidationException);
}

TEST(TxiReader, should_reject_a_key_without_a_value) {
    auto txiBytes = std::string("fps\n");
    auto txi = MemoryInputStream(txiBytes);
    auto reader = TxiReader();

    EXPECT_THROW(reader.load(txi), ValidationException);
}

TEST(TxiReader, should_reject_a_coordinate_line_with_too_few_components) {
    auto txiBytes = std::string("upperleftcoords 1\n0.0 1.0\n");
    auto txi = MemoryInputStream(txiBytes);
    auto reader = TxiReader();

    EXPECT_THROW(reader.load(txi), ValidationException);
}

TEST(TxiReader, should_not_size_an_allocation_from_a_coordinate_count) {
    auto upperBytes = std::string("upperleftcoords 2147483647\n");
    auto upper = MemoryInputStream(upperBytes);
    auto lowerBytes = std::string("lowerrightcoords -5\n");
    auto lower = MemoryInputStream(lowerBytes);

    EXPECT_NO_THROW(TxiReader().load(upper));
    EXPECT_NO_THROW(TxiReader().load(lower));
}
