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

#include "reone/graphics/bcutil.h"

#include "reone/graphics/dxtutil.h"
#include "reone/system/exception/validation.h"

#include "bc7decomp.h"

namespace reone {

namespace graphics {

int blockBytes(PixelFormat format) {
    switch (format) {
    case PixelFormat::DXT1:
        return 8;
    case PixelFormat::DXT5:
    case PixelFormat::BC5:
    case PixelFormat::BC7:
        return 16;
    default:
        return 0;
    }
}

size_t compressedLevelSize(PixelFormat format, int width, int height) {
    size_t blocksX = (static_cast<size_t>(std::max(1, width)) + 3) / 4;
    size_t blocksY = (static_cast<size_t>(std::max(1, height)) + 3) / 4;
    return blocksX * blocksY * static_cast<size_t>(blockBytes(format));
}

// BC4 channel of one block: two 8-bit endpoints and sixteen 3-bit indices.
static void decodeBC4Block(const uint8_t *block, uint8_t out[16]) {
    uint8_t palette[8];
    palette[0] = block[0];
    palette[1] = block[1];
    if (palette[0] > palette[1]) {
        for (int i = 1; i <= 6; ++i) {
            palette[1 + i] = static_cast<uint8_t>(((7 - i) * palette[0] + i * palette[1] + 3) / 7);
        }
    } else {
        for (int i = 1; i <= 4; ++i) {
            palette[1 + i] = static_cast<uint8_t>(((5 - i) * palette[0] + i * palette[1] + 2) / 5);
        }
        palette[6] = 0;
        palette[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) {
        bits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
    }
    for (int i = 0; i < 16; ++i) {
        out[i] = palette[(bits >> (3 * i)) & 7];
    }
}

std::vector<uint8_t> decodeBlockCompressed(PixelFormat format, int width, int height, const uint8_t *data, size_t size) {
    if (blockBytes(format) == 0) {
        throw ValidationException("not a block-compressed format");
    }
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        throw ValidationException("texture dimensions out of range");
    }
    if (size < compressedLevelSize(format, width, height)) {
        throw ValidationException("compressed texture data is too short");
    }
    size_t w = static_cast<size_t>(width);
    size_t h = static_cast<size_t>(height);
    std::vector<uint8_t> out(w * h * 4);

    if (format == PixelFormat::DXT1 || format == PixelFormat::DXT5) {
        std::vector<uint32_t> packed(w * h);
        if (format == PixelFormat::DXT5) {
            decompressDXT5(width, height, data, packed.data());
        } else {
            decompressDXT1(width, height, data, packed.data());
        }
        for (size_t i = 0; i < w * h; ++i) {
            uint32_t v = packed[i];
            out[i * 4 + 0] = static_cast<uint8_t>(v >> 24);
            out[i * 4 + 1] = static_cast<uint8_t>(v >> 16);
            out[i * 4 + 2] = static_cast<uint8_t>(v >> 8);
            out[i * 4 + 3] = static_cast<uint8_t>(v);
        }
        return out;
    }

    size_t blocksX = (w + 3) / 4;
    size_t blocksY = (h + 3) / 4;
    for (size_t by = 0; by < blocksY; ++by) {
        for (size_t bx = 0; bx < blocksX; ++bx) {
            const uint8_t *block = data + (by * blocksX + bx) * 16;
            uint8_t px[16][4];
            if (format == PixelFormat::BC5) {
                uint8_t r[16], g[16];
                decodeBC4Block(block, r);
                decodeBC4Block(block + 8, g);
                for (int i = 0; i < 16; ++i) {
                    px[i][0] = r[i];
                    px[i][1] = g[i];
                    px[i][2] = 0;
                    px[i][3] = 255;
                }
            } else {
                bc7decomp::color_rgba pixels[16];
                bc7decomp::unpack_bc7(block, pixels);
                for (int i = 0; i < 16; ++i) {
                    for (int c = 0; c < 4; ++c) {
                        px[i][c] = pixels[i][c];
                    }
                }
            }
            for (size_t y = 0; y < 4 && by * 4 + y < h; ++y) {
                for (size_t x = 0; x < 4 && bx * 4 + x < w; ++x) {
                    std::memcpy(&out[((by * 4 + y) * w + bx * 4 + x) * 4], px[y * 4 + x], 4);
                }
            }
        }
    }
    return out;
}

} // namespace graphics

} // namespace reone
