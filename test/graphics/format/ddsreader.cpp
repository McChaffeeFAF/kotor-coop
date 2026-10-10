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

// The DDS reader and writer, on synthetic files only: every block format, mip chains, the
// legacy FourCC headers, and truncated or corrupt data.

#include <gtest/gtest.h>

#include <random>

#include "reone/graphics/bcutil.h"
#include "reone/graphics/format/ddsreader.h"
#include "reone/graphics/format/ddswriter.h"
#include "reone/graphics/texture.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stream/memoryinput.h"

using namespace reone;
using namespace reone::graphics;

namespace {

ByteBuffer randomBytes(size_t n, uint32_t seed) {
    std::mt19937 rng(seed);
    ByteBuffer out(n);
    for (auto &b : out) {
        b = static_cast<char>(rng() & 0xff);
    }
    return out;
}

std::vector<ByteBuffer> randomLevels(PixelFormat format, int w, int h, int count, uint32_t seed) {
    std::vector<ByteBuffer> levels;
    for (int i = 0; i < count; ++i) {
        levels.push_back(randomBytes(compressedLevelSize(format, std::max(1, w >> i), std::max(1, h >> i)), seed + i));
    }
    return levels;
}

std::shared_ptr<Texture> read(const ByteBuffer &file, bool nativeBC7 = true) {
    MemoryInputStream stream(const_cast<ByteBuffer &>(file));
    DdsReader reader(stream, "test", TextureUsage::BumpMap, nativeBC7);
    reader.load();
    return reader.texture();
}

void put32(std::string &s, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
    }
}

// A legacy-header file: FourCC, no DX10 extension.
ByteBuffer legacy(const char cc[4], int w, int h, int mips, size_t dataBytes, uint32_t caps2 = 0) {
    std::string s = "DDS ";
    put32(s, 124);
    put32(s, 0x1 | 0x2 | 0x4 | 0x1000 | (mips > 1 ? 0x20000 : 0));
    put32(s, h);
    put32(s, w);
    put32(s, 0);
    put32(s, 0);
    put32(s, mips);
    for (int i = 0; i < 11; ++i) {
        put32(s, 0);
    }
    put32(s, 32);
    put32(s, 0x4);
    s.append(cc, 4);
    for (int i = 0; i < 5; ++i) {
        put32(s, 0);
    }
    put32(s, 0x1000);
    put32(s, caps2);
    put32(s, 0);
    put32(s, 0);
    put32(s, 0);
    s.append(dataBytes, 'x');
    return ByteBuffer(s.begin(), s.end());
}

struct FormatCase {
    PixelFormat format;
    const char *name;
};

} // namespace

class DdsFormats : public ::testing::TestWithParam<FormatCase> {};

TEST_P(DdsFormats, a_written_file_reads_back_with_every_level) {
    auto format = GetParam().format;
    for (auto [w, h] : {std::pair {64, 64}, std::pair {37, 21}, std::pair {16, 4}, std::pair {1, 1}, std::pair {4, 1}}) {
        int count = fullMipCount(w, h);
        auto levels = randomLevels(format, w, h, count, 7);
        auto texture = read(writeDds(format, w, h, levels));
        ASSERT_TRUE(texture) << w << "x" << h;
        EXPECT_EQ(format, texture->pixelFormat());
        EXPECT_EQ(w, texture->width());
        EXPECT_EQ(h, texture->height());
        ASSERT_EQ(1u, texture->layers().size());
        const auto &layer = texture->layers().front();
        EXPECT_EQ(levels[0], *layer.pixels) << "level 0 is untouched";
        ASSERT_EQ(static_cast<size_t>(count - 1), layer.mipMaps.size()) << w << "x" << h;
        for (int i = 1; i < count; ++i) {
            EXPECT_EQ(levels[i], *layer.mipMaps[i - 1]) << "level " << i;
        }
    }
}

TEST_P(DdsFormats, a_partial_mip_chain_and_no_chain_are_kept_as_they_are) {
    auto format = GetParam().format;
    auto some = read(writeDds(format, 32, 32, randomLevels(format, 32, 32, 3, 1)));
    EXPECT_EQ(2u, some->layers().front().mipMaps.size());
    auto none = read(writeDds(format, 32, 32, randomLevels(format, 32, 32, 1, 1)));
    EXPECT_TRUE(none->layers().front().mipMaps.empty());
}

TEST_P(DdsFormats, every_truncation_is_rejected_cleanly) {
    auto format = GetParam().format;
    auto file = writeDds(format, 16, 8, randomLevels(format, 16, 8, fullMipCount(16, 8), 3));
    ASSERT_NO_THROW(read(file));
    for (size_t n = 0; n < file.size(); ++n) {
        ByteBuffer cut(file.begin(), file.begin() + n);
        EXPECT_THROW(read(cut), ValidationException) << "prefix of " << n << " bytes";
    }
}

TEST_P(DdsFormats, corrupt_bytes_never_crash_the_reader) {
    auto format = GetParam().format;
    auto file = writeDds(format, 16, 8, randomLevels(format, 16, 8, fullMipCount(16, 8), 3));
    std::mt19937 rng(11);
    for (int round = 0; round < 400; ++round) {
        auto bad = file;
        int flips = 1 + rng() % 4;
        for (int i = 0; i < flips; ++i) {
            bad[rng() % std::min<size_t>(bad.size(), 160)] = static_cast<char>(rng());
        }
        try {
            auto texture = read(bad);
            EXPECT_TRUE(texture);
        } catch (const std::exception &) {
            // Rejected: fine.
        }
    }
}

INSTANTIATE_TEST_SUITE_P(DdsReader, DdsFormats,
                         ::testing::Values(FormatCase {PixelFormat::DXT1, "bc1"}, FormatCase {PixelFormat::DXT5, "bc3"},
                                           FormatCase {PixelFormat::BC5, "bc5"}, FormatCase {PixelFormat::BC7, "bc7"}),
                         [](const auto &info) { return std::string(info.param.name); });

TEST(DdsReader, bc7_is_decoded_to_rgba8_when_the_gpu_lacks_it) {
    auto levels = randomLevels(PixelFormat::BC7, 20, 12, fullMipCount(20, 12), 5);
    auto file = writeDds(PixelFormat::BC7, 20, 12, levels);
    auto texture = read(file, false);
    EXPECT_EQ(PixelFormat::RGBA8, texture->pixelFormat());
    const auto &layer = texture->layers().front();
    ASSERT_EQ(static_cast<size_t>(fullMipCount(20, 12) - 1), layer.mipMaps.size());
    EXPECT_EQ(20u * 12u * 4u, layer.pixels->size());
    auto expected = decodeBlockCompressed(PixelFormat::BC7, 20, 12, reinterpret_cast<const uint8_t *>(levels[0].data()), levels[0].size());
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), reinterpret_cast<const uint8_t *>(layer.pixels->data())));
    EXPECT_EQ(10u * 6u * 4u, layer.mipMaps[0]->size());
    EXPECT_EQ(4u, layer.mipMaps.back()->size());
}

TEST(DdsReader, legacy_fourcc_headers_are_understood) {
    EXPECT_EQ(PixelFormat::DXT1, read(legacy("DXT1", 8, 8, 1, 32))->pixelFormat());
    EXPECT_EQ(PixelFormat::DXT5, read(legacy("DXT5", 8, 8, 1, 64))->pixelFormat());
    EXPECT_EQ(PixelFormat::BC5, read(legacy("ATI2", 8, 8, 1, 64))->pixelFormat());
    EXPECT_EQ(PixelFormat::BC5, read(legacy("BC5U", 8, 8, 1, 64))->pixelFormat());
    auto chain = read(legacy("DXT1", 8, 8, 4, 32 + 8 + 8 + 8));
    EXPECT_EQ(3u, chain->layers().front().mipMaps.size());
}

TEST(DdsReader, data_past_the_last_level_is_ignored) {
    EXPECT_NO_THROW(read(legacy("DXT1", 8, 8, 1, 32 + 100)));
}

TEST(DdsReader, files_it_cannot_use_are_rejected_with_a_validation_error) {
    EXPECT_THROW(read(ByteBuffer()), ValidationException);
    EXPECT_THROW(read(legacy("DXT1", 0, 8, 1, 32)), ValidationException) << "zero width";
    EXPECT_THROW(read(legacy("DXT1", 8, 0, 1, 32)), ValidationException) << "zero height";
    EXPECT_THROW(read(legacy("DXT1", 16385, 8, 1, 32)), ValidationException) << "too wide";
    EXPECT_THROW(read(legacy("DXT1", 8, 8, 5, 400)), ValidationException) << "more levels than the size allows";
    EXPECT_THROW(read(legacy("DXT1", 8, 8, 1, 31)), ValidationException) << "one byte short";
    EXPECT_THROW(read(legacy("DXT3", 8, 8, 1, 64)), ValidationException) << "BC2 is not supported";
    EXPECT_THROW(read(legacy("DXT1", 8, 8, 1, 32, 0x200 | 0xfe00)), ValidationException) << "cube map";
    EXPECT_THROW(read(legacy("DXT1", 8, 8, 1, 32, 0x200000)), ValidationException) << "volume";

    auto bad = legacy("DXT1", 8, 8, 1, 32);
    bad[0] = 'X';
    EXPECT_THROW(read(bad), ValidationException) << "magic";
    bad = legacy("DXT1", 8, 8, 1, 32);
    bad[4] = 100;
    EXPECT_THROW(read(bad), ValidationException) << "header size";
    bad = legacy("DXT1", 8, 8, 1, 32);
    bad[80] = 0; // pixel format flags: not a FourCC
    EXPECT_THROW(read(bad), ValidationException) << "uncompressed";
}

TEST(DdsReader, dx10_files_of_other_formats_or_shapes_are_rejected) {
    auto base = writeDds(PixelFormat::BC7, 8, 8, randomLevels(PixelFormat::BC7, 8, 8, 1, 2));
    auto withDxgi = [&](uint32_t dxgi) {
        auto f = base;
        for (int i = 0; i < 4; ++i) {
            f[128 + i] = static_cast<char>((dxgi >> (8 * i)) & 0xff);
        }
        return f;
    };
    EXPECT_NO_THROW(read(withDxgi(99))) << "BC7 sRGB";
    EXPECT_THROW(read(withDxgi(28)), ValidationException) << "RGBA8";
    EXPECT_THROW(read(withDxgi(84)), ValidationException) << "BC5 snorm";
    EXPECT_THROW(read(withDxgi(0)), ValidationException);
    auto array = base;
    array[128 + 12] = 3;
    EXPECT_THROW(read(array), ValidationException) << "array of three";
    auto dimension = base;
    dimension[128 + 4] = 4;
    EXPECT_THROW(read(dimension), ValidationException) << "3D";
}

TEST(DdsWriter, rejects_what_it_cannot_write) {
    EXPECT_THROW(writeDds(PixelFormat::RGBA8, 4, 4, {ByteBuffer(64)}), ValidationException);
    EXPECT_THROW(writeDds(PixelFormat::BC5, 4, 4, {}), ValidationException);
    EXPECT_THROW(writeDds(PixelFormat::BC5, 4, 4, {ByteBuffer(15)}), ValidationException) << "wrong level size";
    EXPECT_THROW(writeDds(PixelFormat::BC5, 4, 4, {ByteBuffer(16), ByteBuffer(16), ByteBuffer(16), ByteBuffer(16)}), ValidationException) << "too many levels";
    EXPECT_THROW(writeDds(PixelFormat::BC5, 0, 4, {ByteBuffer(16)}), ValidationException);
}

TEST(DdsWriter, counts_a_full_chain) {
    EXPECT_EQ(1, fullMipCount(1, 1));
    EXPECT_EQ(3, fullMipCount(4, 4));
    EXPECT_EQ(6, fullMipCount(32, 17));
    EXPECT_EQ(11, fullMipCount(1024, 1));
}

TEST(BlockCompressed, level_sizes_round_up_to_whole_blocks) {
    EXPECT_EQ(8u, compressedLevelSize(PixelFormat::DXT1, 1, 1));
    EXPECT_EQ(16u, compressedLevelSize(PixelFormat::BC5, 4, 4));
    EXPECT_EQ(16u * 2 * 1, compressedLevelSize(PixelFormat::BC7, 5, 4));
    EXPECT_EQ(0, blockBytes(PixelFormat::RGBA8));
    EXPECT_THROW(decodeBlockCompressed(PixelFormat::RGBA8, 4, 4, nullptr, 0), ValidationException);
    uint8_t block[16] = {};
    EXPECT_THROW(decodeBlockCompressed(PixelFormat::BC7, 8, 4, block, 16), ValidationException) << "needs two blocks";
}

TEST(BlockCompressed, a_bc5_block_decodes_its_two_channels) {
    // Channel endpoints 255 and 0 with all indices 0 / 1: every pixel is the first / second endpoint.
    uint8_t block[16] = {255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0x49, 0x92, 0x24, 0x49, 0x92, 0x24};
    auto px = decodeBlockCompressed(PixelFormat::BC5, 4, 4, block, 16);
    ASSERT_EQ(64u, px.size());
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(255, px[i * 4 + 0]) << "red index 0 is endpoint 0";
        EXPECT_EQ(255, px[i * 4 + 1]) << "green index 1 (001 repeated) is endpoint 1 = 255";
        EXPECT_EQ(0, px[i * 4 + 2]);
        EXPECT_EQ(255, px[i * 4 + 3]);
    }
}
