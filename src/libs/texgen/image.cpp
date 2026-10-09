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

#include "reone/texgen/image.h"

#include "reone/graphics/dxtutil.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/textureutil.h"
#include "reone/system/exception/validation.h"

namespace reone {

namespace texgen {

using graphics::PixelFormat;

Image imageFromPixels(int width, int height, PixelFormat format, const ByteBuffer &pixels) {
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        throw ValidationException("texture dimensions out of range");
    }
    size_t count = static_cast<size_t>(width) * height;
    Image image(width, height);
    auto byteAt = [&](size_t i) { return static_cast<float>(static_cast<uint8_t>(pixels[i])) / 255.0f; };
    auto need = [&](size_t bytes) {
        if (pixels.size() < bytes) {
            throw ValidationException("texture pixel data is too short");
        }
    };
    switch (format) {
    case PixelFormat::R8:
        need(count);
        for (size_t i = 0; i < count; ++i) {
            float v = byteAt(i);
            float *p = &image.rgba[i * 4];
            p[0] = p[1] = p[2] = v;
            p[3] = 1.0f;
        }
        break;
    case PixelFormat::RGB8:
    case PixelFormat::BGR8: {
        need(count * 3);
        bool bgr = format == PixelFormat::BGR8;
        for (size_t i = 0; i < count; ++i) {
            float *p = &image.rgba[i * 4];
            p[0] = byteAt(i * 3 + (bgr ? 2 : 0));
            p[1] = byteAt(i * 3 + 1);
            p[2] = byteAt(i * 3 + (bgr ? 0 : 2));
            p[3] = 1.0f;
        }
        break;
    }
    case PixelFormat::RGBA8:
    case PixelFormat::BGRA8: {
        need(count * 4);
        bool bgr = format == PixelFormat::BGRA8;
        for (size_t i = 0; i < count; ++i) {
            float *p = &image.rgba[i * 4];
            p[0] = byteAt(i * 4 + (bgr ? 2 : 0));
            p[1] = byteAt(i * 4 + 1);
            p[2] = byteAt(i * 4 + (bgr ? 0 : 2));
            p[3] = byteAt(i * 4 + 3);
        }
        break;
    }
    case PixelFormat::DXT1:
    case PixelFormat::DXT5: {
        bool dxt5 = format == PixelFormat::DXT5;
        size_t blocks = ((static_cast<size_t>(width) + 3) / 4) * ((static_cast<size_t>(height) + 3) / 4);
        need(blocks * (dxt5 ? 16 : 8));
        std::vector<uint32_t> decoded(count);
        if (dxt5) {
            graphics::decompressDXT5(width, height, reinterpret_cast<const uint8_t *>(pixels.data()), decoded.data());
        } else {
            graphics::decompressDXT1(width, height, reinterpret_cast<const uint8_t *>(pixels.data()), decoded.data());
        }
        for (size_t i = 0; i < count; ++i) {
            uint32_t v = decoded[i];
            float *p = &image.rgba[i * 4];
            p[0] = static_cast<float>((v >> 24) & 0xff) / 255.0f;
            p[1] = static_cast<float>((v >> 16) & 0xff) / 255.0f;
            p[2] = static_cast<float>((v >> 8) & 0xff) / 255.0f;
            p[3] = dxt5 ? static_cast<float>(v & 0xff) / 255.0f : 1.0f;
        }
        break;
    }
    default:
        throw ValidationException("unsupported texture pixel format");
    }
    return image;
}

Image imageFromTexture(const graphics::Texture &texture) {
    if (texture.layers().empty() || !texture.layers().front().pixels) {
        throw ValidationException("texture has no pixels");
    }
    return imageFromPixels(texture.width(), texture.height(), texture.pixelFormat(), *texture.layers().front().pixels);
}

ByteBuffer toBytes(const Image &image, int channels) {
    ByteBuffer out(static_cast<size_t>(image.width) * image.height * channels);
    size_t count = static_cast<size_t>(image.width) * image.height;
    for (size_t i = 0; i < count; ++i) {
        for (int c = 0; c < channels; ++c) {
            float v = std::clamp(image.rgba[i * 4 + c], 0.0f, 1.0f);
            out[i * channels + c] = static_cast<char>(static_cast<uint8_t>(std::lround(v * 255.0f)));
        }
    }
    return out;
}

} // namespace texgen

} // namespace reone
