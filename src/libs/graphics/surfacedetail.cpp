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

#include "reone/graphics/surfacedetail.h"

#include <array>
#include <cmath>

namespace reone {

namespace graphics {

namespace surfacedetail {

std::string mapFileName(const std::string &texture, MapKind kind) {
    return boost::to_lower_copy(texture) + (kind == MapKind::Normal ? "_n.tga" : "_m.tga");
}

std::string mapFileNameDds(const std::string &texture, MapKind kind) {
    return boost::to_lower_copy(texture) + (kind == MapKind::Normal ? "_n.dds" : "_m.dds");
}

std::optional<std::filesystem::path> findMapFile(const std::filesystem::path &folder, const std::string &texture, MapKind kind) {
    if (folder.empty() || texture.empty()) {
        return std::nullopt;
    }
    std::error_code ec;
    // A DDS beats a TGA of the same map: texgen writes only one of them, and a stale TGA
    // from an earlier run must not shadow a newer DDS.
    for (const auto &name : {mapFileNameDds(texture, kind), mapFileName(texture, kind)}) {
        auto path = folder / name;
        if (std::filesystem::is_regular_file(path, ec)) {
            return path;
        }
    }
    return std::nullopt;
}

std::filesystem::path defaultFolder(const std::filesystem::path &executablePath) {
    auto dir = executablePath.empty() ? std::filesystem::current_path() : executablePath.parent_path();
    return dir / "enhanced_textures";
}

NormalSource chooseNormalSource(bool featureOn, bool hasGameBumpMap, bool hasGeneratedMap) {
    if (hasGameBumpMap) {
        return NormalSource::Game;
    }
    if (featureOn && hasGeneratedMap) {
        return NormalSource::Generated;
    }
    return NormalSource::None;
}

static glm::vec3 anyPerpendicular(const glm::vec3 &n) {
    glm::vec3 axis = std::abs(n.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::normalize(glm::cross(axis, n));
}

TangentFrames computeTangentFrames(const std::vector<glm::vec3> &positions,
                                   const std::vector<glm::vec3> &normals,
                                   const std::vector<glm::vec2> &uvs,
                                   const std::vector<std::array<uint16_t, 3>> &triangles) {
    size_t count = positions.size();
    std::vector<glm::vec3> tan(count, glm::vec3(0.0f));
    std::vector<glm::vec3> bitan(count, glm::vec3(0.0f));
    for (const auto &tri : triangles) {
        if (tri[0] >= count || tri[1] >= count || tri[2] >= count) {
            continue;
        }
        const auto &p0 = positions[tri[0]];
        glm::vec3 e1 = positions[tri[1]] - p0;
        glm::vec3 e2 = positions[tri[2]] - p0;
        glm::vec2 d1 = uvs[tri[1]] - uvs[tri[0]];
        glm::vec2 d2 = uvs[tri[2]] - uvs[tri[0]];
        float det = d1.x * d2.y - d2.x * d1.y;
        if (std::abs(det) < 1e-12f) {
            continue; // no UV area: says nothing about the tangent
        }
        float r = 1.0f / det;
        // Unnormalised derivatives of position along u and v: their length
        // grows with the triangle's area relative to its UV area, which
        // weights big triangles more, as the standard accumulation does.
        glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
        glm::vec3 b = (e2 * d1.x - e1 * d2.x) * r;
        // Scale by the triangle's UV area so a tiny UV island with a huge
        // derivative does not swamp its neighbours.
        float w = std::abs(det);
        for (int i = 0; i < 3; ++i) {
            tan[tri[i]] += w * t;
            bitan[tri[i]] += w * b;
        }
    }
    TangentFrames frames;
    frames.tangents.resize(count);
    frames.bitangents.resize(count);
    for (size_t i = 0; i < count; ++i) {
        glm::vec3 n = i < normals.size() ? normals[i] : glm::vec3(0.0f, 0.0f, 1.0f);
        float nlen = glm::length(n);
        n = nlen > 1e-8f ? n / nlen : glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec3 t = tan[i] - n * glm::dot(n, tan[i]);
        float tlen = glm::length(t);
        if (tlen < 1e-8f || !std::isfinite(tlen)) {
            t = anyPerpendicular(n);
        } else {
            t /= tlen;
        }
        float handedness = glm::dot(glm::cross(n, t), bitan[i]) < 0.0f ? -1.0f : 1.0f;
        frames.tangents[i] = t;
        frames.bitangents[i] = handedness * glm::cross(n, t);
    }
    return frames;
}

TangentStats &tangentStats() {
    static TangentStats stats;
    return stats;
}

// Procedural test patterns.

static uint32_t nameHash(const std::string &texture) {
    uint32_t h = 2166136261u; // FNV-1a
    for (char c : boost::to_lower_copy(texture)) {
        h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    }
    return h;
}

static float hash2(int x, int y, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xffffff) / static_cast<float>(0xffffff);
}

// Tileable value noise over a period of `cells` cells.
static float valueNoise(float u, float v, int cells, uint32_t seed) {
    float x = u * cells;
    float y = v * cells;
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    float fx = x - x0;
    float fy = y - y0;
    auto at = [&](int cx, int cy) {
        return hash2(((cx % cells) + cells) % cells, ((cy % cells) + cells) % cells, seed);
    };
    float sx = fx * fx * (3.0f - 2.0f * fx);
    float sy = fy * fy * (3.0f - 2.0f * fy);
    float a = glm::mix(at(x0, y0), at(x0 + 1, y0), sx);
    float b = glm::mix(at(x0, y0 + 1), at(x0 + 1, y0 + 1), sx);
    return glm::mix(a, b, sy);
}

struct Pattern {
    int kind;      // 0 bricks, 1 rivets, 2 noise
    uint32_t seed;
};

static Pattern patternOf(const std::string &texture) {
    uint32_t h = nameHash(texture);
    return Pattern {static_cast<int>(h % 3), h};
}

// Height in [0, 1] and a "groove" mask (1 in mortar / low areas), tileable in u and v.
static std::pair<float, float> patternHeight(const Pattern &p, float u, float v) {
    switch (p.kind) {
    case 0: {
        // 4 x 8 bricks, every other row offset by half a brick.
        float rows = 8.0f, cols = 4.0f;
        float y = v * rows;
        int row = static_cast<int>(std::floor(y));
        float x = u * cols + ((row & 1) ? 0.5f : 0.0f);
        float fx = x - std::floor(x);
        float fy = y - row;
        float edge = std::min(std::min(fx, 1.0f - fx) * 2.0f, std::min(fy, 1.0f - fy));
        float mortar = 1.0f - glm::smoothstep(0.02f, 0.07f, edge);
        float grain = valueNoise(u, v, 32, p.seed) * 0.15f;
        return {(1.0f - mortar) * (0.85f + grain), mortar};
    }
    case 1: {
        // A grid of round rivets on a lightly noisy plate.
        float cells = 8.0f;
        float fx = u * cells - std::floor(u * cells) - 0.5f;
        float fy = v * cells - std::floor(v * cells) - 0.5f;
        float r = std::sqrt(fx * fx + fy * fy);
        float dome = std::max(0.0f, 1.0f - r / 0.22f);
        float plate = valueNoise(u, v, 16, p.seed) * 0.2f;
        float seam = 1.0f - glm::smoothstep(0.0f, 0.03f, std::min(0.5f - std::abs(fx), 0.5f - std::abs(fy)));
        return {std::sqrt(dome) * 0.8f + plate - 0.3f * seam + 0.3f, seam};
    }
    default: {
        float n = 0.6f * valueNoise(u, v, 8, p.seed) + 0.3f * valueNoise(u, v, 16, p.seed + 1) + 0.1f * valueNoise(u, v, 32, p.seed + 2);
        return {n, 1.0f - n};
    }
    }
}

std::vector<uint8_t> proceduralNormalMap(const std::string &texture, int size) {
    Pattern p = patternOf(texture);
    std::vector<float> height(static_cast<size_t>(size) * size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            height[y * size + x] = patternHeight(p, (x + 0.5f) / size, (y + 0.5f) / size).first;
        }
    }
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
    // Slope per unit of UV height gradient, per pattern: the brick edges are
    // steep, the noise is gentle and needs more gain to show.
    static constexpr float kSlope[] {1.0f / 64.0f, 1.0f / 24.0f, 1.0f / 4.0f};
    float bump = size * kSlope[p.kind];
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            auto h = [&](int hx, int hy) {
                return height[((hy + size) % size) * size + ((hx + size) % size)];
            };
            float dx = (h(x + 1, y) - h(x - 1, y)) * 0.5f * bump;
            float dy = (h(x, y + 1) - h(x, y - 1)) * 0.5f * bump;
            glm::vec3 n = glm::normalize(glm::vec3(-dx, -dy, 1.0f));
            uint8_t *px = &pixels[(static_cast<size_t>(y) * size + x) * 4];
            px[0] = static_cast<uint8_t>(std::lround((n.x * 0.5f + 0.5f) * 255.0f));
            px[1] = static_cast<uint8_t>(std::lround((n.y * 0.5f + 0.5f) * 255.0f));
            px[2] = static_cast<uint8_t>(std::lround((n.z * 0.5f + 0.5f) * 255.0f));
            px[3] = 255;
        }
    }
    return pixels;
}

std::vector<uint8_t> proceduralMaterialMap(const std::string &texture, int size) {
    Pattern p = patternOf(texture);
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float groove = patternHeight(p, (x + 0.5f) / size, (y + 0.5f) / size).second;
            float roughness = glm::mix(0.3f, 0.95f, groove);
            float specular = glm::mix(0.7f, 0.05f, groove);
            uint8_t *px = &pixels[(static_cast<size_t>(y) * size + x) * 4];
            px[0] = static_cast<uint8_t>(std::lround(roughness * 255.0f));
            px[1] = static_cast<uint8_t>(std::lround(specular * 255.0f));
            px[2] = 0;
            px[3] = 255;
        }
    }
    return pixels;
}

} // namespace surfacedetail

} // namespace graphics

} // namespace reone
