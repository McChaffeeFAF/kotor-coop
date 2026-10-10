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

// Compressed output of the enhanced-texture generator: encode/decode round trips of the DDS
// files within tolerance, on synthetic images only.

#include <gtest/gtest.h>

#include <cmath>

#include "reone/graphics/bcutil.h"
#include "reone/graphics/format/ddsreader.h"
#include "reone/graphics/format/ddswriter.h"
#include "reone/graphics/format/tgawriter.h"
#include "reone/graphics/texture.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/texgen/compress.h"
#include "reone/texgen/generate.h"

using namespace reone;
using namespace reone::graphics;
using namespace reone::texgen;

namespace {

constexpr float kPi = 3.14159265358979f;

// A bumpy tangent-space normal map: a smooth swell plus ripples, z from x and y.
Image bumpyNormals(int w, int h) {
    Image image(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float u = static_cast<float>(x) / w, v = static_cast<float>(y) / h;
            float nx = 0.45f * std::sin(2 * kPi * u * 2) * std::cos(2 * kPi * v) + 0.1f * std::sin(2 * kPi * u * 9);
            float ny = 0.45f * std::cos(2 * kPi * u) * std::sin(2 * kPi * v * 3);
            float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
            float *p = image.at(x, y);
            p[0] = nx * 0.5f + 0.5f;
            p[1] = ny * 0.5f + 0.5f;
            p[2] = nz * 0.5f + 0.5f;
            p[3] = 1.0f;
        }
    }
    return image;
}

// Roughness and a specular hint that vary smoothly, blue 0, as generateMaterialMap makes them.
Image smoothMaterial(int w, int h) {
    Image image(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float u = static_cast<float>(x) / w, v = static_cast<float>(y) / h;
            float *p = image.at(x, y);
            p[0] = 0.55f + 0.3f * std::sin(2 * kPi * u) * std::cos(2 * kPi * v);
            p[1] = 0.15f + 0.1f * u + 0.05f * std::sin(2 * kPi * v * 2);
            p[2] = 0.0f;
            p[3] = 1.0f;
        }
    }
    return image;
}

struct Decoded {
    int width {0};
    int height {0};
    std::vector<std::vector<uint8_t>> levels; // RGBA8 per level
    PixelFormat format {PixelFormat::RGBA8};
    size_t fileSize {0};
};

Decoded decode(const ByteBuffer &file) {
    MemoryInputStream stream(const_cast<ByteBuffer &>(file));
    DdsReader reader(stream, "t", TextureUsage::BumpMap, true);
    reader.load();
    auto texture = reader.texture();
    Decoded out;
    out.width = texture->width();
    out.height = texture->height();
    out.format = texture->pixelFormat();
    out.fileSize = file.size();
    const auto &layer = texture->layers().front();
    auto one = [&](const ByteBuffer &data, int level) {
        out.levels.push_back(decodeBlockCompressed(out.format, std::max(1, out.width >> level), std::max(1, out.height >> level),
                                                   reinterpret_cast<const uint8_t *>(data.data()), data.size()));
    };
    one(*layer.pixels, 0);
    for (size_t i = 0; i < layer.mipMaps.size(); ++i) {
        one(*layer.mipMaps[i], static_cast<int>(i) + 1);
    }
    return out;
}

double psnr(const Image &image, const std::vector<uint8_t> &rgba, int channels) {
    double sum = 0;
    size_t n = 0;
    for (size_t i = 0; i < static_cast<size_t>(image.width) * image.height; ++i) {
        for (int c = 0; c < channels; ++c) {
            double a = std::lround(std::clamp(image.rgba[i * 4 + c], 0.0f, 1.0f) * 255.0f);
            double d = a - rgba[i * 4 + c];
            sum += d * d;
            ++n;
        }
    }
    double mse = sum / n;
    return mse <= 0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

} // namespace

TEST(TexgenCompress, normal_maps_round_trip_through_bc5_with_a_small_error) {
    for (auto [w, h] : {std::pair {64, 64}, std::pair {37, 21}, std::pair {128, 32}}) {
        auto image = bumpyNormals(w, h);
        auto d = decode(encodeNormalDds(image));
        EXPECT_EQ(PixelFormat::BC5, d.format);
        EXPECT_EQ(w, d.width);
        EXPECT_EQ(h, d.height);
        EXPECT_GT(psnr(image, d.levels[0], 2), 40.0) << w << "x" << h;

        // The angle between the stored and the rebuilt normal (z is rebuilt from x and y).
        double worst = 0;
        for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
            float x = d.levels[0][i * 4] / 255.0f * 2 - 1, y = d.levels[0][i * 4 + 1] / 255.0f * 2 - 1;
            float z = std::sqrt(std::max(0.0f, 1 - x * x - y * y));
            float ox = image.rgba[i * 4] * 2 - 1, oy = image.rgba[i * 4 + 1] * 2 - 1, oz = image.rgba[i * 4 + 2] * 2 - 1;
            float dot = std::clamp(x * ox + y * oy + z * oz, -1.0f, 1.0f);
            worst = std::max(worst, std::acos(dot) * 180.0 / kPi);
        }
        EXPECT_LT(worst, 4.0) << "degrees, " << w << "x" << h;
    }
}

TEST(TexgenCompress, a_flat_normal_map_stays_flat) {
    Image flat(16, 16);
    for (size_t i = 0; i < 256; ++i) {
        flat.rgba[i * 4] = flat.rgba[i * 4 + 1] = 0.5f;
        flat.rgba[i * 4 + 2] = 1.0f;
        flat.rgba[i * 4 + 3] = 1.0f;
    }
    auto d = decode(encodeNormalDds(flat));
    for (const auto &level : d.levels) {
        for (size_t i = 0; i < level.size() / 4; ++i) {
            EXPECT_NEAR(127.5, level[i * 4], 1.0);
            EXPECT_NEAR(127.5, level[i * 4 + 1], 1.0);
        }
    }
}

TEST(TexgenCompress, material_maps_round_trip_through_bc7_and_bc1) {
    auto image = smoothMaterial(64, 48);
    auto bc7 = decode(encodeMaterialDds(image, MaterialCodec::BC7));
    EXPECT_EQ(PixelFormat::BC7, bc7.format);
    EXPECT_GT(psnr(image, bc7.levels[0], 3), 42.0);
    auto bc1 = decode(encodeMaterialDds(image, MaterialCodec::BC1));
    EXPECT_EQ(PixelFormat::DXT1, bc1.format);
    EXPECT_GT(psnr(image, bc1.levels[0], 2), 34.0);
    EXPECT_LT(bc1.fileSize, bc7.fileSize);
}

TEST(TexgenCompress, a_detailed_material_map_survives_bc7) {
    // Hard edges (a checker of two roughness values): BC7 keeps them well.
    Image image(32, 32);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            float *p = image.at(x, y);
            bool on = ((x / 4) + (y / 4)) % 2 == 0;
            p[0] = on ? 0.9f : 0.3f;
            p[1] = on ? 0.2f : 0.6f;
            p[2] = 0.0f;
            p[3] = 1.0f;
        }
    }
    auto d = decode(encodeMaterialDds(image));
    EXPECT_GT(psnr(image, d.levels[0], 3), 40.0);
}

TEST(TexgenCompress, the_mip_chain_reaches_one_pixel_and_halves_each_step) {
    auto d = decode(encodeNormalDds(bumpyNormals(37, 21)));
    EXPECT_EQ(fullMipCount(37, 21), static_cast<int>(d.levels.size()));
    auto chain = buildMipChain(bumpyNormals(37, 21), true);
    ASSERT_EQ(d.levels.size(), chain.size());
    EXPECT_EQ(18, chain[1].width);
    EXPECT_EQ(10, chain[1].height);
    EXPECT_EQ(1, chain.back().width);
    EXPECT_EQ(1, chain.back().height);
    for (const auto &level : chain) {
        for (size_t i = 0; i < static_cast<size_t>(level.width) * level.height; ++i) {
            float x = level.rgba[i * 4] * 2 - 1, y = level.rgba[i * 4 + 1] * 2 - 1, z = level.rgba[i * 4 + 2] * 2 - 1;
            EXPECT_NEAR(1.0, std::sqrt(x * x + y * y + z * z), 1e-3) << "normal mips are renormalised";
        }
    }
}

TEST(TexgenCompress, material_mips_are_plain_averages) {
    Image image(2, 2);
    float values[4] = {0.0f, 1.0f, 0.5f, 0.25f};
    for (int i = 0; i < 4; ++i) {
        image.rgba[i * 4] = values[i];
        image.rgba[i * 4 + 3] = 1.0f;
    }
    auto chain = buildMipChain(image, false);
    ASSERT_EQ(2u, chain.size());
    EXPECT_NEAR(0.4375f, chain[1].rgba[0], 1e-6);
}

TEST(TexgenCompress, tiny_and_odd_images_encode) {
    for (auto [w, h] : {std::pair {1, 1}, std::pair {2, 3}, std::pair {5, 1}, std::pair {3, 7}}) {
        auto n = decode(encodeNormalDds(bumpyNormals(w, h)));
        EXPECT_EQ(w, n.width);
        EXPECT_EQ(fullMipCount(w, h), static_cast<int>(n.levels.size()));
        auto m = decode(encodeMaterialDds(smoothMaterial(w, h)));
        EXPECT_EQ(h, m.height);
    }
}

TEST(TexgenCompress, encoding_is_deterministic) {
    auto image = bumpyNormals(48, 48);
    EXPECT_EQ(encodeNormalDds(image), encodeNormalDds(image));
    auto material = smoothMaterial(48, 48);
    EXPECT_EQ(encodeMaterialDds(material), encodeMaterialDds(material));
}

TEST(TexgenCompress, dds_maps_are_much_smaller_than_24_bit_tga) {
    int w = 256, h = 256;
    auto normals = bumpyNormals(w, h);
    auto material = smoothMaterial(w, h);
    size_t tga = TgaWriter(w, h, PixelFormat::RGB8, toBytes(normals, 3)).toBytes().size() +
                 TgaWriter(w, h, PixelFormat::RGB8, toBytes(material, 3)).toBytes().size();
    size_t bc7 = encodeNormalDds(normals).size() + encodeMaterialDds(material, MaterialCodec::BC7).size();
    size_t bc1 = encodeNormalDds(normals).size() + encodeMaterialDds(material, MaterialCodec::BC1).size();
    RecordProperty("tga_bytes", static_cast<int>(tga));
    RecordProperty("bc5_bc7_bytes", static_cast<int>(bc7));
    RecordProperty("bc5_bc1_bytes", static_cast<int>(bc1));
    std::cout << "[ size ] tga " << tga << "  bc5+bc7 " << bc7 << " (" << static_cast<double>(tga) / bc7 << "x)  bc5+bc1 " << bc1
              << " (" << static_cast<double>(tga) / bc1 << "x)\n";
    EXPECT_GT(static_cast<double>(tga) / bc7, 2.0);
    EXPECT_GT(static_cast<double>(tga) / bc1, 2.8);
}
