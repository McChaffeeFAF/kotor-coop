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

#include "reone/graphics/format/ddswriter.h"

#include "reone/graphics/bcutil.h"
#include "reone/system/exception/validation.h"

namespace reone {

namespace graphics {

int fullMipCount(int width, int height) {
    int count = 1;
    int size = std::max(width, height);
    while (size > 1) {
        size >>= 1;
        ++count;
    }
    return count;
}

static void put32(ByteBuffer &out, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
    }
}

ByteBuffer writeDds(PixelFormat format, int width, int height, const std::vector<ByteBuffer> &levels) {
    if (blockBytes(format) == 0) {
        throw ValidationException("DDS output needs a block-compressed format");
    }
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        throw ValidationException("texture dimensions out of range");
    }
    if (levels.empty() || static_cast<int>(levels.size()) > fullMipCount(width, height)) {
        throw ValidationException("wrong number of mip levels");
    }
    for (size_t i = 0; i < levels.size(); ++i) {
        if (levels[i].size() != compressedLevelSize(format, std::max(1, width >> i), std::max(1, height >> i))) {
            throw ValidationException("mip level has the wrong size");
        }
    }
    uint32_t dxgi = 0;
    switch (format) {
    case PixelFormat::DXT1:
        dxgi = 71; // BC1_UNORM
        break;
    case PixelFormat::DXT5:
        dxgi = 77; // BC3_UNORM
        break;
    case PixelFormat::BC5:
        dxgi = 83; // BC5_UNORM
        break;
    default:
        dxgi = 98; // BC7_UNORM
        break;
    }
    constexpr uint32_t kCaps = 0x1000 | 0x8 | 0x400000; // TEXTURE | COMPLEX | MIPMAP
    constexpr uint32_t kFlags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000; // CAPS HEIGHT WIDTH PIXELFORMAT MIPMAPCOUNT LINEARSIZE
    ByteBuffer out;
    out.reserve(148 + compressedLevelSize(format, width, height) * 2);
    out.insert(out.end(), {'D', 'D', 'S', ' '});
    put32(out, 124);
    put32(out, kFlags);
    put32(out, static_cast<uint32_t>(height));
    put32(out, static_cast<uint32_t>(width));
    put32(out, static_cast<uint32_t>(levels.front().size()));
    put32(out, 0);
    put32(out, static_cast<uint32_t>(levels.size()));
    for (int i = 0; i < 11; ++i) {
        put32(out, 0);
    }
    put32(out, 32); // pixel format
    put32(out, 0x4); // FOURCC
    out.insert(out.end(), {'D', 'X', '1', '0'});
    for (int i = 0; i < 5; ++i) {
        put32(out, 0);
    }
    put32(out, kCaps);
    for (int i = 0; i < 4; ++i) {
        put32(out, 0);
    }
    put32(out, dxgi);
    put32(out, 3); // TEXTURE2D
    put32(out, 0);
    put32(out, 1);
    put32(out, 0);
    for (const auto &level : levels) {
        out.insert(out.end(), level.begin(), level.end());
    }
    return out;
}

} // namespace graphics

} // namespace reone
