/*
 * Copyright (c) 2026 The reone project contributors
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

// Player settings at startup: every built-in default and every saved value
// reaches the running options exactly as the options screens' own change
// handler (set the key, then applyUserSettings) puts it there.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/game.h"
#include "reone/game/options.h"
#include "reone/game/usersettings.h"
#include "reone/graphics/preset.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

class StartupSettingsTest : public Test {
protected:
    void SetUp() override {
        _engine.init();
        // Launch values as the command line leaves them: everything off.
        _engine.options().game.smartCompanions = false;
        _engine.options().graphics.grass = false;
        _engine.options().graphics.shadows = false;
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
    }

    /** Startup: no settings file, the built-in defaults. */
    void startUp() { _game->applyUserSettings(false); }

    GameOptions &gameOptions() { return _engine.options().game; }
    graphics::GraphicsOptions &gfx() { return _engine.options().graphics; }
    audio::AudioOptions &audio() { return _engine.options().audio; }

    TestEngine _engine;
    StubConsole _console;
    std::unique_ptr<Game> _game;
};

} // namespace

TEST_F(StartupSettingsTest, gameplay_defaults_apply_at_startup) {
    ASSERT_FALSE(gameOptions().smartCompanions);
    startUp();
    EXPECT_TRUE(gameOptions().smartCompanions); // "better companion movement"
    EXPECT_TRUE(_game->userSettings().getBool(kSmartCompanionsSetting, false));
}

TEST_F(StartupSettingsTest, a_saved_gameplay_value_beats_the_default_at_startup) {
    _game->userSettings().setBool(kSmartCompanionsSetting, false);
    startUp();
    EXPECT_FALSE(gameOptions().smartCompanions);
    _game->userSettings().setBool(kSmartCompanionsSetting, true);
    startUp();
    EXPECT_TRUE(gameOptions().smartCompanions);
}

TEST_F(StartupSettingsTest, startup_equals_toggling_every_gameplay_option) {
    startUp();
    bool atStartup = gameOptions().smartCompanions;
    // The options page: toggle off, toggle on.
    _game->userSettings().setBool(kSmartCompanionsSetting, false);
    _game->applyUserSettings(false);
    EXPECT_FALSE(gameOptions().smartCompanions);
    _game->userSettings().setBool(kSmartCompanionsSetting, true);
    _game->applyUserSettings(false);
    EXPECT_EQ(atStartup, gameOptions().smartCompanions);
}

TEST_F(StartupSettingsTest, graphics_defaults_apply_at_startup) {
    startUp();
    const auto &defaults = UserSettings::builtinDefaults();
    auto on = [&](const char *key) { return defaults.at(key) == "1"; };
    EXPECT_EQ(on("optgraphicsadv.CB_BLOOM"), gfx().bloom);
    EXPECT_EQ(on("optgraphicsadv.CB_AMBIENTOCC"), gfx().gtao);
    EXPECT_EQ(on("optgraphicsadv.CB_SOFTSHADOWS"), gfx().softShadows);
    EXPECT_EQ(on("optgraphicsadv.CB_SHARPEN"), gfx().sharpen);
    EXPECT_EQ(2, static_cast<int>(graphics::antiAliasing(gfx())));
    for (const auto &toggle : graphics::graphicsFeatureToggles()) {
        std::string key = std::string("optgraphicsadv.") + toggle.settingTag;
        ASSERT_TRUE(defaults.count(key)) << key << " has no built-in default";
        EXPECT_EQ(on(key.c_str()), gfx().*toggle.field) << key;
    }
}

TEST_F(StartupSettingsTest, saved_graphics_values_apply_at_startup_as_if_toggled) {
    auto &settings = _game->userSettings();
    settings.setBool("optgraphics.CB_GRASS", true);
    settings.setBool("optgraphics.CB_SHADOWS", true);
    settings.setBool(kGraphicsBloomSetting, false);
    settings.setInt(kGraphicsAntiAliasingSetting, 1);
    startUp();
    EXPECT_TRUE(gfx().grass);
    EXPECT_TRUE(gfx().shadows);
    EXPECT_FALSE(gfx().bloom);
    EXPECT_EQ(1, static_cast<int>(graphics::antiAliasing(gfx())));
}

TEST_F(StartupSettingsTest, audio_defaults_apply_at_startup) {
    startUp();
    EXPECT_EQ(39, audio().musicVolume);
    EXPECT_EQ(89, audio().voiceVolume);
    EXPECT_EQ(60, audio().soundVolume);
    EXPECT_EQ(62, audio().movieVolume);
}

TEST_F(StartupSettingsTest, saved_audio_values_apply_at_startup_and_are_clamped) {
    auto &settings = _game->userSettings();
    settings.setInt("audio.music", 10);
    settings.setInt("audio.voice", 250);
    startUp();
    EXPECT_EQ(10, audio().musicVolume);
    EXPECT_EQ(100, audio().voiceVolume);
    EXPECT_EQ(60, audio().soundVolume); // untouched: still the default
}
