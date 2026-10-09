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

#include "options.h"

namespace reone {

namespace graphics {

/**
 * Graphics presets (design note graphics_scope, Tier 1).
 *
 * Retail is the default look and the defaults of GraphicsOptions. Enhanced
 * turns on the opt-in features: bloom, SMAA (over FXAA), soft shadows (PCSS),
 * GTAO, ACES tone mapping, eye adaptation, soft particles, reflections,
 * lightsaber glow, conversation depth of field and surface detail (normal maps,
 * which need a texgen folder to show anything), and keeps sharpening as it is. Neither touches the shadow-map
 * resolution (4096 maps are a separate opt-in for their VRAM), anisotropic
 * filtering, pbr, ssao or ssr, nor anything outside rendering
 * (design note graphics_preset).
 */
enum class GraphicsPreset {
    Retail,
    Enhanced
};

enum class AntiAliasing {
    None,
    FXAA,
    SMAA,
    TAA
};

/**
 * The Tier 2 on/off features, one row each (design note graphics_preset):
 * the `gfx` name (also the command-line and reone.cfg key), the check box on
 * Options > Graphics > Advanced (stored as optgraphicsadv.<tag>), and whether
 * Enhanced turns it on. Every one defaults to off.
 */
struct GraphicsFeatureToggle {
    const char *name;
    const char *settingTag;
    const char *label;
    const char *help;
    bool GraphicsOptions::*field;
    bool enhanced;
};

const std::vector<GraphicsFeatureToggle> &graphicsFeatureToggles();

void applyGraphicsPreset(GraphicsOptions &options, GraphicsPreset preset);

/** True when every preset-controlled setting has the preset's value. */
bool matchesGraphicsPreset(const GraphicsOptions &options, GraphicsPreset preset);

/** The anti-aliasing in effect: TAA over SMAA over FXAA. */
AntiAliasing antiAliasing(const GraphicsOptions &options);
void setAntiAliasing(GraphicsOptions &options, AntiAliasing mode);

std::optional<AntiAliasing> parseAntiAliasing(const std::string &name);

/** Tone-mapping operators: "aces" (1) and "agx" (2). */
std::optional<int> parseToneMapOperator(const std::string &name);
std::string toneMapOperatorName(int op);
std::string antiAliasingName(AntiAliasing mode);

/**
 * Set one rendering option by name, as the `gfx` console command and tests
 * do: preset, bloom, aa, fxaa, smaa, taa, softshadows, shadowres, gtao,
 * anisotropy, sharpen, sharpenamount, bloomintensity, normalmapstrength (0-1),
 * normalmaptest (procedural test maps) and every toggle row. Throws
 * std::invalid_argument on an unknown name or a bad value.
 */
void setGraphicsOption(GraphicsOptions &options, const std::string &name, const std::string &value);

/** The preset-related settings as one line of JSON. */
std::string graphicsOptionsJson(const GraphicsOptions &options);

} // namespace graphics

} // namespace reone
