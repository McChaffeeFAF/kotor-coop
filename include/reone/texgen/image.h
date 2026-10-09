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

#pragma once

#include "reone/graphics/types.h"
#include "reone/system/types.h"

namespace reone {

namespace graphics {

class Texture;

}

namespace texgen {

/**
 * A float RGBA image, row 0 at the bottom (the order the engine uploads its textures
 * in), every channel 0..1.
 */
struct Image {
    int width {0};
    int height {0};
    std::vector<float> rgba;

    Image() = default;
    Image(int width, int height) :
        width(width),
        height(height),
        rgba(static_cast<size_t>(width) * height * 4, 0.0f) {
    }

    bool empty() const { return width <= 0 || height <= 0; }
    float *at(int x, int y) { return &rgba[(static_cast<size_t>(y) * width + x) * 4]; }
    const float *at(int x, int y) const { return &rgba[(static_cast<size_t>(y) * width + x) * 4]; }
};

/// Decodes pixels in one of the formats textures load in (R8, RGB8, RGBA8, BGR8, BGRA8, DXT1, DXT5).
Image imageFromPixels(int width, int height, graphics::PixelFormat format, const ByteBuffer &pixels);

/// The first layer of a 2D texture.
Image imageFromTexture(const graphics::Texture &texture);

/// Rec. 601 luma.
inline float luma(const float *rgba) {
    return 0.299f * rgba[0] + 0.587f * rgba[1] + 0.114f * rgba[2];
}

/// 8-bit channels, `channels` of them (3 or 4) per pixel, rows bottom to top.
ByteBuffer toBytes(const Image &image, int channels);

} // namespace texgen

} // namespace reone
