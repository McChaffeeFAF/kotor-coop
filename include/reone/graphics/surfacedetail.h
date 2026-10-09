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

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "glm/glm.hpp"

namespace reone {

namespace graphics {

/**
 * Tier 4 "Surface Detail" (design note graphics_preset): normal and
 * material maps that a local tool (texgen) generated from the player's own
 * textures, read from an override folder outside the game install, and
 * the tangent frames the shading needs on meshes that have none.
 */
namespace surfacedetail {

enum class MapKind {
    Normal,  /**< <texture>_n.tga: tangent-space normal, +X along +u, +Y along +v */
    Material /**< <texture>_m.tga: R roughness, G specular / metal hint, B unused */
};

/** The file a map of a texture is read from: lower-case name plus _n.tga or _m.tga. */
std::string mapFileName(const std::string &texture, MapKind kind);

/**
 * The map file of a texture in the override folder, if there is one (a
 * regular file named by mapFileName). Only ever reads the folder.
 */
std::optional<std::filesystem::path> findMapFile(const std::filesystem::path &folder, const std::string &texture, MapKind kind);

/** Where the override folder is when none is configured: enhanced_textures beside the executable. */
std::filesystem::path defaultFolder(const std::filesystem::path &executablePath);

enum class NormalSource {
    None,      /**< the plain vertex normal */
    Game,      /**< the game's own TXI bump map or MDL bump map */
    Generated, /**< a map from the override folder (or a procedural test pattern) */
};

/**
 * Which normal a surface uses. The game's own bump map always wins, with
 * the feature on or off; a generated map is only used with the feature on.
 */
NormalSource chooseNormalSource(bool featureOn, bool hasGameBumpMap, bool hasGeneratedMap);

/**
 * Per-vertex tangent frames by the standard per-triangle accumulation:
 * each triangle adds its UV-space tangent and bitangent (weighted by its
 * area, as the unnormalised derivatives are) to its three vertices; the
 * sum is then made orthogonal to the vertex normal (Gram-Schmidt), and the
 * bitangent is the cross product with the handedness of the accumulated
 * one, so a mirrored UV island keeps its orientation. A vertex whose
 * triangles have degenerate UVs gets an arbitrary frame around its normal.
 */
struct TangentFrames {
    std::vector<glm::vec3> tangents;
    std::vector<glm::vec3> bitangents;
};

TangentFrames computeTangentFrames(const std::vector<glm::vec3> &positions,
                                   const std::vector<glm::vec3> &normals,
                                   const std::vector<glm::vec2> &uvs,
                                   const std::vector<std::array<uint16_t, 3>> &triangles);

/** Load-time cost of the tangent generation so far: meshes, vertices, microseconds. */
struct TangentStats {
    int meshes {0};
    int64_t vertices {0};
    int64_t micros {0};
};

TangentStats &tangentStats();

/**
 * Procedural test patterns (development only, never final quality): a
 * square RGBA8 image, bricks or bumps chosen by a hash of the texture name,
 * so a capture shows the shading path without texgen output. Rows are bottom
 * to top, as the engine's textures are.
 */
std::vector<uint8_t> proceduralNormalMap(const std::string &texture, int size);
std::vector<uint8_t> proceduralMaterialMap(const std::string &texture, int size);

} // namespace surfacedetail

} // namespace graphics

} // namespace reone
