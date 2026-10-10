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

#include "reone/graphics/format/ddsreader.h"

#include "reone/graphics/bcutil.h"
#include "reone/graphics/format/ddswriter.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/textureutil.h"
#include "reone/system/binaryreader.h"
#include "reone/system/exception/validation.h"

namespace reone {

namespace graphics {

namespace {

constexpr uint32_t fourCC(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) | static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8 |
           static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16 | static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24;
}

constexpr uint32_t kDdsdMipMapCount = 0x20000;
constexpr uint32_t kPfFourCC = 0x4;
constexpr uint32_t kCaps2Cubemap = 0x200;
constexpr uint32_t kCaps2Volume = 0x200000;

PixelFormat formatFromDxgi(uint32_t dxgi) {
    switch (dxgi) {
    case 70: // BC1_TYPELESS
    case 71: // BC1_UNORM
    case 72: // BC1_UNORM_SRGB
        return PixelFormat::DXT1;
    case 76: // BC3_TYPELESS
    case 77:
    case 78:
        return PixelFormat::DXT5;
    case 82: // BC5_TYPELESS
    case 83: // BC5_UNORM
        return PixelFormat::BC5;
    case 97: // BC7_TYPELESS
    case 98:
    case 99:
        return PixelFormat::BC7;
    default:
        throw ValidationException("unsupported DDS format");
    }
}

} // namespace

void DdsReader::load() {
    BinaryReader reader(_dds);
    if (reader.length() < 128) {
        throw ValidationException("DDS file is too short");
    }
    if (reader.readUint32() != fourCC('D', 'D', 'S', ' ')) {
        throw ValidationException("not a DDS file");
    }
    if (reader.readUint32() != 124) {
        throw ValidationException("bad DDS header size");
    }
    uint32_t flags = reader.readUint32();
    uint32_t height = reader.readUint32();
    uint32_t width = reader.readUint32();
    reader.skipBytes(8); // pitch or linear size, depth
    uint32_t mipCount = reader.readUint32();
    reader.skipBytes(44);
    if (reader.readUint32() != 32) {
        throw ValidationException("bad DDS pixel format size");
    }
    uint32_t pfFlags = reader.readUint32();
    uint32_t cc = reader.readUint32();
    reader.skipBytes(20);
    reader.skipBytes(4); // caps
    uint32_t caps2 = reader.readUint32();
    reader.skipBytes(12);

    if (width == 0 || height == 0 || width > 16384 || height > 16384) {
        throw ValidationException("DDS dimensions out of range");
    }
    if (caps2 & (kCaps2Cubemap | kCaps2Volume)) {
        throw ValidationException("DDS cube maps and volumes are not supported");
    }
    if (!(pfFlags & kPfFourCC)) {
        throw ValidationException("uncompressed DDS files are not supported");
    }

    PixelFormat format;
    if (cc == fourCC('D', 'X', '1', '0')) {
        if (reader.length() < 148) {
            throw ValidationException("DDS file is too short");
        }
        uint32_t dxgi = reader.readUint32();
        uint32_t dimension = reader.readUint32();
        reader.skipBytes(4);
        uint32_t arraySize = reader.readUint32();
        reader.skipBytes(4);
        if (dimension != 3 || arraySize != 1) {
            throw ValidationException("DDS arrays are not supported");
        }
        format = formatFromDxgi(dxgi);
    } else if (cc == fourCC('D', 'X', 'T', '1')) {
        format = PixelFormat::DXT1;
    } else if (cc == fourCC('D', 'X', 'T', '5')) {
        format = PixelFormat::DXT5;
    } else if (cc == fourCC('A', 'T', 'I', '2') || cc == fourCC('B', 'C', '5', 'U')) {
        format = PixelFormat::BC5;
    } else {
        throw ValidationException("unsupported DDS format");
    }

    int levels = (flags & kDdsdMipMapCount) ? static_cast<int>(mipCount) : 1;
    int maxLevels = fullMipCount(static_cast<int>(width), static_cast<int>(height));
    if (levels < 1 || levels > maxLevels) {
        throw ValidationException("bad DDS mip count");
    }

    // Every level must be present before anything is kept.
    size_t total = 0;
    for (int i = 0; i < levels; ++i) {
        total += compressedLevelSize(format, std::max(1, static_cast<int>(width) >> i), std::max(1, static_cast<int>(height) >> i));
    }
    size_t remaining = static_cast<size_t>(reader.length() - reader.position());
    if (remaining < total) {
        throw ValidationException("DDS file is truncated");
    }

    std::vector<std::shared_ptr<ByteBuffer>> data;
    for (int i = 0; i < levels; ++i) {
        size_t size = compressedLevelSize(format, std::max(1, static_cast<int>(width) >> i), std::max(1, static_cast<int>(height) >> i));
        data.push_back(std::make_shared<ByteBuffer>(reader.readBytes(static_cast<int>(size))));
    }

    if (format == PixelFormat::BC7 && !_nativeBC7) {
        // Decode every level to RGBA8.
        for (int i = 0; i < levels; ++i) {
            int w = std::max(1, static_cast<int>(width) >> i);
            int h = std::max(1, static_cast<int>(height) >> i);
            auto rgba = decodeBlockCompressed(PixelFormat::BC7, w, h, reinterpret_cast<const uint8_t *>(data[i]->data()), data[i]->size());
            data[i] = std::make_shared<ByteBuffer>(rgba.begin(), rgba.end());
        }
        format = PixelFormat::RGBA8;
    }

    Texture::Layer layer;
    layer.pixels = std::move(data.front());
    layer.mipMaps.assign(std::make_move_iterator(data.begin() + 1), std::make_move_iterator(data.end()));
    _texture = std::make_shared<Texture>(_resRef, TextureType::TwoDim, getTextureProperties(_usage));
    _texture->setPixels(static_cast<int>(width), static_cast<int>(height), format, std::move(layer));
}

} // namespace graphics

} // namespace reone
