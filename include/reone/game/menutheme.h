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

#pragma once

#include <cstddef>
#include <functional>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace reone {

namespace game {

/**
 * A main-menu theme: the whole main-menu presentation of one game, meaning
 * its layout and art, its background stage, its music and its logo.
 * Presentation only (design note p1_k2menus "Main-menu themes").
 */
enum class MenuTheme {
    K1,
    K2
};

/** Player setting `menu.theme`: which themes the main menu shows. */
enum class MenuThemeMode {
    Rotate, ///< alternate K1 and K2 (default)
    K1Only,
    K2Only
};

std::optional<MenuThemeMode> parseMenuThemeMode(std::string_view value);
const char *menuThemeModeName(MenuThemeMode mode);
std::optional<MenuTheme> parseMenuTheme(std::string_view value);
const char *menuThemeName(MenuTheme theme);

/** Settings keys: the mode, and the theme shown last (so launches alternate). */
constexpr const char *kMenuThemeKey = "menu.theme";
constexpr const char *kMenuLastThemeKey = "menu.lastTheme";
/** The lineup character shown last, so the next show continues the rotation. */
constexpr const char *kMenuLastCharacterKey = "menu.lastCharacter";

/**
 * The theme to show next, given the mode, whether K2 art is available, and the
 * theme shown last. Rotate alternates; without K2 art it is always K1.
 */
MenuTheme nextMenuTheme(MenuThemeMode mode, bool k2Available, std::optional<MenuTheme> last);

/**
 * Idle rotation and the fade between themes, on wall time.
 *
 * Once the menu has sat idle for kIdleSeconds it fades out, asks for the
 * switch, then fades the new theme in. Input resets the idle clock.
 */
class MenuThemeFade {
public:
    /** Idle time before the next character (and every few, the next theme). */
    static constexpr float kIdleSeconds = 20.0f;
    /** Idle character switches per theme switch: 9 x 20 s = 180 s. */
    static constexpr int kSwitchesPerTheme = 9;
    static constexpr float kFadeSeconds = 0.6f;

    /** Advances the clock. True on the frame the screen is fully dark: switch now. */
    bool update(float dt, bool canRotate);
    /** After a switch: start dark and fade the new theme in. */
    void beginFadeIn();
    void resetIdle() { _idle = 0.0f; }

    /** Opacity of the black cover, 0..1. */
    float alpha() const;
    bool fading() const { return _phase != Phase::None; }
    float idleSeconds() const { return _idle; }

private:
    enum class Phase {
        None,
        Out,
        In
    };

    Phase _phase {Phase::None};
    float _idle {0.0f};
    float _time {0.0f};
};

/**
 * One character in the owner's main-menu lineup: a creature appearance row,
 * named by its appearance.2da label, read from one game's data.
 */
/** Which lineup a character stands in: one group is shown at a time. */
enum class MenuLineupGroup {
    Party,
    Villain
};

struct MenuLineupEntry {
    std::string name;
    MenuTheme source {MenuTheme::K1};
    std::string appearance;
    MenuLineupGroup group {MenuLineupGroup::Party};
};

/**
 * The one character the menu shows next: the eligible entry after `last` (by
 * name) in table order, wrapping; the first when `last` is not found. K2
 * entries are skipped without K2 data. Empty when nothing is eligible.
 */
std::optional<size_t> nextMenuCharacter(const std::vector<MenuLineupEntry> &entries,
                                        bool k2Available,
                                        const std::string &last);

/** The lineup file, next to reone_settings.ini. Absent: the default table. */
constexpr const char *kMenuLineupFilename = "menu_lineup.txt";

/** The default lineup: five K1 and six K2 characters. */
const std::vector<MenuLineupEntry> &defaultMenuLineup();

/**
 * Parse "name, k1|k2, appearance label[, party|villain]" lines (the group
 * defaults to party). Blank lines and lines starting with '#' are skipped, as
 * are malformed lines.
 */
std::vector<MenuLineupEntry> parseMenuLineup(std::istream &input);

/**
 * Pick up to `count` distinct entries to show, all from one group (party
 * members or villains, chosen at random among groups with an eligible entry),
 * alternating K1 and K2 characters while both are available. K2 entries are
 * skipped without K2 data.
 * `randomInt(min, max)` is inclusive; pass the cosmetic random stream.
 */
std::vector<size_t> pickMenuLineup(const std::vector<MenuLineupEntry> &entries,
                                   bool k2Available,
                                   size_t count,
                                   const std::function<int(int, int)> &randomInt);

} // namespace game

} // namespace reone
