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

#include <array>
#include <fstream>

#include <gtest/gtest.h>

#include "reone/graphics/surfacedetail.h"

using namespace reone::graphics;
using namespace reone::graphics::surfacedetail;

namespace {

void expectVec(const glm::vec3 &expected, const glm::vec3 &actual) {
    EXPECT_NEAR(expected.x, actual.x, 1e-5f);
    EXPECT_NEAR(expected.y, actual.y, 1e-5f);
    EXPECT_NEAR(expected.z, actual.z, 1e-5f);
}

} // namespace

TEST(SurfaceDetailTangents, axis_aligned_triangle_follows_uv_axes) {
    // A triangle in the XY plane whose UVs equal its XY: tangent +X, bitangent +Y.
    std::vector<glm::vec3> positions {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    std::vector<glm::vec3> normals(3, glm::vec3(0, 0, 1));
    std::vector<glm::vec2> uvs {{0, 0}, {1, 0}, {0, 1}};
    auto frames = computeTangentFrames(positions, normals, uvs, {{0, 1, 2}});
    ASSERT_EQ(3u, frames.tangents.size());
    for (int i = 0; i < 3; ++i) {
        expectVec({1, 0, 0}, frames.tangents[i]);
        expectVec({0, 1, 0}, frames.bitangents[i]);
    }
}

TEST(SurfaceDetailTangents, rotated_uvs_rotate_the_frame) {
    // u runs along +Y and v along -X: the tangent is +Y, the bitangent -X.
    std::vector<glm::vec3> positions {{0, 0, 0}, {0, 1, 0}, {-1, 0, 0}};
    std::vector<glm::vec3> normals(3, glm::vec3(0, 0, 1));
    std::vector<glm::vec2> uvs {{0, 0}, {1, 0}, {0, 1}};
    auto frames = computeTangentFrames(positions, normals, uvs, {{0, 1, 2}});
    expectVec({0, 1, 0}, frames.tangents[0]);
    expectVec({-1, 0, 0}, frames.bitangents[0]);
}

TEST(SurfaceDetailTangents, mirrored_uvs_flip_the_bitangent) {
    // v decreases along +Y: a mirrored island keeps T and flips B.
    std::vector<glm::vec3> positions {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    std::vector<glm::vec3> normals(3, glm::vec3(0, 0, 1));
    std::vector<glm::vec2> uvs {{0, 1}, {1, 1}, {0, 0}};
    auto frames = computeTangentFrames(positions, normals, uvs, {{0, 1, 2}});
    expectVec({1, 0, 0}, frames.tangents[0]);
    expectVec({0, -1, 0}, frames.bitangents[0]);
}

TEST(SurfaceDetailTangents, frames_are_orthonormal_to_a_tilted_vertex_normal) {
    std::vector<glm::vec3> positions {{0, 0, 0}, {2, 0, 0}, {0, 3, 0}};
    glm::vec3 n = glm::normalize(glm::vec3(0.3f, 0.2f, 1.0f));
    std::vector<glm::vec3> normals(3, n);
    std::vector<glm::vec2> uvs {{0, 0}, {1, 0}, {0, 1}};
    auto frames = computeTangentFrames(positions, normals, uvs, {{0, 1, 2}});
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(1.0f, glm::length(frames.tangents[i]), 1e-5f);
        EXPECT_NEAR(1.0f, glm::length(frames.bitangents[i]), 1e-5f);
        EXPECT_NEAR(0.0f, glm::dot(frames.tangents[i], n), 1e-5f);
        EXPECT_NEAR(0.0f, glm::dot(frames.bitangents[i], n), 1e-5f);
        EXPECT_GT(frames.tangents[i].x, 0.9f);
    }
}

TEST(SurfaceDetailTangents, shared_vertex_averages_its_triangles) {
    // Two triangles of a quad folded 90 degrees along the shared edge x = 1
    // with u continuing across: the shared vertices' tangent is the average
    // of +X and -Z, made orthogonal to their normal.
    std::vector<glm::vec3> positions {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {1, 0, -1}, {1, 1, -1}};
    glm::vec3 shared = glm::normalize(glm::vec3(1, 0, 1));
    std::vector<glm::vec3> normals {{0, 0, 1}, shared, shared, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}};
    std::vector<glm::vec2> uvs {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {2, 0}, {2, 1}};
    std::vector<std::array<uint16_t, 3>> tris {{0, 1, 2}, {0, 2, 3}, {1, 4, 5}, {1, 5, 2}};
    auto frames = computeTangentFrames(positions, normals, uvs, tris);
    expectVec(glm::normalize(glm::vec3(1, 0, -1)), frames.tangents[1]);
    expectVec({0, 1, 0}, frames.bitangents[1]);
    expectVec({1, 0, 0}, frames.tangents[0]);
    expectVec({0, 0, -1}, frames.tangents[4]);
}

TEST(SurfaceDetailTangents, degenerate_uvs_get_a_valid_frame) {
    std::vector<glm::vec3> positions {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    std::vector<glm::vec3> normals(3, glm::vec3(0, 0, 1));
    std::vector<glm::vec2> uvs(3, glm::vec2(0.5f));
    auto frames = computeTangentFrames(positions, normals, uvs, {{0, 1, 2}});
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(1.0f, glm::length(frames.tangents[i]), 1e-5f);
        EXPECT_NEAR(0.0f, glm::dot(frames.tangents[i], normals[i]), 1e-5f);
        EXPECT_NEAR(0.0f, glm::dot(frames.bitangents[i], normals[i]), 1e-5f);
    }
}

TEST(SurfaceDetailTangents, out_of_range_indices_are_skipped) {
    std::vector<glm::vec3> positions {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    std::vector<glm::vec3> normals(3, glm::vec3(0, 0, 1));
    std::vector<glm::vec2> uvs {{0, 0}, {1, 0}, {0, 1}};
    auto frames = computeTangentFrames(positions, normals, uvs, {{0, 1, 7}, {0, 1, 2}});
    expectVec({1, 0, 0}, frames.tangents[0]);
}

TEST(SurfaceDetailOverride, file_names_are_lower_case_with_suffix) {
    EXPECT_EQ("lsi_floor01_n.tga", mapFileName("LSI_Floor01", MapKind::Normal));
    EXPECT_EQ("lsi_floor01_m.tga", mapFileName("lsi_floor01", MapKind::Material));
}

TEST(SurfaceDetailOverride, default_folder_is_beside_the_executable) {
    auto exe = std::filesystem::path("C:/games/reone/engine.exe");
    EXPECT_EQ(std::filesystem::path("C:/games/reone") / "enhanced_textures", defaultFolder(exe));
}

TEST(SurfaceDetailOverride, game_bump_map_wins_over_generated) {
    EXPECT_EQ(NormalSource::Game, chooseNormalSource(true, true, true));
    EXPECT_EQ(NormalSource::Game, chooseNormalSource(false, true, true));
    EXPECT_EQ(NormalSource::Game, chooseNormalSource(true, true, false));
}

TEST(SurfaceDetailOverride, generated_only_with_the_feature_on) {
    EXPECT_EQ(NormalSource::Generated, chooseNormalSource(true, false, true));
    EXPECT_EQ(NormalSource::None, chooseNormalSource(false, false, true));
    EXPECT_EQ(NormalSource::None, chooseNormalSource(true, false, false));
}

TEST(SurfaceDetailProcedural, patterns_are_deterministic_unit_normals) {
    EXPECT_EQ(proceduralNormalMap("lda_wall01", 64), proceduralNormalMap("LDA_WALL01", 64));
    // Names that land on each of the three patterns.
    for (auto name : {"lda_wall01", "lsi_floor01", "tel_metal02", "end_panel03", "unk_rock01"}) {
        auto a = proceduralNormalMap(name, 256);
        ASSERT_EQ(256u * 256u * 4u, a.size());
        int tilted = 0;
        for (size_t i = 0; i < a.size(); i += 4) {
            glm::vec3 n(a[i] / 127.5f - 1.0f, a[i + 1] / 127.5f - 1.0f, a[i + 2] / 127.5f - 1.0f);
            ASSERT_NEAR(1.0f, glm::length(n), 0.03f) << name;
            ASSERT_GT(n.z, 0.0f) << name;
            tilted += n.z < 0.95f ? 1 : 0;
        }
        EXPECT_GT(tilted, 256 * 256 / 50) << name; // visibly bumpy, not flat
    }
    auto m = proceduralMaterialMap("lda_wall01", 64);
    ASSERT_EQ(64u * 64u * 4u, m.size());
}

TEST(SurfaceDetailOverride, lookup_finds_present_maps_and_misses_cleanly) {
    auto folder = std::filesystem::temp_directory_path() / "reone_surfacedetail_test";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    { std::ofstream(folder / "lsi_floor01_n.tga", std::ios::binary) << "x"; }
    std::filesystem::create_directories(folder / "lsi_wall01_m.tga"); // a directory is not a map

    auto normal = findMapFile(folder, "LSI_Floor01", MapKind::Normal);
    ASSERT_TRUE(normal.has_value());
    EXPECT_EQ(folder / "lsi_floor01_n.tga", *normal);
    EXPECT_FALSE(findMapFile(folder, "lsi_floor01", MapKind::Material).has_value());
    EXPECT_FALSE(findMapFile(folder, "lsi_wall01", MapKind::Material).has_value());
    EXPECT_FALSE(findMapFile(folder / "missing", "lsi_floor01", MapKind::Normal).has_value());
    EXPECT_FALSE(findMapFile("", "lsi_floor01", MapKind::Normal).has_value());
    EXPECT_FALSE(findMapFile(folder, "", MapKind::Normal).has_value());
    std::filesystem::remove_all(folder);
}
