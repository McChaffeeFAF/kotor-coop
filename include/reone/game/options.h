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

#include "reone/audio/options.h"
#include "reone/system/types.h"
#include "reone/graphics/options.h"
#include "menupresentation.h"

#include <cctype>
#include <stdexcept>
#include <string>

namespace reone {

namespace game {

/**
 * Which game's interface art and layout the HUD family presents.
 *
 * Default follows the running game. K2 in a K1 session reads the K2 GUI set
 * from the install named by GameOptions::k2Path; game rules still follow the
 * running game.
 */
enum class HudProfile {
    Default,
    K1,
    K2
};

/** Parses "k1", "k2" or "" (default). Throws std::invalid_argument otherwise. */
inline HudProfile parseHudProfile(const std::string &value) {
    std::string lower;
    for (char c : value) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower.empty() || lower == "default") {
        return HudProfile::Default;
    }
    if (lower == "k1") {
        return HudProfile::K1;
    }
    if (lower == "k2") {
        return HudProfile::K2;
    }
    throw std::invalid_argument("--hud must be k1 or k2, got \"" + value + "\"");
}

/** User setting (reone_settings.ini) behind the "Smarter companions" check box. */
constexpr const char *kSmartCompanionsSetting = "optgameplay.CB_SMARTCOMPANIONS";

// User settings behind the Enhanced preset's controls (Options > Graphics >
// Advanced). K1's panel is optgraphicsadv and K2's optgraphicadv; both
// store under optgraphicsadv.
constexpr const char *kGraphicsBloomSetting = "optgraphicsadv.CB_BLOOM";
constexpr const char *kGraphicsAOSetting = "optgraphicsadv.CB_AMBIENTOCC";
constexpr const char *kGraphicsSoftShadowsSetting = "optgraphicsadv.CB_SOFTSHADOWS";
constexpr const char *kGraphicsHiResShadowsSetting = "optgraphicsadv.CB_HIRESSHADOWS";
constexpr const char *kGraphicsSharpenSetting = "optgraphicsadv.CB_SHARPEN";
constexpr const char *kGraphicsAntiAliasingSetting = "graphics.antialiasing"; /**< 0 none, 1 FXAA, 2 SMAA, 3 TAA */
/** The settings key of a Tier 2 check box: optgraphicsadv.<settingTag>. */
inline std::string graphicsFeatureSetting(const char *settingTag) { return std::string("optgraphicsadv.") + settingTag; }
constexpr const char *kGraphicsAnisotropySetting = "graphics.anisotropy";     /**< log2: 0 off .. 4 16x */

struct GameOptions {
    std::filesystem::path path;
    /// Optional K2 install: its GUI resources for the K2 HUD, and the data
    /// the K2 systems merge reads (design note k2_merge).
    std::filesystem::path k2Path;
    /// With a K2 install, merge K2's systems into K1's data (default on).
    bool k2Merge {true};
    HudProfile hud {HudProfile::Default};
    bool developer {false};
    /// P1 item 10: the player character starts with auto-combat on (default off).
    bool autoCombat {false};
    bool neo {false};
    uint8_t clientDifficulty {1}; // Easy=0, Normal=1, Difficult=2, Default=3
    MenuPresentation menuPresentation;
    std::filesystem::path configurationPath; // Empty for embedded/test hosts.
    // Fixed simulation rate, in ticks per second.
    int tickRate {30};
    // Start with the tick gate closed: the simulation advances only by ticks
    // a harness grants (step, stepidle, untiltick).
    bool tickGate {false};
    // Developer option: the player character made at character generation
    // (and the developer default party) survives 0 hit points. Retail player
    // characters can die, so this is off by default.
    bool immortalPlayer {false};
    // Retail pauses the world while the in-game menus or a container screen
    // are open. Off by default: in co-op one player's menu must not stop the
    // others, so the world keeps running.
    bool pauseInMenus {false};
    // Co-op "Friendly collision" off: party members and player characters
    // pass through each other (collision, avoidance and side-step probes
    // skip friendly pairs). Hostile and neutral creatures, walls, doors and
    // placeables still block. Off by default, which is retail collision.
    bool friendlyCollisionOff {false};
    // Most ticks one frame may run (scaled by game speed); overdue ticks past
    // this are dropped, so a stall cannot snowball.
    int maxTicksPerFrame {5};
    // Draw between the last two ticks (design note render_interp). Off, the
    // world is drawn as the last tick left it: motion steps at the tick rate.
    bool interpolate {true};
    // Test hosts only: every Game::update runs exactly one simulation step of
    // the frame's own length, as before the fixed step existed. Unit tests of
    // game logic drive time that way; the engine never sets this.
    bool variableStep {false};
    // Natural regeneration speed (design note regeneration): multiplies the
    // out-of-combat hit point and force point rates. 1 is retail. In co-op
    // this becomes a host setting.
    float regenRate {1.0f};
    // "Smarter companions" (C13, opt-in): an engine-side brain decides the
    // combat rounds of AI-controlled party members that run the generic
    // combat scripts, and of the player character under auto-combat. Off,
    // those rounds run the retail scripts as before. A host setting in co-op.
    bool smartCompanions {false};
};

struct OptionsView {
    GameOptions &game;
    graphics::GraphicsOptions &graphics;
    audio::AudioOptions &audio;

    OptionsView(
        GameOptions &game,
        graphics::GraphicsOptions &graphics,
        audio::AudioOptions &audio) :
        game(game),
        graphics(graphics),
        audio(audio) {
    }
};

} // namespace game

} // namespace reone
