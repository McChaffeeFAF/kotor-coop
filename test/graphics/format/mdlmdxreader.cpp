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

#include <cstring>

#include "reone/graphics/format/mdlmdxreader.h"
#include "reone/graphics/model.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/stringbuilder.h"

#include "../../fixtures/graphics.h"

using namespace reone;
using namespace reone::graphics;

// A one-node model; nameIndex is the root node's index into the name table.
static std::string minimalMdl(const char *nameIndex, const char *childOffset = "\x00\x00\x00\x00", const char *childCount = "\x00\x00\x00\x00") {

    return StringBuilder()
                        // File Header (0)
                        .append("\x00\x00\x00\x00", 4) // unknown
                        .append("\x2e\x01\x00\x00", 4) // MDL size
                        .append("\x00\x00\x00\x00", 4) // MDX size
                        // Geometry Header (12)
                        .append("\x00\x00\x00\x00", 4)                                                                                    // model function pointer 1
                        .append("\x00\x00\x00\x00", 4)                                                                                    // model function pointer 2
                        .append("some_model\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 32) // model name
                        .append("\xd2\x00\x00\x00", 4)                                                                                    // offset to root node
                        .append("\x01\x00\x00\x00", 4)                                                                                    // number of nodes
                        .append('\x00', 6 * 4)                                                                                            // unknown
                        .append("\x00\x00\x00\x00", 4)                                                                                    // reference counter
                        .append("\x00\x00\x00\x00", 4)                                                                                    // model type
                        // Model Header (92)
                        .append("\x00", 1)                                                                                                                  // classification
                        .append("\x00", 1)                                                                                                                  // subclassification
                        .append("\x00", 1)                                                                                                                  // unknown
                        .append("\x00", 1)                                                                                                                  // affected by fog
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // number of child models
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // offset to animations
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // number of animations
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // number of animations
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // supermodel reference
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // bounding box (X min)
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // bounding box (Y min)
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // bounding box (Z min)
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // bounding box (X max)
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // bounding box (Y max)
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // bounding box (Z max)
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // radius
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // animation scale
                        .append("NULL\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 32) // supermodel name
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // offset to animation root node
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // unknown
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // MDX size
                        .append("\x00\x00\x00\x00", 4)                                                                                                      // offset to MDX
                        .append("\xc4\x00\x00\x00", 4)                                                                                                      // offset to names
                        .append("\x01\x00\x00\x00", 4)                                                                                                      // number of names
                        .append("\x01\x00\x00\x00", 4)                                                                                                      // number of names
                        // Name Offsets (208)
                        .append("\xc8\x00\x00\x00", 4)
                        // Names (212)
                        .append("root_node\x00", 10)
                        // Root Node (222)
                        .append("\x00\x00", 2)         // flags
                        .append("\x00\x00", 2)         // node number
                        .append(nameIndex, 2)          // name index
                        .append("\x00\x00", 2)         // padding
                        .append("\x00\x00\x00\x00", 4) // offset to root node
                        .append("\x00\x00\x00\x00", 4) // offset to parent node
                        .append("\x00\x00\x00\x00", 4) // position (X)
                        .append("\x00\x00\x00\x00", 4) // position (Y)
                        .append("\x00\x00\x00\x00", 4) // position (Z)
                        .append("\x00\x00\x00\x00", 4) // orientation (W)
                        .append("\x00\x00\x00\x00", 4) // orientation (X)
                        .append("\x00\x00\x00\x00", 4) // orientation (Y)
                        .append("\x00\x00\x00\x00", 4) // orientation (Z)
                        .append(childOffset, 4)        // offset to children
                        .append(childCount, 4)         // number of children
                        .append(childCount, 4)         // number of children
                        .append("\x00\x00\x00\x00", 4) // offset to controller keys
                        .append("\x00\x00\x00\x00", 4) // number of controller keys
                        .append("\x00\x00\x00\x00", 4) // number of controller keys
                        .append("\x00\x00\x00\x00", 4) // offset to controller values
                        .append("\x00\x00\x00\x00", 4) // number of controller values
                        .append("\x00\x00\x00\x00", 4) // number of controller values
                        .string();
}

TEST(MdlReader, should_load_mdl) {
    // given
    auto mdlBytes = minimalMdl("\x00\x00");
    auto mdl = MemoryInputStream(mdlBytes);
    auto mdxBytes = StringBuilder().string();
    auto mdx = MemoryInputStream(mdxBytes);
    Statistic statistic;
    auto reader = MdlMdxReader(mdl, mdx, statistic);

    // when
    reader.load();

    // then
    auto model = reader.model();
    EXPECT_TRUE(static_cast<bool>(model));
    EXPECT_EQ(std::string("some_model"), model->name());
    auto rootNode = model->rootNode();
    EXPECT_EQ(std::string("root_node"), rootNode->name());
}

TEST(MdlReader, preserves_bezier_vector_and_float_rows) {
    // Synthetic one-node MDL. All offsets below are file offsets; stored MDL
    // offsets exclude its 12-byte file header. No shipped asset bytes.
    std::string bytes(306 + 32 + 28 * 4, '\0');
    auto word = [&](size_t offset, uint32_t value, int width = 4) {
        for (int i = 0; i < width; ++i) bytes[offset + i] = (value >> (8 * i)) & 0xff;
    };
    auto number = [&](size_t offset, float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        word(offset, bits);
    };
    word(4, bytes.size() - 12);
    bytes.replace(20, 10, "bezier_cam");
    word(52, 210); // root
    word(56, 1);
    number(144, 1); // animation scale
    word(196, 196); // name array
    word(200, 1);
    word(204, 1);
    word(208, 200);
    bytes.replace(212, 9, "root_node");
    number(250, 1); // root quaternion W
    word(278, 294); // keys
    word(282, 2);
    word(286, 2);
    word(290, 326); // data
    word(294, 28);
    word(298, 28);
    // Root node ends at 302 (four bytes of unused padding before keys).
    word(306, ControllerTypes::position);
    word(312, 2, 2); // two rows
    word(314, 0, 2); // time index
    word(316, 2, 2); // data index
    word(318, 19, 1); // vec3 + Bezier flag
    word(322, ControllerTypes::scale);
    word(328, 2, 2);
    word(330, 20, 2);
    word(332, 22, 2);
    word(334, 17, 1); // float + Bezier flag
    const float data[] {3, 7, 2, 0, 1, 100, 100, 100, 4, 0, 0,
                             4, 8, 1, 4, 0, 0, -100, -100, -100,
                           3, 7, 2, 100, 4, 4, 4, -100};
    for (size_t i = 0; i < std::size(data); ++i) number(338 + 4 * i, data[i]);
    MemoryInputStream mdl(bytes);
    std::string empty;
    MemoryInputStream mdx(empty);
    Statistic statistic;
    MdlMdxReader reader(mdl, mdx, statistic);
    reader.load();
    const auto root = reader.model()->rootNode();
    glm::vec3 position;
    ASSERT_TRUE(root->vectorTracks().at(ControllerTypes::position).valueAtTime(5, position));
    EXPECT_EQ(glm::vec3(6, 4, 1), position);
    float scale;
    ASSERT_TRUE(root->floatTracks().at(ControllerTypes::scale).valueAtTime(5, scale));
    EXPECT_FLOAT_EQ(6, scale);
}

// Found by fuzzing: a node whose name index is outside the name table indexed
// the table out of range (UBSan reference binding to null in readNodes).
TEST(MdlReader, should_reject_a_node_name_index_outside_the_name_table) {
    // given
    auto mdlBytes = minimalMdl("\x05\x00");
    auto mdl = MemoryInputStream(mdlBytes);
    auto mdxBytes = StringBuilder().string();
    auto mdx = MemoryInputStream(mdxBytes);
    Statistic statistic;
    auto reader = MdlMdxReader(mdl, mdx, statistic);

    // then
    EXPECT_THROW(reader.load(), ValidationException);
}

// Found by fuzzing: child offsets that loop back recursed until the stack was
// exhausted (AddressSanitizer stack-overflow in readNodes).
TEST(MdlReader, should_reject_a_node_that_lists_itself_as_a_child) {
    // given
    auto base = minimalMdl("\x00\x00");
    uint32_t arrayOffset = static_cast<uint32_t>(base.size() - 12); // the child array follows the model
    char offset[4];
    std::memcpy(offset, &arrayOffset, 4);
    auto mdlBytes = minimalMdl("\x00\x00", offset, "\x01\x00\x00\x00") + std::string("\xd2\x00\x00\x00", 4);
    auto mdl = MemoryInputStream(mdlBytes);
    auto mdxBytes = StringBuilder().string();
    auto mdx = MemoryInputStream(mdxBytes);
    Statistic statistic;
    auto reader = MdlMdxReader(mdl, mdx, statistic);

    // then
    EXPECT_THROW(reader.load(), ValidationException);
}
