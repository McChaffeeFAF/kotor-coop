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

#include "reone/graphics/grading.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace reone {

namespace graphics {

namespace {

struct Prefix {
    const char *prefix;
    const char *profile;
};

// K1 module names: tar_m02aa, danm13, tat_m17aa, kas_m22aa, manm26aa,
// korr_m33aa, lev_m40aa, unk_m41aa, sta_m45aa, ebo_m12aa. The Endar Spire
// (end_) and anything else stay neutral.
const Prefix kPrefixes[] {
    {"tar_", "taris"},
    {"danm", "dantooine"},
    {"tat_", "tatooine"},
    {"kas_", "kashyyyk"},
    {"manm", "manaan"},
    {"korr", "korriban"},
    {"lev_", "leviathan"},
    {"unk_", "unknownworld"},
    {"sta_", "starforge"},
    {"ebo_", "ebonhawk"},
};

// Small, neutral-leaning grades: a hint of each planet's light, never a
// filter over it. {lift}, {gain}, gamma, saturation, contrast, strength.
const std::map<std::string, ColorGrade> &grades() {
    static const std::map<std::string, ColorGrade> table {
        // Taris: grey-blue city haze, slightly muted.
        {"taris", {{0.000f, 0.006f, 0.014f}, {0.97f, 1.00f, 1.04f}, 1.00f, 0.92f, 1.04f, 0.7f}},
        // Dantooine: soft warm-green afternoon.
        {"dantooine", {{0.006f, 0.008f, 0.000f}, {1.03f, 1.02f, 0.95f}, 0.97f, 1.04f, 1.00f, 0.7f}},
        // Tatooine: hot, dusty orange, lifted shadows.
        {"tatooine", {{0.018f, 0.010f, 0.000f}, {1.05f, 1.00f, 0.92f}, 0.97f, 0.96f, 0.98f, 0.7f}},
        // Kashyyyk: deep forest green, a touch darker in the mids.
        {"kashyyyk", {{0.000f, 0.008f, 0.002f}, {0.98f, 1.03f, 0.96f}, 1.04f, 1.03f, 1.03f, 0.7f}},
        // Manaan: clean ocean cyan.
        {"manaan", {{0.000f, 0.008f, 0.016f}, {0.96f, 1.01f, 1.05f}, 1.00f, 1.03f, 1.03f, 0.7f}},
        // Korriban: red dust, harsh contrast, drained colour.
        {"korriban", {{0.014f, 0.004f, 0.000f}, {1.06f, 0.98f, 0.92f}, 1.02f, 0.88f, 1.08f, 0.7f}},
        // Leviathan: cold steel, desaturated.
        {"leviathan", {{0.000f, 0.004f, 0.012f}, {0.96f, 0.99f, 1.04f}, 1.02f, 0.85f, 1.05f, 0.7f}},
        // Unknown World (Rakata Prime): golden beach light, teal shadows.
        {"unknownworld", {{0.000f, 0.010f, 0.012f}, {1.05f, 1.02f, 0.95f}, 0.98f, 1.04f, 1.02f, 0.7f}},
        // Star Forge: cold violet-blue, high contrast.
        {"starforge", {{0.008f, 0.000f, 0.018f}, {0.98f, 0.96f, 1.05f}, 1.02f, 0.92f, 1.08f, 0.7f}},
        // Ebon Hawk: warm cabin lights.
        {"ebonhawk", {{0.006f, 0.004f, 0.000f}, {1.03f, 1.01f, 0.97f}, 1.00f, 1.00f, 1.02f, 0.6f}},
    };
    return table;
}

} // namespace

std::string gradingProfileForModule(const std::string &module) {
    std::string lower(module);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto &entry : kPrefixes) {
        if (lower.rfind(entry.prefix, 0) == 0) {
            return entry.profile;
        }
    }
    return "";
}

const std::vector<std::string> &gradingProfiles() {
    static const std::vector<std::string> profiles = []() {
        std::vector<std::string> out;
        for (const auto &entry : kPrefixes) {
            out.push_back(entry.profile);
        }
        return out;
    }();
    return profiles;
}

const ColorGrade *colorGrade(const std::string &profile) {
    auto it = grades().find(profile);
    return it != grades().end() ? &it->second : nullptr;
}

glm::vec3 applyColorGrade(const ColorGrade &grade, const glm::vec3 &color) {
    glm::vec3 c = glm::clamp(color, 0.0f, 1.0f);
    // Lift the shadows, scale the highlights: lift + c (gain - lift).
    glm::vec3 g = grade.lift + c * (grade.gain - grade.lift);
    g = glm::pow(glm::max(g, glm::vec3(0.0f)), glm::vec3(grade.gamma));
    g = (g - 0.5f) * grade.contrast + 0.5f;
    float luma = glm::dot(g, glm::vec3(0.2126f, 0.7152f, 0.0722f));
    g = glm::mix(glm::vec3(luma), g, grade.saturation);
    g = glm::clamp(g, 0.0f, 1.0f);
    return glm::mix(c, g, grade.strength);
}

std::vector<uint8_t> bakeColorGradeLUT(const ColorGrade &grade) {
    const int n = kGradeLUTSize;
    std::vector<uint8_t> pixels(static_cast<size_t>(3) * n * n * n);
    for (int g = 0; g < n; ++g) {
        for (int b = 0; b < n; ++b) {
            for (int r = 0; r < n; ++r) {
                glm::vec3 in(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b));
                glm::vec3 out = applyColorGrade(grade, in / static_cast<float>(n - 1));
                size_t i = 3 * (static_cast<size_t>(g) * n * n + static_cast<size_t>(b) * n + r);
                pixels[i + 0] = static_cast<uint8_t>(out.r * 255.0f + 0.5f);
                pixels[i + 1] = static_cast<uint8_t>(out.g * 255.0f + 0.5f);
                pixels[i + 2] = static_cast<uint8_t>(out.b * 255.0f + 0.5f);
            }
        }
    }
    return pixels;
}

} // namespace graphics

} // namespace reone
