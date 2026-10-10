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

#include "reone/graphics/preset.h"

#include "reone/graphics/grading.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace reone {

namespace graphics {

namespace {

// What a preset sets. Shadow-map resolution and anisotropic filtering are
// separate choices: 4096 shadow maps cost VRAM, and neither preset touches
// them.
struct PresetValues {
    bool bloom;
    AntiAliasing aa;
    bool softShadows;
    bool gtao;
    bool sharpen;
    float sharpenAmount;
};

PresetValues presetValues(GraphicsPreset preset) {
    GraphicsOptions defaults;
    if (preset == GraphicsPreset::Enhanced) {
        return PresetValues {true, AntiAliasing::SMAA, true, true, defaults.sharpen, defaults.sharpenAmount};
    }
    return PresetValues {defaults.bloom, antiAliasing(defaults), defaults.softShadows, defaults.gtao,
                         defaults.sharpen, defaults.sharpenAmount};
}

bool parseBool(const std::string &value) {
    if (value == "1" || value == "on" || value == "true") {
        return true;
    }
    if (value == "0" || value == "off" || value == "false") {
        return false;
    }
    throw std::invalid_argument("expected 0 or 1, got '" + value + "'");
}

int parseInt(const std::string &value, int lo, int hi) {
    size_t used = 0;
    int result = 0;
    try {
        result = std::stoi(value, &used);
    } catch (const std::exception &) {
        used = 0;
    }
    if (used != value.size() || value.empty() || result < lo || result > hi) {
        throw std::invalid_argument("expected an integer in [" + std::to_string(lo) + ", " + std::to_string(hi) + "], got '" + value + "'");
    }
    return result;
}

float parseFloat(const std::string &value, float lo, float hi) {
    size_t used = 0;
    float result = 0.0f;
    try {
        result = std::stof(value, &used);
    } catch (const std::exception &) {
        used = 0;
    }
    if (used != value.size() || value.empty() || !std::isfinite(result) || result < lo || result > hi) {
        throw std::invalid_argument("expected a number in [" + std::to_string(lo) + ", " + std::to_string(hi) + "], got '" + value + "'");
    }
    return result;
}

} // namespace

const std::vector<GraphicsFeatureToggle> &graphicsFeatureToggles() {
    static const std::vector<GraphicsFeatureToggle> toggles {
        {"reshade", "CB_RESHADE", "ReShade Depth",
         "ReShade-friendly mode: the scene depth stays in the window's depth buffer and the GUI is drawn after a debug-group hook without touching depth",
         &GraphicsOptions::reshade, false},
        {"tonemap", "CB_TONEMAP", "Tone Mapping",
         "tone mapping: half-float scene targets, lighting past 1.0, and a filmic curve (tonemap-op aces or agx)",
         &GraphicsOptions::tonemap, true},
        {"autoexposure", "CB_AUTOEXPOSURE", "Eye Adaptation",
         "auto exposure: the frame is metered and the exposure eases toward a clamped target",
         &GraphicsOptions::autoExposure, true},
        {"volumetrics", "CB_VOLUMETRICS", "Volumetric Light",
         "volumetric light: light shafts and haze from the scene lights and the sun, denser where the area has fog",
         &GraphicsOptions::volumetrics, false},
        {"softparticles", "CB_SOFTPARTICLES", "Soft Particles",
         "soft particles: particle edges fade out where they meet walls and floors",
         &GraphicsOptions::softParticles, true},
        {"ssgi", "CB_SSGI", "Indirect Light",
         "screen-space global illumination: one bounce of coloured light from nearby surfaces",
         &GraphicsOptions::ssgi, false},
        {"reflections", "CB_REFLECTIONS", "Reflections",
         "screen-space reflections on reflective surfaces, with a reflection probe captured on entering an area",
         &GraphicsOptions::reflections, true},
        {"grading", "CB_GRADING", "Planet Colour Grading",
         "per-planet colour grading: a small, neutral-leaning look per planet, picked by module",
         &GraphicsOptions::grading, false},
        {"saberfx", "CB_SABERFX", "Lightsaber Glow",
         "lightsaber glow and trails: core-and-halo blades and a short glowing arc behind swings",
         &GraphicsOptions::saberFx, true},
        {"dof", "CB_DOF", "Conversation Depth of Field",
         "depth of field in conversations: focus on the speaker, a soft background blur that fades with the dialog camera",
         &GraphicsOptions::dof, true},
        {"motionblur", "CB_MOTIONBLUR", "Motion Blur",
         "camera motion blur: the frame blurred along its screen motion since the last frame",
         &GraphicsOptions::motionBlur, false},
        {"lensflares", "CB_LENSFLARES", "Lens Ghosts",
         "lens effects: faint lens ghosts and a halo from the brightest parts of the frame",
         &GraphicsOptions::lensFlares, false},
        {"filmgrain", "CB_FILMGRAIN", "Film Grain", "lens effects: fine film grain", &GraphicsOptions::filmGrain, false},
        {"vignette", "CB_VIGNETTE", "Vignette", "lens effects: soft darkening of the corners", &GraphicsOptions::vignette, false},
        {"chromaticaberration", "CB_CHROMATIC", "Chromatic Aberration",
         "lens effects: a slight red/blue split toward the screen edges", &GraphicsOptions::chromaticAberration, false},
        {"normalmaps", "CB_NORMALMAPS", "Surface Detail",
         "surface detail: generated normal and material maps (DDS or TGA) from the enhanced_textures folder (texgen); no effect while the folder is empty",
         &GraphicsOptions::normalMaps, true},
    };
    return toggles;
}

std::optional<int> parseToneMapOperator(const std::string &name) {
    if (name == "aces" || name == "1") return 1;
    if (name == "agx" || name == "2") return 2;
    return std::nullopt;
}

std::string toneMapOperatorName(int op) {
    return op == 2 ? "agx" : "aces";
}

AntiAliasing antiAliasing(const GraphicsOptions &options) {
    if (options.taa) {
        return AntiAliasing::TAA;
    }
    if (options.smaa) {
        return AntiAliasing::SMAA;
    }
    return options.fxaa ? AntiAliasing::FXAA : AntiAliasing::None;
}

void setAntiAliasing(GraphicsOptions &options, AntiAliasing mode) {
    options.fxaa = mode == AntiAliasing::FXAA;
    options.smaa = mode == AntiAliasing::SMAA;
    options.taa = mode == AntiAliasing::TAA;
}

std::optional<AntiAliasing> parseAntiAliasing(const std::string &name) {
    if (name == "none" || name == "off" || name == "0") return AntiAliasing::None;
    if (name == "fxaa") return AntiAliasing::FXAA;
    if (name == "smaa") return AntiAliasing::SMAA;
    if (name == "taa") return AntiAliasing::TAA;
    return std::nullopt;
}

std::string antiAliasingName(AntiAliasing mode) {
    switch (mode) {
    case AntiAliasing::FXAA:
        return "fxaa";
    case AntiAliasing::SMAA:
        return "smaa";
    case AntiAliasing::TAA:
        return "taa";
    default:
        return "none";
    }
}

void applyGraphicsPreset(GraphicsOptions &options, GraphicsPreset preset) {
    auto values = presetValues(preset);
    options.bloom = values.bloom;
    setAntiAliasing(options, values.aa);
    options.softShadows = values.softShadows;
    options.gtao = values.gtao;
    options.sharpen = values.sharpen;
    options.sharpenAmount = values.sharpenAmount;
    for (const auto &toggle : graphicsFeatureToggles()) {
        options.*toggle.field = preset == GraphicsPreset::Enhanced && toggle.enhanced;
    }
    if (preset == GraphicsPreset::Enhanced) {
        options.tonemapOperator = 1; // Enhanced tone-maps with ACES; AgX stays an option
    }
}

bool matchesGraphicsPreset(const GraphicsOptions &options, GraphicsPreset preset) {
    auto values = presetValues(preset);
    return options.bloom == values.bloom &&
           antiAliasing(options) == values.aa &&
           options.softShadows == values.softShadows &&
           options.gtao == values.gtao &&
           options.sharpen == values.sharpen &&
           options.sharpenAmount == values.sharpenAmount &&
           std::all_of(graphicsFeatureToggles().begin(), graphicsFeatureToggles().end(), [&](const auto &toggle) {
               return options.*toggle.field == (preset == GraphicsPreset::Enhanced && toggle.enhanced);
           });
}

void setGraphicsOption(GraphicsOptions &options, const std::string &name, const std::string &value) {
    if (name == "preset") {
        if (value == "retail") {
            applyGraphicsPreset(options, GraphicsPreset::Retail);
        } else if (value == "enhanced") {
            applyGraphicsPreset(options, GraphicsPreset::Enhanced);
        } else {
            throw std::invalid_argument("preset is retail or enhanced, got '" + value + "'");
        }
    } else if (name == "aa") {
        auto mode = parseAntiAliasing(value);
        if (!mode) {
            throw std::invalid_argument("aa is none, fxaa, smaa or taa, got '" + value + "'");
        }
        setAntiAliasing(options, *mode);
    } else if (name == "bloom") {
        options.bloom = parseBool(value);
    } else if (name == "fxaa") {
        options.fxaa = parseBool(value);
    } else if (name == "smaa") {
        options.smaa = parseBool(value);
    } else if (name == "taa") {
        options.taa = parseBool(value);
    } else if (name == "softshadows") {
        options.softShadows = parseBool(value);
    } else if (name == "shadowres") {
        int resolution = parseInt(value, 512, 8192);
        if ((resolution & (resolution - 1)) != 0) {
            throw std::invalid_argument("shadowres is a power of two, got '" + value + "'");
        }
        options.shadowResolution = resolution;
    } else if (name == "gtao") {
        options.gtao = parseBool(value);
    } else if (name == "anisotropy") {
        options.anisotropicFiltering = parseInt(value, 0, 4);
    } else if (name == "sharpen") {
        options.sharpen = parseBool(value);
    } else if (name == "sharpenamount") {
        options.sharpenAmount = parseFloat(value, 0.0f, 2.0f);
    } else if (name == "tonemapop") {
        auto op = parseToneMapOperator(value);
        if (!op) {
            throw std::invalid_argument("tonemapop is aces or agx, got '" + value + "'");
        }
        options.tonemapOperator = *op;
    } else if (name == "gradeprofile") {
        if (value != "auto" && value != "neutral" && !colorGrade(value)) {
            throw std::invalid_argument("gradeprofile is auto, neutral or a planet profile, got '" + value + "'");
        }
        options.gradingOverride = value;
    } else if (name == "normalmapstrength") {
        options.normalMapStrength = parseFloat(value, 0.0f, 1.0f);
    } else if (name == "normalmaptest") {
        options.normalMapTest = parseBool(value);
    } else if (name == "bloomintensity") {
        options.bloomIntensity = parseFloat(value, 0.0f, 4.0f);
    } else {
        for (const auto &toggle : graphicsFeatureToggles()) {
            if (name == toggle.name) {
                options.*toggle.field = parseBool(value);
                return;
            }
        }
        throw std::invalid_argument("unknown graphics option '" + name + "'");
    }
}

std::string graphicsOptionsJson(const GraphicsOptions &options) {
    std::ostringstream out;
    const char *preset = matchesGraphicsPreset(options, GraphicsPreset::Enhanced)
                             ? "enhanced"
                             : (matchesGraphicsPreset(options, GraphicsPreset::Retail) ? "retail" : "custom");
    out << "{\"preset\":\"" << preset << "\""
        << ",\"pbr\":" << (options.pbr ? 1 : 0)
        << ",\"bloom\":" << (options.bloom ? 1 : 0)
        << ",\"bloomintensity\":" << options.bloomIntensity
        << ",\"aa\":\"" << antiAliasingName(antiAliasing(options)) << "\""
        << ",\"softshadows\":" << (options.softShadows ? 1 : 0)
        << ",\"shadowres\":" << options.shadowResolution
        << ",\"gtao\":" << (options.gtao ? 1 : 0)
        << ",\"ssao\":" << (options.ssao ? 1 : 0)
        << ",\"anisotropy\":" << options.anisotropicFiltering
        << ",\"sharpen\":" << (options.sharpen ? 1 : 0)
        << ",\"sharpenamount\":" << options.sharpenAmount;
    out << ",\"tonemapop\":\"" << toneMapOperatorName(options.tonemapOperator) << "\"";
    out << ",\"gradeprofile\":\"" << options.gradingOverride << "\"";
    out << ",\"normalmapstrength\":" << options.normalMapStrength
        << ",\"normalmaptest\":" << (options.normalMapTest ? 1 : 0);
    for (const auto &toggle : graphicsFeatureToggles()) {
        out << ",\"" << toggle.name << "\":" << (options.*toggle.field ? 1 : 0);
    }
    out << "}";
    return out.str();
}

} // namespace graphics

} // namespace reone
