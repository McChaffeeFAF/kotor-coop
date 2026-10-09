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

// Graphics preset plumbing through the engine command line and reone.cfg
// (design note graphics_preset).

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "../../src/apps/engine/optionsparser.h"

#include "reone/graphics/preset.h"

using namespace reone;
using namespace reone::graphics;

namespace {

std::unique_ptr<Options> parseArgs(std::vector<std::string> args) {
    args.insert(args.begin(), "engine");
    std::vector<char *> argv;
    for (auto &arg : args) {
        argv.push_back(arg.data());
    }
    OptionsParser parser(static_cast<int>(argv.size()), argv.data());
    return parser.parse();
}

// Each test runs in a scratch directory, so any reone.cfg there is its own.
class GraphicsOptionsParse : public testing::Test {
protected:
    void SetUp() override {
        _previous = std::filesystem::current_path();
        _dir = std::filesystem::temp_directory_path() /
               (std::string("reone_gfxopts_") + testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(_dir);
        std::filesystem::create_directories(_dir);
        std::filesystem::current_path(_dir);
    }

    void TearDown() override {
        std::filesystem::current_path(_previous);
        std::error_code ec;
        std::filesystem::remove_all(_dir, ec);
    }

    void writeConfig(const std::string &text) {
        std::ofstream(_dir / "reone.cfg") << text;
    }

private:
    std::filesystem::path _previous;
    std::filesystem::path _dir;
};

} // namespace

TEST_F(GraphicsOptionsParse, no_flags_is_the_retail_look) {
    auto options = parseArgs({});
    const auto &g = options->graphics;
    EXPECT_TRUE(matchesGraphicsPreset(g, GraphicsPreset::Retail));
    EXPECT_EQ(AntiAliasing::FXAA, antiAliasing(g));
    EXPECT_FALSE(g.bloom || g.smaa || g.taa || g.softShadows || g.gtao);
    EXPECT_EQ(2048, g.shadowResolution);
    EXPECT_EQ(2, g.anisotropicFiltering);
}

TEST_F(GraphicsOptionsParse, enhanced_sets_the_preset_and_leaves_shadow_resolution_alone) {
    auto options = parseArgs({"--enhanced", "1"});
    const auto &g = options->graphics;
    EXPECT_TRUE(matchesGraphicsPreset(g, GraphicsPreset::Enhanced));
    EXPECT_TRUE(g.bloom);
    EXPECT_EQ(AntiAliasing::SMAA, antiAliasing(g));
    EXPECT_TRUE(g.softShadows);
    EXPECT_TRUE(g.gtao);
    EXPECT_EQ(2048, g.shadowResolution);
    EXPECT_EQ(2, g.anisotropicFiltering);
}

TEST_F(GraphicsOptionsParse, flags_given_with_enhanced_override_it) {
    auto options = parseArgs({"--enhanced", "1", "--gtao", "0", "--aa", "taa", "--shadowres", "2", "--anisofilter", "4"});
    const auto &g = options->graphics;
    EXPECT_FALSE(g.gtao);
    EXPECT_EQ(AntiAliasing::TAA, antiAliasing(g));
    EXPECT_TRUE(g.bloom);
    EXPECT_TRUE(g.softShadows);
    EXPECT_EQ(4096, g.shadowResolution);
    EXPECT_EQ(4, g.anisotropicFiltering);

    auto noBloom = parseArgs({"--bloom", "0", "--enhanced", "1"});
    EXPECT_FALSE(noBloom->graphics.bloom);
    EXPECT_TRUE(noBloom->graphics.smaa);
}

TEST_F(GraphicsOptionsParse, single_features_without_the_preset) {
    auto options = parseArgs({"--softshadows", "1", "--bloom", "1", "--bloom-intensity", "1.5", "--sharpen-amount", "0.5"});
    const auto &g = options->graphics;
    EXPECT_TRUE(g.softShadows);
    EXPECT_TRUE(g.bloom);
    EXPECT_FLOAT_EQ(1.5f, g.bloomIntensity);
    EXPECT_FLOAT_EQ(0.5f, g.sharpenAmount);
    EXPECT_FALSE(g.gtao);
    EXPECT_EQ(AntiAliasing::FXAA, antiAliasing(g));
    EXPECT_EQ(AntiAliasing::None, antiAliasing(parseArgs({"--aa", "none"})->graphics));
}

TEST_F(GraphicsOptionsParse, bad_values_are_rejected) {
    EXPECT_THROW(parseArgs({"--aa", "msaa"}), std::invalid_argument);
    EXPECT_THROW(parseArgs({"--bloom-intensity", "-1"}), std::invalid_argument);
    EXPECT_ANY_THROW(parseArgs({"--softshadows", "maybe"}));
}

TEST_F(GraphicsOptionsParse, config_file_keys_match_the_flags_and_the_command_line_wins) {
    writeConfig("enhanced=1\naa=taa\nshadowres=2\ngtao=0\n");
    auto options = parseArgs({});
    const auto &g = options->graphics;
    EXPECT_TRUE(g.bloom);
    EXPECT_TRUE(g.softShadows);
    EXPECT_FALSE(g.gtao);
    EXPECT_EQ(AntiAliasing::TAA, antiAliasing(g));
    EXPECT_EQ(4096, g.shadowResolution);

    auto overridden = parseArgs({"--aa", "smaa", "--gtao", "1"});
    EXPECT_EQ(AntiAliasing::SMAA, antiAliasing(overridden->graphics));
    EXPECT_TRUE(overridden->graphics.gtao);
}

TEST_F(GraphicsOptionsParse, tier2_feature_flags_on_the_command_line_and_in_the_config) {
    for (const auto &toggle : graphicsFeatureToggles()) {
        auto options = parseArgs({std::string("--") + toggle.name, "1"});
        EXPECT_TRUE(options->graphics.*toggle.field) << toggle.name;
        // Given with --enhanced, the flag wins over the preset either way.
        auto off = parseArgs({"--enhanced", "1", std::string("--") + toggle.name, "0"});
        EXPECT_FALSE(off->graphics.*toggle.field) << toggle.name;
        auto on = parseArgs({"--enhanced", "1", std::string("--") + toggle.name, "1"});
        EXPECT_TRUE(on->graphics.*toggle.field) << toggle.name;
        auto preset = parseArgs({"--enhanced", "1"});
        EXPECT_EQ(toggle.enhanced, preset->graphics.*toggle.field) << toggle.name;
    }
    writeConfig("reshade=1\n");
    EXPECT_TRUE(parseArgs({})->graphics.reshade);
    EXPECT_FALSE(parseArgs({"--reshade", "0"})->graphics.reshade);
}

TEST_F(GraphicsOptionsParse, tone_mapping_operator_flag) {
    EXPECT_EQ(1, parseArgs({})->graphics.tonemapOperator);
    auto agx = parseArgs({"--tonemap", "1", "--tonemap-op", "agx"});
    EXPECT_TRUE(agx->graphics.tonemap);
    EXPECT_EQ(2, agx->graphics.tonemapOperator);
    EXPECT_THROW(parseArgs({"--tonemap-op", "filmic"}), std::invalid_argument);
    writeConfig("autoexposure=1\ntonemap-op=agx\n");
    auto cfg = parseArgs({});
    EXPECT_TRUE(cfg->graphics.autoExposure);
    EXPECT_EQ(2, cfg->graphics.tonemapOperator);
}

TEST_F(GraphicsOptionsParse, surface_detail_strength_folder_and_test_flags) {
    auto defaults = parseArgs({});
    EXPECT_FALSE(defaults->graphics.normalMaps);
    EXPECT_FLOAT_EQ(1.0f, defaults->graphics.normalMapStrength);
    EXPECT_TRUE(defaults->graphics.normalMapFolder.empty()); // main() puts it beside the executable
    EXPECT_FALSE(defaults->graphics.normalMapTest);

    auto options = parseArgs({"--normalmaps", "1", "--normalmap-strength", "0.5", "--normalmap-folder", "D:/maps", "--normalmap-test", "1"});
    EXPECT_TRUE(options->graphics.normalMaps);
    EXPECT_FLOAT_EQ(0.5f, options->graphics.normalMapStrength);
    EXPECT_EQ("D:/maps", options->graphics.normalMapFolder);
    EXPECT_TRUE(options->graphics.normalMapTest);
    EXPECT_TRUE(parseArgs({"--enhanced", "1"})->graphics.normalMaps);

    EXPECT_THROW(parseArgs({"--normalmap-strength", "2"}), std::invalid_argument);
    writeConfig("normalmaps=1\nnormalmap-strength=0.75\nnormalmap-folder=maps\n");
    auto config = parseArgs({});
    EXPECT_TRUE(config->graphics.normalMaps);
    EXPECT_FLOAT_EQ(0.75f, config->graphics.normalMapStrength);
    EXPECT_EQ("maps", config->graphics.normalMapFolder);
}

TEST_F(GraphicsOptionsParse, join_without_a_port_defaults_to_27015) {
    EXPECT_EQ("1.2.3.4:27015", parseArgs({"--join", "1.2.3.4"})->coop.joinAddress);
    EXPECT_EQ("host.example:27015", parseArgs({"--join", "host.example"})->coop.joinAddress);
    EXPECT_EQ("1.2.3.4:5000", parseArgs({"--join", "1.2.3.4:5000"})->coop.joinAddress);
}
