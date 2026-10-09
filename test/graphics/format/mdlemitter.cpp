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

// Visual census tags: VC_E_layout VC_E_renderorder VC_E_p2p VC_E_spawntype
// VC_E_depthtexture VC_E_blend_punch (spec: design note v_vfx_emitters, section 5)

#include <gtest/gtest.h>

#include "reone/graphics/format/mdlmdxreader.h"
#include "reone/system/binaryreader.h"
#include "reone/system/stream/memoryinput.h"

#include "../../fixtures/graphics.h"

using namespace reone;
using namespace reone::graphics;

namespace {

void putFloat(std::string &out, float value) {
    char bytes[4];
    std::memcpy(bytes, &value, 4);
    out.append(bytes, 4);
}

void putU32(std::string &out, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
    }
}

void putString(std::string &out, const std::string &value, size_t size) {
    std::string padded(value);
    padded.resize(size, '\0');
    out.append(padded);
}

struct HeaderFields {
    uint32_t spawnType {0};
    std::string update {"fountain"};
    std::string blend {"normal"};
    uint16_t renderOrder {0};
    uint8_t frameBlending {0};
    std::string depthTexture {"NULL"};
    uint32_t flags {0};
};

/** The 224-byte emitter sub-header in the public layout. */
std::string emitterHeader(const HeaderFields &f) {
    std::string out;
    putFloat(out, 0.5f); // dead space
    putFloat(out, 2.0f); // blast radius
    putFloat(out, 3.0f); // blast length
    putU32(out, 4);      // branch count
    putFloat(out, 0.25f);
    putU32(out, 4); // x grid
    putU32(out, 2); // y grid
    putU32(out, f.spawnType);
    putString(out, f.update, 32);
    putString(out, "Normal", 32);
    putString(out, f.blend, 32);
    putString(out, "fx_spark", 32);
    putString(out, "chunkmdl", 16);
    putU32(out, 1); // two-sided
    putU32(out, 1); // loop
    out.push_back(static_cast<char>(f.renderOrder & 0xff));
    out.push_back(static_cast<char>(f.renderOrder >> 8));
    out.push_back(static_cast<char>(f.frameBlending));
    putString(out, f.depthTexture, 32);
    out.push_back('\0'); // padding
    putU32(out, f.flags);
    return out;
}

std::shared_ptr<ModelNode::Emitter> parse(std::string bytes) {
    MemoryInputStream stream(bytes);
    std::string none;
    MemoryInputStream mdx(none);
    Statistic statistic;
    MdlMdxReader reader(stream, mdx, statistic);
    return reader.readEmitterHeader();
}

} // namespace

TEST(MdlEmitterHeader, VC_E_layout_is_224_bytes_with_the_flags_word_at_220) {
    HeaderFields f;
    f.flags = 0x1 | 0x8; // p2p, tinted
    auto bytes = emitterHeader(f);
    ASSERT_EQ(224u, bytes.size());

    MemoryInputStream stream(bytes);
    std::string none;
    MemoryInputStream mdx(none);
    Statistic statistic;
    MdlMdxReader reader(stream, mdx, statistic);
    auto emitter = reader.readEmitterHeader();

    // The whole header is consumed and nothing more.
    EXPECT_EQ(224u, stream.position());
    EXPECT_EQ(0x9u, emitter->flags);
    EXPECT_TRUE(emitter->p2p);
    EXPECT_TRUE(emitter->tinted);
    EXPECT_FLOAT_EQ(0.5f, emitter->deadSpace);
    EXPECT_FLOAT_EQ(2.0f, emitter->blastRadius);
    EXPECT_FLOAT_EQ(3.0f, emitter->blastLength);
    EXPECT_EQ(4, emitter->branchCount);
    EXPECT_EQ(glm::ivec2(4, 2), emitter->gridSize);
    EXPECT_EQ("fx_spark", emitter->textureName);
    EXPECT_EQ("chunkmdl", emitter->chunkName);
    EXPECT_TRUE(emitter->twosided);
    EXPECT_TRUE(emitter->loop);
}

TEST(MdlEmitterHeader, VC_E_p2p_p2p_sel_and_tinted_bits_come_back_as_written) {
    HeaderFields f;
    f.flags = 0x2; // p2p_sel only: the common K1 case
    auto emitter = parse(emitterHeader(f));
    EXPECT_FALSE(emitter->p2p);
    EXPECT_TRUE(emitter->p2pSel);
    EXPECT_FALSE(emitter->tinted);

    f.flags = 0x1 | 0x2 | 0x8;
    emitter = parse(emitterHeader(f));
    EXPECT_TRUE(emitter->p2p);
    EXPECT_TRUE(emitter->p2pSel);
    EXPECT_TRUE(emitter->tinted);
}

TEST(MdlEmitterHeader, every_documented_flag_bit_maps_to_its_field) {
    // VC_E_flag_affected_by_wind VC_E_flag_bounce VC_E_flag_random VC_E_flag_inherit
    // VC_E_flag_inherit_vel VC_E_flag_inherit_local VC_E_flag_splat VC_E_flag_inherit_part
    // VC_E_flag_flag13
    struct Case {
        uint32_t bit;
        bool ModelNode::Emitter::*field;
    };
    const Case cases[] {
        {0x1, &ModelNode::Emitter::p2p},
        {0x2, &ModelNode::Emitter::p2pSel},
        {0x4, &ModelNode::Emitter::affectedByWind},
        {0x8, &ModelNode::Emitter::tinted},
        {0x10, &ModelNode::Emitter::bounce},
        {0x20, &ModelNode::Emitter::random},
        {0x40, &ModelNode::Emitter::inherit},
        {0x80, &ModelNode::Emitter::inheritVelocity},
        {0x100, &ModelNode::Emitter::inheritLocal},
        {0x200, &ModelNode::Emitter::splat},
        {0x400, &ModelNode::Emitter::inheritParticle},
        {0x800, &ModelNode::Emitter::depthTextureFlag},
        {0x1000, &ModelNode::Emitter::flag13}};
    for (const auto &c : cases) {
        HeaderFields f;
        f.flags = c.bit;
        auto emitter = parse(emitterHeader(f));
        for (const auto &other : cases) {
            EXPECT_EQ(other.bit == c.bit, (*emitter).*(other.field)) << "flags 0x" << std::hex << c.bit << " field 0x" << other.bit;
        }
    }
}

TEST(MdlEmitterHeader, VC_E_renderorder_is_a_uint16_and_frame_blending_a_uint8) {
    HeaderFields f;
    f.renderOrder = 0x0105;
    f.frameBlending = 1;
    f.flags = 0x20;
    auto emitter = parse(emitterHeader(f));
    EXPECT_EQ(0x0105, emitter->renderOrder);
    EXPECT_EQ(1, emitter->frameBlending);
    // Read as two uint32 (the old layout) these bytes would have swallowed the
    // depth texture name and put the flags five bytes late.
    EXPECT_TRUE(emitter->random);
    EXPECT_EQ(0x20u, emitter->flags);
}

TEST(MdlEmitterHeader, VC_E_spawntype_1_is_a_trail) {
    HeaderFields f;
    f.spawnType = 1;
    EXPECT_EQ(ModelNode::Emitter::SpawnType::Trail, parse(emitterHeader(f))->spawnType);
    f.spawnType = 0;
    EXPECT_EQ(ModelNode::Emitter::SpawnType::Normal, parse(emitterHeader(f))->spawnType);
}

TEST(MdlEmitterHeader, VC_E_depthtexture_name_is_read_at_its_documented_offset) {
    HeaderFields f;
    f.depthTexture = "FX_Depth";
    f.flags = 0x10;
    auto emitter = parse(emitterHeader(f));
    EXPECT_EQ("fx_depth", emitter->depthTexture);
    EXPECT_TRUE(emitter->bounce);

    f.depthTexture = "NULL";
    EXPECT_TRUE(parse(emitterHeader(f))->depthTexture.empty());
}

TEST(MdlEmitterHeader, VC_E_blend_punch_accepts_both_spellings) {
    HeaderFields f;
    f.blend = "punch-through";
    EXPECT_EQ(ModelNode::Emitter::BlendMode::PunchThrough, parse(emitterHeader(f))->blendMode);
    f.blend = "PunchThrough";
    EXPECT_EQ(ModelNode::Emitter::BlendMode::PunchThrough, parse(emitterHeader(f))->blendMode);
    f.blend = "lighten";
    EXPECT_EQ(ModelNode::Emitter::BlendMode::Lighten, parse(emitterHeader(f))->blendMode);
}
