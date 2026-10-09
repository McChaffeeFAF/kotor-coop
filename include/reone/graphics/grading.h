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

namespace reone {

namespace graphics {

/**
 * Per-planet colour grading (design note graphics_preset, Tier 3). A grade
 * is a few parameters; the renderer bakes it into a 32x32x32 LUT (a
 * 1024x32 strip of blue slices) and samples that. Grades lean neutral: each
 * is blended in at `strength`. No LUT images ship; they are made here.
 */
struct ColorGrade {
    glm::vec3 lift {0.0f}; /**< added to the shadows */
    glm::vec3 gain {1.0f}; /**< multiplies the highlights */
    float gamma {1.0f};    /**< mid-tone power (below 1 brightens) */
    float saturation {1.0f};
    float contrast {1.0f}; /**< about mid grey */
    float strength {1.0f}; /**< blend of the graded colour over the original */
};

constexpr int kGradeLUTSize = 32;

/** The grading profile for a module by its name prefix ("tar_" is "taris"); empty is neutral. */
std::string gradingProfileForModule(const std::string &module);

/** The planet profiles, in a stable order. */
const std::vector<std::string> &gradingProfiles();

/** The grade of a profile; nullptr for an unknown or neutral profile. */
const ColorGrade *colorGrade(const std::string &profile);

/** A display-encoded colour through the grade (strength included). */
glm::vec3 applyColorGrade(const ColorGrade &grade, const glm::vec3 &color);

/** RGB8 strip, kGradeLUTSize^2 wide and kGradeLUTSize high: texel (b * N + r, g). */
std::vector<uint8_t> bakeColorGradeLUT(const ColorGrade &grade);

} // namespace graphics

} // namespace reone
