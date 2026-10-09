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

#include "types.h"

namespace reone {

namespace graphics {

struct GraphicsOptions {
    int width {1024};
    int height {768};
    int winScale {100};
    /** Exclusive fullscreen: the display switches to the closest mode to width x height. */
    bool fullscreen {false};
    /**
     * Fullscreen windowed: a borderless window covering the desktop at its own
     * resolution. Takes precedence over fullscreen.
     */
    bool borderless {false};
    /** Hide the presentation window while retaining a renderable surface. */
    bool headless {false};
    /** Draw 3D scene content: the world and scene-backed GUI panels. */
    bool sceneRender {true};
    bool vsync {true};
    bool grass {true};
    /**
     * Draw the shadow of the selected shadow light, or none at all.
     *
     * Off means the scene has no shadow light for the frame: no shadow pass
     * runs and no shadow term reaches the uniforms, rather than a pass that
     * renders and resolves to nothing. It exists so a comparison against
     * another build can exclude a subsystem whose two implementations are
     * known to differ, and it must therefore mean the same thing in both
     * builds - a switch that disables slightly different work on each side
     * measures itself.
     */
    bool shadows {true};
    /** Admit emitter particles, or leave them out of the frame entirely. */
    bool particles {true};
    bool pbr {false};
    bool ssao {false};
    bool ssr {false};
    bool fxaa {true};
    bool sharpen {true};
    // Enhanced preset (reone/graphics/preset.h). Every default is the
    // retail look: with all of these off a frame is what it was before.
    /** Bloom: bright parts of the frame and the glow channel, blurred over a mip chain. */
    bool bloom {false};
    float bloomIntensity {0.6f};
    /** Morphological anti-aliasing (SMAA 1x style); takes precedence over fxaa. */
    bool smaa {false};
    /** Temporal anti-aliasing with a jittered projection; takes precedence over smaa. */
    bool taa {false};
    /** Percentage-closer soft shadows (blocker search, variable penumbra). */
    bool softShadows {false};
    /** Ground-truth ambient occlusion; in the PBR pipeline it replaces ssao. */
    bool gtao {false};
    float sharpenAmount {0.25f};
    // Tier 2 (graphicsFeatureToggles() in reone/graphics/preset.h).
    /**
     * ReShade-friendly mode: the final scene depth is written to the
     * window's depth buffer, and the GUI is drawn after a "reone.ui" debug
     * group starts, with depth writes off, so a player-installed ReShade
     * gets clean depth. Nothing is bundled.
     */
    bool reshade {false};
    /**
     * Tone mapping: the scene is drawn into half-float targets, lighting may
     * pass 1.0 (up to 2.0), and a filmic curve (tonemapOperator) maps the
     * result to the display.
     */
    bool tonemap {false};
    /** 1: ACES (filmic fit), 2: AgX. */
    int tonemapOperator {1};
    /** Eye adaptation: a metered, clamped and eased exposure (also without tone mapping). */
    bool autoExposure {false};
    /**
     * Volumetric light: a half-resolution ray march through a thin medium
     * lit by the scene lights and the sun (shadowed by the shadow light),
     * denser and tinted where the area has fog.
     */
    bool volumetrics {false};
    /** Soft particles: particle edges fade where they meet geometry. */
    bool softParticles {false};
    /** Screen-space GI: one bounce of light from nearby surfaces, gathered from the opaque frame. */
    bool ssgi {false};
    /**
     * Reflections: screen-space reflections on env-mapped (reflective)
     * surfaces in the retro pipeline, falling back to a static reflection
     * probe captured when an area is entered.
     */
    bool reflections {false};
    /** Per-planet colour grading: a small neutral-leaning LUT per planet, by module prefix. */
    bool grading {false};
    /**
     * Lightsaber glow and trails: a whiter blade core with a stronger halo
     * in the blade's colour, and a short glowing arc behind a swing (past
     * blade positions drawn into the glow buffer). Rendering only.
     */
    bool saberFx {false};
    /** Conversation depth of field: the speaker sharp, the background softly blurred. */
    bool dof {false};
    /** Camera motion blur: the frame blurred along each pixel's screen motion since the last frame. */
    bool motionBlur {false};
    // Lens effects, each its own toggle (one pass after anti-aliasing).
    bool lensFlares {false};         /**< lens ghosts and a halo from bright parts of the frame */
    bool filmGrain {false};
    bool vignette {false};
    bool chromaticAberration {false};
    // Tier 4 (design note graphics_preset, "Tier 4: surface detail").
    /**
     * Surface detail: generated normal and material maps (<texture>_n.tga,
     * <texture>_m.tga) from normalMapFolder, on meshes without a game bump
     * map. Nothing changes while the folder is empty.
     */
    bool normalMaps {false};
    /** How far the generated normal and material maps act, 0 to 1. */
    float normalMapStrength {1.0f};
    /** The override folder; empty is enhanced_textures beside the executable. */
    std::string normalMapFolder;
    /** Development: procedural test patterns instead of the folder (not final quality). */
    bool normalMapTest {false};
    /** Presentation state, set by the game each frame: focus distance (m) and blur amount (0-1). */
    float dofFocus {0.0f};
    float dofAmount {0.0f};
    /**
     * Presentation state, not a setting: the grading profile of the current
     * module (set by the game each frame), and a `gfx gradeprofile` override
     * for captures ("auto" follows the module).
     */
    std::string gradingProfile;
    std::string gradingOverride {"auto"};
    TextureQuality textureQuality {TextureQuality::High};
    int shadowResolution {2048};
    /** log2 of the anisotropy: 0 is off, 2 is 4x, 4 is 16x. */
    int anisotropicFiltering {2};
    float drawDistance {kDefaultObjectDrawDistance};
    float guiScale {1.0f};
    float guiTextScale {0.5f};
    float guiDialogTextScale {0.6f};
    float guiBorderScale {1.0f};
    float guiListScale {0.5f};
};

} // namespace graphics

} // namespace reone
