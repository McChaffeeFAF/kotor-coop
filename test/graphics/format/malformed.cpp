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

// Malformed-input regressions for the texture and walkmesh readers. Every input is
// built from inline bytes; each was first produced by a libFuzzer target.

#include <gtest/gtest.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "reone/graphics/format/bwmreader.h"
#include "reone/graphics/format/tgareader.h"
#include "reone/graphics/format/tpcreader.h"
#include "reone/graphics/walkmesh.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stream/memoryinput.h"

using namespace reone;
using namespace reone::graphics;

namespace {

struct Bytes {
    std::string s;
    Bytes &str(const std::string &v) { return s += v, *this; }
    Bytes &zeros(size_t n) { return s += std::string(n, '\0'), *this; }
    Bytes &u8(uint8_t v) { return s += static_cast<char>(v), *this; }
    Bytes &u16(uint16_t v) { return u8(v & 0xff).u8(v >> 8); }
    Bytes &u32(uint32_t v) { return u16(v & 0xffff).u16(v >> 16); }
    Bytes &f32(float v) {
        uint32_t b;
        std::memcpy(&b, &v, 4);
        return u32(b);
    }
};

std::string tga(uint16_t width, uint16_t height) {
    return Bytes().u8(0).u8(0).u8(3).zeros(9).u16(width).u16(height).u8(8).u8(0).u8(0xff).s;
}

std::string tpc(uint16_t width, uint16_t height, uint8_t mipMaps) {
    return Bytes().u32(0).u32(0).u16(width).u16(height).u8(1).u8(mipMaps).zeros(114).u8(0xff).s;
}

// A walkmesh of the given kind with three vertices and one face whose vertex
// indices are f0..f2, followed by the AABB table (children are 0-based indices).
struct AabbNode {
    int32_t faceIdx;
    uint32_t left;
    uint32_t right;
};

std::string wok(uint32_t numVertices, uint32_t f0, const std::vector<AabbNode> &aabbs) {
    const uint32_t headerSize = 8 + 4 + 15 * 4 + 7 * 4 + 9 * 4;
    const uint32_t offVertices = headerSize;
    const uint32_t offFaces = offVertices + 3 * 12;
    const uint32_t offMaterials = offFaces + 12;
    const uint32_t offNormals = offMaterials + 4;
    const uint32_t offAabb = offNormals + 12;
    Bytes b;
    b.str("BWM V1.0").u32(1).zeros(15 * 4);
    b.u32(numVertices).u32(offVertices).u32(1).u32(offFaces).u32(offMaterials).u32(offNormals).u32(0);
    b.u32(static_cast<uint32_t>(aabbs.size())).u32(offAabb).u32(0).u32(0).u32(0).u32(0).u32(0).u32(0).u32(0);
    for (int i = 0; i < 3; ++i) {
        b.f32(static_cast<float>(i)).f32(0.0f).f32(0.0f);
    }
    b.u32(f0).u32(1).u32(2); // face
    b.u32(0);                // material
    b.f32(0.0f).f32(0.0f).f32(1.0f); // normal
    for (const auto &n : aabbs) {
        b.zeros(24).u32(static_cast<uint32_t>(n.faceIdx)).u32(0).u32(0).u32(n.left).u32(n.right);
    }
    return b.s;
}

} // namespace

TEST(TgaReader, should_reject_a_zero_width) {
    auto bytes = tga(0, 1);
    auto in = MemoryInputStream(bytes);

    EXPECT_THROW(TgaReader(in, "t", TextureUsage::Default).load(), ValidationException);
}

TEST(TgaReader, should_reject_dimensions_that_are_too_large_to_decode) {
    auto bytes = tga(65535, 65535);
    auto in = MemoryInputStream(bytes);

    EXPECT_THROW(TgaReader(in, "t", TextureUsage::Default).load(), ValidationException);
}

TEST(TpcReader, should_reject_a_zero_width) {
    auto bytes = tpc(0, 1, 1);
    auto in = MemoryInputStream(bytes);

    EXPECT_THROW(TpcReader(in, "t", TextureUsage::Default).load(), ValidationException);
}

TEST(TpcReader, should_tolerate_a_mip_map_count_larger_than_the_image_has_levels) {
    auto bytes = tpc(1, 1, 255);
    auto in = MemoryInputStream(bytes);

    EXPECT_NO_THROW(TpcReader(in, "t", TextureUsage::Default).load());
}

TEST(BwmReader, should_load_a_walkmesh_with_one_leaf_aabb) {
    auto bytes = wok(3, 0, {{0, 0, 0}});
    auto in = MemoryInputStream(bytes);
    auto reader = BwmReader(in);

    reader.load();

    ASSERT_TRUE(static_cast<bool>(reader.walkmesh()));
    EXPECT_EQ(3ll, reader.walkmesh()->vertices.size());
}

TEST(BwmReader, should_reject_a_face_that_refers_to_a_missing_vertex) {
    auto bytes = wok(3, 7, {{0, 0, 0}});
    auto in = MemoryInputStream(bytes);

    EXPECT_THROW(BwmReader(in).load(), ValidationException);
}

TEST(BwmReader, should_reject_a_vertex_count_larger_than_the_file) {
    auto bytes = wok(0x7fffffff, 0, {{0, 0, 0}});
    auto in = MemoryInputStream(bytes);

    EXPECT_THROW(BwmReader(in).load(), std::runtime_error);
}

TEST(BwmReader, should_reject_an_aabb_child_outside_the_table) {
    auto bytes = wok(3, 0, {{-1, 5, 6}});
    auto in = MemoryInputStream(bytes);

    EXPECT_THROW(BwmReader(in).load(), ValidationException);
}

// A node that is its own child made a shared_ptr cycle (LeakSanitizer) and would
// have looped forever when the tree was walked.
TEST(BwmReader, should_reject_an_aabb_node_that_is_its_own_child) {
    auto bytes = wok(3, 0, {{-1, 0, 0}});
    auto in = MemoryInputStream(bytes);

    EXPECT_THROW(BwmReader(in).load(), ValidationException);
}

// Nodes the root never reaches can still point at each other; linking them would
// leak a shared_ptr cycle (LeakSanitizer) even though the reachable tree is sound.
TEST(BwmReader, should_not_link_aabb_nodes_that_are_outside_the_tree) {
    auto bytes = wok(3, 0, {{0, 0, 0}, {-1, 2, 2}, {-1, 1, 1}});
    auto in = MemoryInputStream(bytes);
    auto reader = BwmReader(in);

    EXPECT_NO_THROW(reader.load());
}
