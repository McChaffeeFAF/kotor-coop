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

#include "reone/texgen/generate.h"

namespace reone {

namespace texgen {

namespace {

int wrapIndex(int i, int n, bool wrap) {
    if (wrap) {
        i %= n;
        return i < 0 ? i + n : i;
    }
    return std::clamp(i, 0, n - 1);
}

float autoRadius(int width, int height) {
    return std::max(1.0f, std::round(static_cast<float>(std::min(width, height)) / 64.0f));
}

} // namespace

std::vector<float> blurField(const std::vector<float> &field, int width, int height, float radius, bool wrap) {
    int r = std::max(1, static_cast<int>(std::ceil(radius)));
    float sigma = std::max(0.5f, radius * 0.5f);
    std::vector<float> kernel(2 * r + 1);
    float sum = 0.0f;
    for (int i = -r; i <= r; ++i) {
        kernel[i + r] = std::exp(-static_cast<float>(i * i) / (2.0f * sigma * sigma));
        sum += kernel[i + r];
    }
    for (auto &k : kernel) {
        k /= sum;
    }
    std::vector<float> tmp(field.size());
    std::vector<float> out(field.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float v = 0.0f;
            for (int i = -r; i <= r; ++i) {
                v += kernel[i + r] * field[static_cast<size_t>(y) * width + wrapIndex(x + i, width, wrap)];
            }
            tmp[static_cast<size_t>(y) * width + x] = v;
        }
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float v = 0.0f;
            for (int i = -r; i <= r; ++i) {
                v += kernel[i + r] * tmp[static_cast<size_t>(wrapIndex(y + i, height, wrap)) * width + x];
            }
            out[static_cast<size_t>(y) * width + x] = v;
        }
    }
    return out;
}

std::vector<float> estimateHeight(const Image &source, const NormalOptions &options) {
    size_t count = static_cast<size_t>(source.width) * source.height;
    std::vector<float> lumaField(count);
    for (size_t i = 0; i < count; ++i) {
        lumaField[i] = luma(&source.rgba[i * 4]);
    }
    float radius = options.blurRadius >= 0.0f ? options.blurRadius : autoRadius(source.width, source.height);
    auto low = blurField(lumaField, source.width, source.height, radius, options.wrap);
    std::vector<float> height(count);
    for (size_t i = 0; i < count; ++i) {
        height[i] = options.detailWeight * (lumaField[i] - low[i]) + options.lowWeight * low[i];
    }
    return height;
}

Image normalsFromHeight(const std::vector<float> &height, int width, int heightPixels, float strength, bool wrap) {
    Image out(width, heightPixels);
    auto h = [&](int x, int y) {
        return height[static_cast<size_t>(wrapIndex(y, heightPixels, wrap)) * width + wrapIndex(x, width, wrap)];
    };
    for (int y = 0; y < heightPixels; ++y) {
        for (int x = 0; x < width; ++x) {
            // Sobel: the slope toward +x and toward +y (up).
            float dx = ((h(x + 1, y - 1) + 2.0f * h(x + 1, y) + h(x + 1, y + 1)) -
                        (h(x - 1, y - 1) + 2.0f * h(x - 1, y) + h(x - 1, y + 1))) / 8.0f;
            float dy = ((h(x - 1, y + 1) + 2.0f * h(x, y + 1) + h(x + 1, y + 1)) -
                        (h(x - 1, y - 1) + 2.0f * h(x, y - 1) + h(x + 1, y - 1))) / 8.0f;
            glm::vec3 n(-strength * dx, -strength * dy, 1.0f);
            n = glm::normalize(n);
            float *p = out.at(x, y);
            p[0] = n.x * 0.5f + 0.5f;
            p[1] = n.y * 0.5f + 0.5f;
            p[2] = n.z * 0.5f + 0.5f;
            p[3] = 1.0f;
        }
    }
    return out;
}

Image generateNormalMap(const Image &source, const NormalOptions &options) {
    auto height = estimateHeight(source, options);
    return normalsFromHeight(height, source.width, source.height, options.strength, options.wrap);
}

Image generateMaterialMap(const Image &source, const MaterialOptions &options) {
    size_t count = static_cast<size_t>(source.width) * source.height;
    std::vector<float> lumaField(count);
    std::vector<float> squared(count);
    for (size_t i = 0; i < count; ++i) {
        lumaField[i] = luma(&source.rgba[i * 4]);
        squared[i] = lumaField[i] * lumaField[i];
    }
    float radius = 2.0f;
    auto mean = blurField(lumaField, source.width, source.height, radius, options.wrap);
    auto meanSq = blurField(squared, source.width, source.height, radius, options.wrap);
    Image out(source.width, source.height);
    for (size_t i = 0; i < count; ++i) {
        float variance = std::max(0.0f, meanSq[i] - mean[i] * mean[i]);
        float contrast = std::sqrt(variance); // local standard deviation of luma
        float roughness = options.baseRoughness + 1.5f * contrast - 0.25f * (mean[i] - 0.5f);
        float metalness = 0.0f;
        if (options.shiny) {
            roughness *= 0.6f;
            metalness = 0.5f + 0.5f * mean[i];
        }
        float *p = &out.rgba[i * 4];
        p[0] = std::clamp(roughness, 0.05f, 1.0f);
        p[1] = std::clamp(metalness, 0.0f, 1.0f);
        p[2] = 0.0f;
        p[3] = 1.0f;
    }
    return out;
}

} // namespace texgen

} // namespace reone
