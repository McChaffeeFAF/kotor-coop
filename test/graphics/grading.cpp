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

#include <gtest/gtest.h>

#include "reone/graphics/grading.h"
#include "reone/graphics/options.h"
#include "reone/graphics/preset.h"

using namespace reone;
using namespace reone::graphics;

TEST(ColorGrading, modules_map_to_planets_by_prefix_and_the_rest_is_neutral) {
    EXPECT_EQ("taris", gradingProfileForModule("tar_m02af"));
    EXPECT_EQ("dantooine", gradingProfileForModule("danm13"));
    EXPECT_EQ("tatooine", gradingProfileForModule("tat_m17aa"));
    EXPECT_EQ("kashyyyk", gradingProfileForModule("kas_m22aa"));
    EXPECT_EQ("manaan", gradingProfileForModule("manm26aa"));
    EXPECT_EQ("korriban", gradingProfileForModule("korr_m33aa"));
    EXPECT_EQ("leviathan", gradingProfileForModule("lev_m40aa"));
    EXPECT_EQ("unknownworld", gradingProfileForModule("unk_m41aa"));
    EXPECT_EQ("starforge", gradingProfileForModule("sta_m45aa"));
    EXPECT_EQ("ebonhawk", gradingProfileForModule("ebo_m12aa"));
    EXPECT_EQ("taris", gradingProfileForModule("TAR_M03AA"));
    EXPECT_EQ("", gradingProfileForModule("end_m01aa"));
    EXPECT_EQ("", gradingProfileForModule(""));
    EXPECT_EQ(nullptr, colorGrade(""));
    EXPECT_EQ(10u, gradingProfiles().size());
    for (const auto &profile : gradingProfiles()) {
        EXPECT_NE(nullptr, colorGrade(profile)) << profile;
    }
}

TEST(ColorGrading, grades_lean_neutral) {
    // No grade moves any colour of the cube by more than a sixth.
    for (const auto &profile : gradingProfiles()) {
        const auto &grade = *colorGrade(profile);
        float worst = 0.0f;
        for (float r = 0.0f; r <= 1.0f; r += 0.25f) {
            for (float g = 0.0f; g <= 1.0f; g += 0.25f) {
                for (float b = 0.0f; b <= 1.0f; b += 0.25f) {
                    glm::vec3 c(r, g, b);
                    glm::vec3 d = glm::abs(applyColorGrade(grade, c) - c);
                    worst = std::max(worst, std::max(d.r, std::max(d.g, d.b)));
                }
            }
        }
        EXPECT_LT(worst, 1.0f / 6.0f) << profile;
        EXPECT_GT(worst, 0.0f) << profile;
    }
}

TEST(ColorGrading, an_identity_grade_bakes_an_identity_lut) {
    ColorGrade identity;
    auto lut = bakeColorGradeLUT(identity);
    const int n = kGradeLUTSize;
    ASSERT_EQ(static_cast<size_t>(3 * n * n * n), lut.size());
    for (int g : {0, 7, n - 1}) {
        for (int b : {0, 13, n - 1}) {
            for (int r : {0, 21, n - 1}) {
                size_t i = 3 * (static_cast<size_t>(g) * n * n + static_cast<size_t>(b) * n + r);
                EXPECT_NEAR(r * 255.0 / (n - 1), lut[i + 0], 0.6);
                EXPECT_NEAR(g * 255.0 / (n - 1), lut[i + 1], 0.6);
                EXPECT_NEAR(b * 255.0 / (n - 1), lut[i + 2], 0.6);
            }
        }
    }
}

TEST(ColorGrading, the_toggle_and_the_profile_override) {
    GraphicsOptions options;
    EXPECT_FALSE(options.grading);
    setGraphicsOption(options, "grading", "1");
    EXPECT_TRUE(options.grading);
    setGraphicsOption(options, "gradeprofile", "korriban");
    EXPECT_EQ("korriban", options.gradingOverride);
    setGraphicsOption(options, "gradeprofile", "neutral");
    setGraphicsOption(options, "gradeprofile", "auto");
    EXPECT_THROW(setGraphicsOption(options, "gradeprofile", "hoth"), std::invalid_argument);
    GraphicsOptions enhanced;
    applyGraphicsPreset(enhanced, GraphicsPreset::Enhanced);
    EXPECT_FALSE(enhanced.grading);
}
