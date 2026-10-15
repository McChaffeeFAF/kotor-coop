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

#include "reone/graphics/format/bwmreader.h"
#include "reone/graphics/walkmesh.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/stringbuilder.h"

using namespace reone;
using namespace reone::graphics;

TEST(BwmReader, should_load_pwk_dwk) {
    // given
    auto bwmBytes = StringBuilder()
                        .append("BWM V1.0")
                        .append("\x00\x00\x00\x00", 4) // type
                        .append("\x00\x00\x00\x00", 4) // relative use position 1 (X)
                        .append("\x00\x00\x00\x00", 4) // relative use position 1 (Y)
                        .append("\x00\x00\x00\x00", 4) // relative use position 1 (Z)
                        .append("\x00\x00\x00\x00", 4) // relative use position 2 (X)
                        .append("\x00\x00\x00\x00", 4) // relative use position 2 (Y)
                        .append("\x00\x00\x00\x00", 4) // relative use position 2 (Z)
                        .append("\x00\x00\x00\x00", 4) // absolute use position 1 (X)
                        .append("\x00\x00\x00\x00", 4) // absolute use position 1 (Y)
                        .append("\x00\x00\x00\x00", 4) // absolute use position 1 (Z)
                        .append("\x00\x00\x00\x00", 4) // absolute use position 2 (X)
                        .append("\x00\x00\x00\x00", 4) // absolute use position 2 (Y)
                        .append("\x00\x00\x00\x00", 4) // absolute use position 2 (Z)
                        .append("\x00\x00\x00\x00", 4) // position (X)
                        .append("\x00\x00\x00\x00", 4) // position (Y)
                        .append("\x00\x00\x00\x00", 4) // position (Z)
                        .append("\x00\x00\x00\x00", 4) // vertex count
                        .append("\x00\x00\x00\x00", 4) // offset to vertices
                        .append("\x00\x00\x00\x00", 4) // offset to indices
                        .append("\x00\x00\x00\x00", 4) // offset to materials
                        .append("\x00\x00\x00\x00", 4) // offset to normals
                        .append("\x00\x00\x00\x00", 4) // offset to planar distances
                        .string();
    auto bwm = MemoryInputStream(bwmBytes);
    auto reader = BwmReader(bwm);

    // when
    reader.load();

    // then
    auto walkmesh = reader.walkmesh();
}

TEST(BwmReader, should_load_wok) {
    // given
    auto wokBytes = StringBuilder()
                        .append("BWM V1.0")
                        .append("\x01\x00\x00\x00", 4) // type
                        .append("\x00\x00\x00\x00", 4) // relative use position 1 (X)
                        .append("\x00\x00\x00\x00", 4) // relative use position 1 (Y)
                        .append("\x00\x00\x00\x00", 4) // relative use position 1 (Z)
                        .append("\x00\x00\x00\x00", 4) // relative use position 2 (X)
                        .append("\x00\x00\x00\x00", 4) // relative use position 2 (Y)
                        .append("\x00\x00\x00\x00", 4) // relative use position 2 (Z)
                        .append("\x00\x00\x00\x00", 4) // absolute use position 1 (X)
                        .append("\x00\x00\x00\x00", 4) // absolute use position 1 (Y)
                        .append("\x00\x00\x00\x00", 4) // absolute use position 1 (Z)
                        .append("\x00\x00\x00\x00", 4) // absolute use position 2 (X)
                        .append("\x00\x00\x00\x00", 4) // absolute use position 2 (Y)
                        .append("\x00\x00\x00\x00", 4) // absolute use position 2 (Z)
                        .append("\x00\x00\x00\x00", 4) // position (X)
                        .append("\x00\x00\x00\x00", 4) // position (Y)
                        .append("\x00\x00\x00\x00", 4) // position (Z)
                        .append("\x00\x00\x00\x00", 4) // number of vertices
                        .append("\x00\x00\x00\x00", 4) // offset to vertices
                        .append("\x00\x00\x00\x00", 4) // offset to indices
                        .append("\x00\x00\x00\x00", 4) // offset to materials
                        .append("\x00\x00\x00\x00", 4) // offset to normals
                        .append("\x00\x00\x00\x00", 4) // offset to planar distances
                        .append("\x00\x00\x00\x00", 4) // position
                        .append("\x00\x00\x00\x00", 4) // number of AABBs
                        .append("\x00\x00\x00\x00", 4) // offset to AABBs
                        .append("\x00\x00\x00\x00", 4) // unknown
                        .append("\x00\x00\x00\x00", 4) // number of adjacencies
                        .append("\x00\x00\x00\x00", 4) // offset to adjacencies
                        .append("\x00\x00\x00\x00", 4) // number of edges
                        .append("\x00\x00\x00\x00", 4) // offset to edges
                        .append("\x00\x00\x00\x00", 4) // number of perimeters
                        .append("\x00\x00\x00\x00", 4) // offset to perimeters
                        .string();
    auto wok = MemoryInputStream(wokBytes);
    auto reader = BwmReader(wok);

    // when
    reader.load();

    // then
    auto walkmesh = reader.walkmesh();
}

namespace {

std::string floats(std::initializer_list<float> values) {
    std::string out;
    for (float v : values) {
        out.append(reinterpret_cast<const char *>(&v), 4);
    }
    return out;
}

std::string uints(std::initializer_list<uint32_t> values) {
    std::string out;
    for (uint32_t v : values) {
        out.append(reinterpret_cast<const char *>(&v), 4);
    }
    return out;
}

} // namespace

TEST(BwmReader, a_door_walkmesh_places_its_vertices_by_its_position_offset_and_keeps_its_use_points) {
    // Upstream #356. K1's open-door walkmeshes are authored at the hinge and
    // offset to the jamb (dor_lhr011: -2.79 m).
    const uint32_t header = 8 + 4 + 15 * 4 + 7 * 4;
    const uint32_t offVertices = header;
    const uint32_t offFaces = offVertices + 3 * 12;
    const uint32_t offMaterials = offFaces + 12;
    const uint32_t offNormals = offMaterials + 4;
    const uint32_t offDistances = offNormals + 12;
    auto bytes = StringBuilder()
                     .append("BWM V1.0")
                     .append(uints({0}))                    // PWK/DWK
                     .append(floats({0.0f, -0.85f, 0.0f}))  // relative use 1
                     .append(floats({0.0f, 0.0f, 0.0f}))    // relative use 2
                     .append(floats({-2.79f, -0.8f, 0.0f})) // absolute use 1
                     .append(floats({-2.79f, 0.05f, 0.0f})) // absolute use 2
                     .append(floats({-2.785f, 0.05f, 0.0f})) // position
                     .append(uints({3, offVertices, 1, offFaces, offMaterials, offNormals, offDistances}))
                     .append(floats({0.0f, -0.4f, 0.0f, 0.0f, 0.4f, 0.0f, 0.0f, 0.4f, 3.0f}))
                     .append(uints({0, 1, 2}))
                     .append(uints({7}))
                     .append(floats({1.0f, 0.0f, 0.0f}))
                     .append(floats({0.0f}))
                     .string();
    auto stream = MemoryInputStream(bytes);
    auto reader = BwmReader(stream);
    reader.load();
    auto walkmesh = reader.walkmesh();
    ASSERT_TRUE(walkmesh);
    ASSERT_EQ(3u, walkmesh->vertices.size());
    EXPECT_NEAR(-2.785f, walkmesh->vertices[0].x, 1e-5f);
    EXPECT_NEAR(-0.35f, walkmesh->vertices[0].y, 1e-5f);
    EXPECT_NEAR(-2.785f, walkmesh->position.x, 1e-5f);
    EXPECT_NEAR(-0.85f, walkmesh->relativeUsePositions[0].y, 1e-5f) << "use points carry no offset";
    EXPECT_NEAR(-2.79f, walkmesh->absoluteUsePositions[0].x, 1e-5f);
    EXPECT_TRUE(walkmesh->hasFaceWithin({7}, {-2.7f, 0.0f, 1.0f}, 0.2f, 0.0f, 2.0f));
    EXPECT_FALSE(walkmesh->hasFaceWithin({7}, {0.0f, 0.0f, 1.0f}, 0.2f, 0.0f, 2.0f)) << "not where it was authored";
    EXPECT_FALSE(walkmesh->hasFaceWithin({1}, {-2.7f, 0.0f, 1.0f}, 0.2f, 0.0f, 2.0f)) << "another material";
}

TEST(BwmReader, a_walkmesh_without_faces_keeps_its_header) {
    auto bytes = StringBuilder()
                     .append("BWM V1.0")
                     .append(uints({0}))
                     .append(floats({0.0f, 1.0f, 0.0f}))
                     .append(floats({0.0f, -1.0f, 0.0f}))
                     .append(floats({0.0f, 1.0f, 0.0f}))
                     .append(floats({0.0f, -1.0f, 0.0f}))
                     .append(floats({0.5f, 0.0f, 0.0f}))
                     .append(uints({0, 0, 0, 0, 0, 0, 0}))
                     .string();
    auto stream = MemoryInputStream(bytes);
    auto reader = BwmReader(stream);
    reader.load();
    auto walkmesh = reader.walkmesh();
    ASSERT_TRUE(walkmesh);
    EXPECT_TRUE(walkmesh->faces.empty());
    EXPECT_FLOAT_EQ(1.0f, walkmesh->relativeUsePositions[0].y);
    EXPECT_FLOAT_EQ(0.5f, walkmesh->position.x);
}
