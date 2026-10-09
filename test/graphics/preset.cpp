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

#include <set>

#include "reone/graphics/options.h"
#include "reone/graphics/preset.h"
#include "reone/scene/render/pipeline.h"

using namespace reone;
using namespace reone::graphics;

TEST(GraphicsPreset, defaults_are_the_retail_preset_with_every_enhanced_feature_off) {
    GraphicsOptions options;
    EXPECT_TRUE(matchesGraphicsPreset(options, GraphicsPreset::Retail));
    EXPECT_FALSE(matchesGraphicsPreset(options, GraphicsPreset::Enhanced));
    EXPECT_FALSE(options.bloom);
    EXPECT_FALSE(options.smaa);
    EXPECT_FALSE(options.taa);
    EXPECT_FALSE(options.softShadows);
    EXPECT_FALSE(options.gtao);
    EXPECT_EQ(AntiAliasing::FXAA, antiAliasing(options));
    EXPECT_EQ(2048, options.shadowResolution);
    EXPECT_EQ(2, options.anisotropicFiltering);
}

TEST(GraphicsPreset, enhanced_turns_on_bloom_smaa_soft_shadows_and_gtao_and_retail_turns_them_back) {
    GraphicsOptions options;
    options.pbr = true; // not the preset's business
    applyGraphicsPreset(options, GraphicsPreset::Enhanced);
    EXPECT_TRUE(matchesGraphicsPreset(options, GraphicsPreset::Enhanced));
    EXPECT_FALSE(matchesGraphicsPreset(options, GraphicsPreset::Retail));
    EXPECT_TRUE(options.bloom);
    EXPECT_EQ(AntiAliasing::SMAA, antiAliasing(options));
    EXPECT_TRUE(options.smaa);
    EXPECT_FALSE(options.taa);
    EXPECT_FALSE(options.fxaa);
    EXPECT_TRUE(options.softShadows);
    EXPECT_TRUE(options.gtao);
    EXPECT_TRUE(options.sharpen);
    EXPECT_FLOAT_EQ(0.25f, options.sharpenAmount);
    EXPECT_TRUE(options.pbr);
    EXPECT_FALSE(options.ssao);
    EXPECT_FALSE(options.ssr);

    applyGraphicsPreset(options, GraphicsPreset::Retail);
    GraphicsOptions defaults;
    defaults.pbr = true;
    EXPECT_TRUE(matchesGraphicsPreset(options, GraphicsPreset::Retail));
    EXPECT_EQ(scene::renderPipelineKey(defaults), scene::renderPipelineKey(options));
}

TEST(GraphicsPreset, shadow_resolution_and_anisotropy_are_separate_from_the_presets) {
    GraphicsOptions options;
    applyGraphicsPreset(options, GraphicsPreset::Enhanced);
    EXPECT_EQ(2048, options.shadowResolution); // 4096 maps are their own opt-in (VRAM)
    EXPECT_EQ(2, options.anisotropicFiltering);

    options.shadowResolution = 4096;
    options.anisotropicFiltering = 4;
    EXPECT_TRUE(matchesGraphicsPreset(options, GraphicsPreset::Enhanced));
    applyGraphicsPreset(options, GraphicsPreset::Retail);
    EXPECT_EQ(4096, options.shadowResolution);
    EXPECT_EQ(4, options.anisotropicFiltering);
    EXPECT_TRUE(matchesGraphicsPreset(options, GraphicsPreset::Retail));
}

TEST(GraphicsPreset, one_changed_feature_makes_the_options_custom) {
    GraphicsOptions options;
    applyGraphicsPreset(options, GraphicsPreset::Enhanced);
    setGraphicsOption(options, "aa", "taa");
    EXPECT_FALSE(matchesGraphicsPreset(options, GraphicsPreset::Enhanced));
    EXPECT_FALSE(matchesGraphicsPreset(options, GraphicsPreset::Retail));
    EXPECT_NE(std::string::npos, graphicsOptionsJson(options).find("\"preset\":\"custom\""));
    setGraphicsOption(options, "preset", "enhanced");
    EXPECT_NE(std::string::npos, graphicsOptionsJson(options).find("\"preset\":\"enhanced\""));
}

TEST(GraphicsPreset, anti_aliasing_modes_are_exclusive_with_taa_over_smaa_over_fxaa) {
    GraphicsOptions options;
    options.smaa = true;
    EXPECT_EQ(AntiAliasing::SMAA, antiAliasing(options));
    options.taa = true;
    EXPECT_EQ(AntiAliasing::TAA, antiAliasing(options));
    setAntiAliasing(options, AntiAliasing::None);
    EXPECT_FALSE(options.fxaa || options.smaa || options.taa);
    EXPECT_EQ(AntiAliasing::TAA, parseAntiAliasing("taa"));
    EXPECT_FALSE(parseAntiAliasing("msaa").has_value());
}

TEST(GraphicsPreset, set_by_name_checks_values) {
    GraphicsOptions options;
    setGraphicsOption(options, "bloom", "1");
    setGraphicsOption(options, "aa", "taa");
    setGraphicsOption(options, "shadowres", "4096");
    setGraphicsOption(options, "anisotropy", "4");
    setGraphicsOption(options, "sharpenamount", "0.5");
    EXPECT_TRUE(options.bloom);
    EXPECT_TRUE(options.taa);
    EXPECT_EQ(4096, options.shadowResolution);
    EXPECT_EQ(4, options.anisotropicFiltering);
    EXPECT_FLOAT_EQ(0.5f, options.sharpenAmount);
    EXPECT_THROW(setGraphicsOption(options, "shadowres", "3000"), std::invalid_argument);
    EXPECT_THROW(setGraphicsOption(options, "anisotropy", "9"), std::invalid_argument);
    EXPECT_THROW(setGraphicsOption(options, "bloom", "maybe"), std::invalid_argument);
    EXPECT_THROW(setGraphicsOption(options, "raytracing", "1"), std::invalid_argument);
    setGraphicsOption(options, "preset", "retail");
    EXPECT_TRUE(matchesGraphicsPreset(options, GraphicsPreset::Retail));
}

TEST(GraphicsPreset, pipeline_key_changes_only_with_options_that_change_render_targets) {
    GraphicsOptions options;
    auto key = scene::renderPipelineKey(options);
    options.softShadows = true;
    options.sharpenAmount = 1.0f;
    options.anisotropicFiltering = 4;
    EXPECT_EQ(key, scene::renderPipelineKey(options));
    for (auto change : {"bloom", "smaa", "taa", "gtao"}) {
        GraphicsOptions changed;
        setGraphicsOption(changed, change, "1");
        EXPECT_NE(key, scene::renderPipelineKey(changed)) << change;
    }
    options.shadowResolution = 4096;
    EXPECT_NE(key, scene::renderPipelineKey(options));
}

// Tier 2: every feature toggle is off by default and by Retail, Enhanced
// sets exactly its chosen ones, and each is reachable by name and in JSON.
TEST(GraphicsTier2, feature_toggles_default_off_and_follow_the_presets) {
    ASSERT_FALSE(graphicsFeatureToggles().empty());
    GraphicsOptions options;
    for (const auto &toggle : graphicsFeatureToggles()) {
        EXPECT_FALSE(options.*toggle.field) << toggle.name;
    }
    applyGraphicsPreset(options, GraphicsPreset::Enhanced);
    for (const auto &toggle : graphicsFeatureToggles()) {
        EXPECT_EQ(toggle.enhanced, options.*toggle.field) << toggle.name;
    }
    applyGraphicsPreset(options, GraphicsPreset::Retail);
    for (const auto &toggle : graphicsFeatureToggles()) {
        EXPECT_FALSE(options.*toggle.field) << toggle.name;
    }
}

TEST(GraphicsTier2, feature_toggles_set_by_name_and_show_in_json) {
    std::set<std::string> names;
    std::set<std::string> tags;
    for (const auto &toggle : graphicsFeatureToggles()) {
        EXPECT_TRUE(names.insert(toggle.name).second) << toggle.name;
        EXPECT_TRUE(tags.insert(toggle.settingTag).second) << toggle.settingTag;
        EXPECT_EQ(0u, std::string(toggle.settingTag).rfind("CB_", 0)) << toggle.settingTag;
        GraphicsOptions options;
        setGraphicsOption(options, toggle.name, "1");
        EXPECT_TRUE(options.*toggle.field) << toggle.name;
        EXPECT_NE(std::string::npos, graphicsOptionsJson(options).find(std::string("\"") + toggle.name + "\":1")) << toggle.name;
        // A feature Enhanced leaves off makes Enhanced custom when on.
        applyGraphicsPreset(options, GraphicsPreset::Enhanced);
        options.*toggle.field = !toggle.enhanced;
        EXPECT_FALSE(matchesGraphicsPreset(options, GraphicsPreset::Enhanced)) << toggle.name;
        setGraphicsOption(options, toggle.name, "off");
        EXPECT_FALSE(options.*toggle.field) << toggle.name;
        EXPECT_THROW(setGraphicsOption(options, toggle.name, "maybe"), std::invalid_argument);
    }
}

TEST(GraphicsTier2, reshade_mode_keeps_a_depth_texture_and_is_not_part_of_enhanced) {
    GraphicsOptions options;
    EXPECT_FALSE(scene::sceneDepthTextureNeeded(options));
    auto key = scene::renderPipelineKey(options);
    setGraphicsOption(options, "reshade", "1");
    EXPECT_TRUE(options.reshade);
    EXPECT_TRUE(scene::sceneDepthTextureNeeded(options));
    EXPECT_NE(key, scene::renderPipelineKey(options));
    applyGraphicsPreset(options, GraphicsPreset::Enhanced);
    EXPECT_FALSE(options.reshade);
}

TEST(GraphicsTier2, tone_mapping_and_auto_exposure_use_half_float_targets) {
    GraphicsOptions options;
    EXPECT_FALSE(scene::hdrSceneTargets(options));
    EXPECT_EQ(1, options.tonemapOperator); // ACES
    auto key = scene::renderPipelineKey(options);
    for (auto name : {"tonemap", "autoexposure"}) {
        GraphicsOptions changed;
        setGraphicsOption(changed, name, "1");
        EXPECT_TRUE(scene::hdrSceneTargets(changed)) << name;
        EXPECT_NE(key, scene::renderPipelineKey(changed)) << name;
    }
    // The curve is read per frame: no new pipeline.
    setGraphicsOption(options, "tonemapop", "agx");
    EXPECT_EQ(2, options.tonemapOperator);
    EXPECT_EQ(key, scene::renderPipelineKey(options));
    EXPECT_NE(std::string::npos, graphicsOptionsJson(options).find("\"tonemapop\":\"agx\""));
    EXPECT_THROW(setGraphicsOption(options, "tonemapop", "reinhard"), std::invalid_argument);
    EXPECT_EQ("aces", toneMapOperatorName(*parseToneMapOperator("aces")));
}

TEST(GraphicsTier2, volumetrics_keep_a_depth_texture_and_make_a_new_pipeline) {
    GraphicsOptions options;
    auto key = scene::renderPipelineKey(options);
    setGraphicsOption(options, "volumetrics", "1");
    EXPECT_TRUE(options.volumetrics);
    EXPECT_TRUE(scene::sceneDepthTextureNeeded(options));
    EXPECT_NE(key, scene::renderPipelineKey(options));
}

TEST(GraphicsTier2, soft_particles_read_the_opaque_depth_and_are_part_of_enhanced) {
    GraphicsOptions options;
    setGraphicsOption(options, "softparticles", "1");
    EXPECT_TRUE(scene::sceneDepthTextureNeeded(options));
    GraphicsOptions enhanced;
    applyGraphicsPreset(enhanced, GraphicsPreset::Enhanced);
    EXPECT_TRUE(enhanced.softParticles);
}

TEST(GraphicsTier2, enhanced_adds_aces_tone_mapping_eye_adaptation_and_soft_particles) {
    GraphicsOptions options;
    options.tonemapOperator = 2; // AgX chosen before
    applyGraphicsPreset(options, GraphicsPreset::Enhanced);
    EXPECT_TRUE(options.autoExposure);
    EXPECT_TRUE(options.softParticles);
    EXPECT_TRUE(options.tonemap);
    EXPECT_EQ(1, options.tonemapOperator); // ACES
    // AgX on top of Enhanced is still Enhanced (the operator is a style choice).
    setGraphicsOption(options, "tonemapop", "agx");
    EXPECT_TRUE(matchesGraphicsPreset(options, GraphicsPreset::Enhanced));
    EXPECT_FALSE(options.volumetrics);
    EXPECT_FALSE(options.reshade);
}

TEST(GraphicsTier2, ssgi_keeps_a_depth_texture_and_makes_a_new_pipeline) {
    GraphicsOptions options;
    auto key = scene::renderPipelineKey(options);
    setGraphicsOption(options, "ssgi", "1");
    EXPECT_TRUE(options.ssgi);
    EXPECT_TRUE(scene::sceneDepthTextureNeeded(options));
    EXPECT_NE(key, scene::renderPipelineKey(options));
}

TEST(GraphicsTier3, reflections_keep_a_depth_texture_and_probe_faces_are_orthonormal) {
    GraphicsOptions options;
    auto key = scene::renderPipelineKey(options);
    setGraphicsOption(options, "reflections", "1");
    EXPECT_TRUE(scene::sceneDepthTextureNeeded(options));
    EXPECT_NE(key, scene::renderPipelineKey(options));
    for (int face = 0; face < 6; ++face) {
        auto d = scene::probeFaceDirection(face);
        auto u = scene::probeFaceUp(face);
        EXPECT_FLOAT_EQ(1.0f, glm::length(d)) << face;
        EXPECT_FLOAT_EQ(0.0f, glm::dot(d, u)) << face;
    }
    // Six distinct directions covering both signs of every axis.
    glm::vec3 sum(0.0f);
    for (int face = 0; face < 6; ++face) {
        sum += scene::probeFaceDirection(face);
    }
    EXPECT_EQ(glm::vec3(0.0f), sum);
}

TEST(GraphicsTier3, saber_fx_is_part_of_enhanced_and_needs_no_new_pipeline) {
    GraphicsOptions options;
    auto key = scene::renderPipelineKey(options);
    setGraphicsOption(options, "saberfx", "1");
    EXPECT_TRUE(options.saberFx);
    EXPECT_EQ(key, scene::renderPipelineKey(options)); // drawn in the existing glow pass
    GraphicsOptions enhanced;
    applyGraphicsPreset(enhanced, GraphicsPreset::Enhanced);
    EXPECT_TRUE(enhanced.saberFx);
}

TEST(GraphicsTier3, conversation_dof_reads_depth_and_is_part_of_enhanced) {
    GraphicsOptions options;
    auto key = scene::renderPipelineKey(options);
    setGraphicsOption(options, "dof", "1");
    EXPECT_TRUE(options.dof);
    EXPECT_TRUE(scene::sceneDepthTextureNeeded(options));
    EXPECT_NE(key, scene::renderPipelineKey(options));
    EXPECT_FLOAT_EQ(0.0f, options.dofAmount); // nothing until a conversation sets it
    GraphicsOptions enhanced;
    applyGraphicsPreset(enhanced, GraphicsPreset::Enhanced);
    EXPECT_TRUE(enhanced.dof);
}

TEST(GraphicsTier3, motion_blur_reads_depth_and_stays_out_of_enhanced) {
    GraphicsOptions options;
    auto key = scene::renderPipelineKey(options);
    setGraphicsOption(options, "motionblur", "1");
    EXPECT_TRUE(options.motionBlur);
    EXPECT_TRUE(scene::sceneDepthTextureNeeded(options));
    EXPECT_NE(key, scene::renderPipelineKey(options));
    GraphicsOptions enhanced;
    applyGraphicsPreset(enhanced, GraphicsPreset::Enhanced);
    EXPECT_FALSE(enhanced.motionBlur);
}

TEST(GraphicsTier3, lens_effects_are_separate_toggles_off_in_enhanced_and_need_no_new_pipeline) {
    GraphicsOptions options;
    auto key = scene::renderPipelineKey(options);
    for (auto name : {"lensflares", "filmgrain", "vignette", "chromaticaberration"}) {
        GraphicsOptions changed;
        setGraphicsOption(changed, name, "1");
        EXPECT_EQ(key, scene::renderPipelineKey(changed)) << name;
    }
    setGraphicsOption(options, "vignette", "1");
    EXPECT_TRUE(options.vignette);
    EXPECT_FALSE(options.filmGrain || options.lensFlares || options.chromaticAberration);
    GraphicsOptions enhanced;
    applyGraphicsPreset(enhanced, GraphicsPreset::Enhanced);
    EXPECT_FALSE(enhanced.lensFlares || enhanced.filmGrain || enhanced.vignette || enhanced.chromaticAberration);
}

TEST(GraphicsTier3, enhanced_turns_on_reflections) {
    GraphicsOptions options;
    applyGraphicsPreset(options, GraphicsPreset::Enhanced);
    EXPECT_TRUE(options.reflections);
    EXPECT_TRUE(scene::sceneDepthTextureNeeded(options));
    applyGraphicsPreset(options, GraphicsPreset::Retail);
    EXPECT_FALSE(options.reflections);
}

TEST(GraphicsTier4, surface_detail_is_part_of_enhanced_with_a_strength_and_test_patterns) {
    GraphicsOptions options;
    EXPECT_FALSE(options.normalMaps);
    EXPECT_FLOAT_EQ(1.0f, options.normalMapStrength);
    EXPECT_FALSE(options.normalMapTest);
    auto key = scene::renderPipelineKey(options);
    applyGraphicsPreset(options, GraphicsPreset::Enhanced);
    EXPECT_TRUE(options.normalMaps);
    // Maps are bound per mesh: no new render targets.
    GraphicsOptions on;
    setGraphicsOption(on, "normalmaps", "1");
    EXPECT_TRUE(on.normalMaps);
    EXPECT_EQ(key, scene::renderPipelineKey(on));

    setGraphicsOption(on, "normalmapstrength", "0.25");
    EXPECT_FLOAT_EQ(0.25f, on.normalMapStrength);
    EXPECT_THROW(setGraphicsOption(on, "normalmapstrength", "1.5"), std::invalid_argument);
    EXPECT_THROW(setGraphicsOption(on, "normalmapstrength", "-0.1"), std::invalid_argument);
    setGraphicsOption(on, "normalmaptest", "1");
    EXPECT_TRUE(on.normalMapTest);
    auto json = graphicsOptionsJson(on);
    EXPECT_NE(std::string::npos, json.find("\"normalmaps\":1"));
    EXPECT_NE(std::string::npos, json.find("\"normalmapstrength\":0.25"));
    EXPECT_NE(std::string::npos, json.find("\"normalmaptest\":1"));
    // Strength and test patterns are not preset settings.
    applyGraphicsPreset(on, GraphicsPreset::Retail);
    EXPECT_FALSE(on.normalMaps);
    EXPECT_FLOAT_EQ(0.25f, on.normalMapStrength);
    EXPECT_TRUE(matchesGraphicsPreset(on, GraphicsPreset::Retail));
}
