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

// Malformed-input regressions for the binary and text resource readers. Every
// input is built from inline bytes; each was first produced by a libFuzzer
// target. A count read from the file must never size an allocation or a loop.

#include <gtest/gtest.h>

#include <cstring>
#include <stdexcept>
#include <string>

#include "reone/resource/format/2dareader.h"
#include "reone/resource/format/bifreader.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/format/keyreader.h"
#include "reone/resource/format/rimreader.h"
#include "reone/resource/format/visreader.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stream/memoryinput.h"

using namespace reone;
using namespace reone::resource;

namespace {

struct Bytes {
    std::string s;
    Bytes &str(const std::string &v) { return s += v, *this; }
    Bytes &zeros(size_t n) { return s += std::string(n, '\0'), *this; }
    Bytes &u8(uint8_t v) { return s += static_cast<char>(v), *this; }
    Bytes &u16(uint16_t v) { return u8(v & 0xff).u8(v >> 8); }
    Bytes &u32(uint32_t v) { return u16(v & 0xffff).u16(v >> 16); }
};

} // namespace

TEST(KeyReader, should_reject_counts_larger_than_the_file) {
    auto bytes = Bytes().str("KEY V1  ").u32(0x7fffffff).u32(0x7fffffff).u32(64).u32(64).zeros(40).s;
    auto in = MemoryInputStream(bytes);

    EXPECT_THROW(KeyReader(in).load(), std::runtime_error);
}

TEST(BifReader, should_reject_a_resource_count_larger_than_the_file) {
    auto bytes = Bytes().str("BIFFV1  ").u32(0x7fffffff).u32(0).u32(20).s;
    auto in = MemoryInputStream(bytes);

    EXPECT_THROW(BifReader(in).load(), std::runtime_error);
}

TEST(BifReader, should_reject_a_negative_resource_count) {
    auto bytes = Bytes().str("BIFFV1  ").u32(0xffffffff).u32(0).u32(20).s;
    auto in = MemoryInputStream(bytes);

    EXPECT_THROW(BifReader(in).load(), std::runtime_error);
}

TEST(RimReader, should_reject_a_resource_count_larger_than_the_file) {
    auto bytes = Bytes().str("RIM V1.0").u32(0).u32(0x7fffffff).u32(120).zeros(100).s;
    auto in = MemoryInputStream(bytes);

    EXPECT_THROW(RimReader(in).load(), std::runtime_error);
}

TEST(TwoDAReader, should_reject_a_row_count_larger_than_the_file) {
    auto bytes = Bytes().str("2DA V2.b").u8(0x0a).str("a\t").u8(0).u32(0x7fffffff).s;
    auto in = MemoryInputStream(bytes);

    EXPECT_THROW(TwoDAReader(in).load(), std::runtime_error);
}

// A GFF with one struct whose only field is a list that contains that same
// struct. Reading it followed the list forever (stack exhaustion).
TEST(GffReader, should_reject_a_struct_that_contains_itself) {
    auto bytes = Bytes()
                     .str("GFF V3.2")
                     .u32(56).u32(1)   // structs
                     .u32(68).u32(1)   // fields
                     .u32(80).u32(1)   // labels
                     .u32(104).u32(0)  // field data
                     .u32(104).u32(0)  // field indices
                     .u32(96).u32(8)   // list indices
                     .u32(0xffffffff).u32(0).u32(1) // struct 0: one field, index 0
                     .u32(15).u32(0).u32(0)         // field 0: list at offset 0
                     .str(std::string("Self") + std::string(12, '\0'))
                     .u32(1).u32(0)                 // list of one struct: struct 0
                     .s;
    auto in = MemoryInputStream(bytes);

    EXPECT_THROW(GffReader(in).load(), ValidationException);
}

TEST(GffReader, should_reject_a_label_index_that_overflows_the_label_offset) {
    auto bytes = Bytes()
                     .str("GFF V3.2")
                     .u32(56).u32(1)
                     .u32(68).u32(1)
                     .u32(80).u32(1)
                     .u32(96).u32(0)
                     .u32(96).u32(0)
                     .u32(96).u32(0)
                     .u32(0xffffffff).u32(0).u32(1)
                     .u32(5).u32(0x7fffffff).u32(1) // INT field with a huge label index
                     .str(std::string("Num") + std::string(13, '\0'))
                     .s;
    auto in = MemoryInputStream(bytes);

    EXPECT_THROW(GffReader(in).load(), std::runtime_error);
}

// A line of 256 bytes or more without a terminator made the reader index one past
// its buffer (heap-buffer-overflow).
TEST(VisReader, should_read_a_long_line_without_a_terminator) {
    auto bytes = std::string(300, 'a');
    auto in = MemoryInputStream(bytes);
    auto reader = VisReader();

    EXPECT_NO_THROW(reader.load(in));
}
