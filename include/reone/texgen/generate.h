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

#include "image.h"

namespace reone {

namespace texgen {

struct NormalOptions {
    /// How steep the surface is: the slope the height field is scaled by before the normal is built.
    float strength {2.0f};
    /// Wrap at the edges (a tiling texture) instead of clamping.
    bool wrap {true};
    /// Radius in pixels of the blur that separates the low-frequency shape from the detail; < 0 picks one from the size.
    float blurRadius {-1.0f};
    /// How much of the detail (luma minus its blur) and of the low-frequency shape goes into the height.
    float detailWeight {1.0f};
    float lowWeight {0.25f};
};

struct MaterialOptions {
    /// Roughness of a plain surface, 0..1.
    float baseRoughness {0.8f};
    /// A shiny texture (its TXI names an environment map or a bumpy-shiny map): smoother, with a metalness hint.
    bool shiny {false};
    bool wrap {true};
};

/// Height from luma: the blur-separated detail plus a little of the low frequencies. Values are about 0..1.
std::vector<float> estimateHeight(const Image &source, const NormalOptions &options);

/**
 * A tangent-space normal map (x right, y up, z out, encoded n * 0.5 + 0.5) from a height
 * field, by Sobel filtering. Alpha is 1.
 */
Image normalsFromHeight(const std::vector<float> &height, int width, int heightPixels, float strength, bool wrap);

Image generateNormalMap(const Image &source, const NormalOptions &options);

/**
 * A packed material map: red is roughness, green a specular/metalness hint, blue 0, alpha 1.
 * Roughness rises with local contrast and falls with brightness; a shiny texture is smoother.
 */
Image generateMaterialMap(const Image &source, const MaterialOptions &options);

/// Gaussian blur of a one-channel field.
std::vector<float> blurField(const std::vector<float> &field, int width, int height, float radius, bool wrap);

} // namespace texgen

} // namespace reone
