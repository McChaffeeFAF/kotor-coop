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

// P1-K2 menus: main-menu themes and the character lineup (design note p1_k2menus).

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "reone/game/game.h"
#include "reone/game/gui.h"
#include "reone/game/menutheme.h"
#include "reone/gui/gui.h"

#include <algorithm>
#include <set>
#include <sstream>

using namespace reone;
using namespace reone::game;
using namespace reone::gui;
using namespace reone::resource;

namespace reone::game {

/** A screen of each presentation family, reporting what it presents as. */
class P1K2MenusProbeGUI : public GameGUI {
public:
    P1K2MenusProbeGUI(Game &game, ServicesView &services, PresentationFamily family) :
        GameGUI(game, services, family) {}
    bool presentsTSL() const { return isTSL(); }
    bool k2LayoutInK1() const { return isK2LayoutInK1(); }
};

} // namespace reone::game

namespace {

class NullGUIs : public IGUIs {
public:
    void clear() override {}
    std::shared_ptr<IGUI> get(const std::string &, std::function<void(IGUI &)>) override { return nullptr; }
};

struct MenuSession {
    NullGUIs mainGuis;
    NullGUIs k2Guis;
    GUIServices guiServices;
    ServicesView services;
    GameOptions gameOptions;
    OptionsView options;
    StubConsole console;
    Game game;

    MenuSession(TestEngine &engine, GameID gameId, HudProfile hud, bool withK2Set) :
        guiServices(mainGuis, withK2Set ? &k2Guis : nullptr, withK2Set ? &engine.services().resource : nullptr),
        services(engine.services().game, engine.services().movie, engine.services().audio,
                 engine.services().graphics, engine.services().scene, guiServices,
                 engine.services().script, engine.services().resource, engine.services().system),
        options(gameOptions, engine.options().graphics, engine.options().audio),
        game(gameId, "", options, services, console) {
        gameOptions.hud = hud;
    }

    bool presentsTSL(PresentationFamily family) {
        return P1K2MenusProbeGUI(game, services, family).presentsTSL();
    }
};

} // namespace

TEST(P1K2MenusProfile, k2_hud_brings_k2_menus_but_not_game_bound_screens) {
    TestEngine &engine = testEngine();
    MenuSession session(engine, GameID::KotOR, HudProfile::K2, true);
    EXPECT_TRUE(session.presentsTSL(PresentationFamily::Menu));
    EXPECT_TRUE(session.presentsTSL(PresentationFamily::Hud));
    EXPECT_FALSE(session.presentsTSL(PresentationFamily::Game));
    EXPECT_TRUE(P1K2MenusProbeGUI(session.game, session.services, PresentationFamily::Menu).k2LayoutInK1());
    EXPECT_TRUE(session.game.menuThemesAvailable());
    // Rules never follow the presentation.
    EXPECT_FALSE(session.game.isTSL());
}

TEST(P1K2MenusProfile, k1_hud_and_missing_k2_set_stay_k1) {
    TestEngine &engine = testEngine();
    for (auto [hud, withSet] : {std::pair {HudProfile::K1, false}, {HudProfile::Default, false}, {HudProfile::K2, false}}) {
        MenuSession session(engine, GameID::KotOR, hud, withSet);
        EXPECT_FALSE(session.presentsTSL(PresentationFamily::Menu));
        EXPECT_FALSE(session.presentsTSL(PresentationFamily::Hud));
        EXPECT_FALSE(session.game.menuThemesAvailable());
    }
    // A K2 session presents K2 throughout and has no K1 themes to rotate.
    MenuSession tsl(engine, GameID::TSL, HudProfile::Default, false);
    EXPECT_TRUE(tsl.presentsTSL(PresentationFamily::Menu));
    EXPECT_TRUE(tsl.presentsTSL(PresentationFamily::Game));
    EXPECT_FALSE(tsl.game.menuThemesAvailable());
}

TEST(P1K2MenusProfile, theme_mode_reads_the_settings_file_key) {
    TestEngine &engine = testEngine();
    MenuSession session(engine, GameID::KotOR, HudProfile::K2, true);
    EXPECT_EQ(MenuThemeMode::Rotate, session.game.menuThemeMode());
    session.game.userSettings().set(kMenuThemeKey, "k2");
    EXPECT_EQ(MenuThemeMode::K2Only, session.game.menuThemeMode());
    session.game.userSettings().set(kMenuThemeKey, "bogus");
    EXPECT_EQ(MenuThemeMode::Rotate, session.game.menuThemeMode());
}

TEST(P1K2MenusTheme, modes_parse_and_round_trip) {
    EXPECT_EQ(MenuThemeMode::Rotate, parseMenuThemeMode("rotate"));
    EXPECT_EQ(MenuThemeMode::K1Only, parseMenuThemeMode("k1"));
    EXPECT_EQ(MenuThemeMode::K2Only, parseMenuThemeMode("k2"));
    EXPECT_FALSE(parseMenuThemeMode("K3").has_value());
    for (auto mode : {MenuThemeMode::Rotate, MenuThemeMode::K1Only, MenuThemeMode::K2Only}) {
        EXPECT_EQ(mode, parseMenuThemeMode(menuThemeModeName(mode)));
    }
    EXPECT_EQ(MenuTheme::K2, parseMenuTheme(menuThemeName(MenuTheme::K2)));
}

TEST(P1K2MenusTheme, rotate_alternates_and_needs_k2_art) {
    EXPECT_EQ(MenuTheme::K1, nextMenuTheme(MenuThemeMode::Rotate, true, std::nullopt));
    EXPECT_EQ(MenuTheme::K2, nextMenuTheme(MenuThemeMode::Rotate, true, MenuTheme::K1));
    EXPECT_EQ(MenuTheme::K1, nextMenuTheme(MenuThemeMode::Rotate, true, MenuTheme::K2));
    EXPECT_EQ(MenuTheme::K2, nextMenuTheme(MenuThemeMode::K2Only, true, MenuTheme::K2));
    EXPECT_EQ(MenuTheme::K1, nextMenuTheme(MenuThemeMode::K1Only, true, MenuTheme::K1));
    // Without a K2 install, only K1's theme.
    EXPECT_EQ(MenuTheme::K1, nextMenuTheme(MenuThemeMode::Rotate, false, MenuTheme::K1));
    EXPECT_EQ(MenuTheme::K1, nextMenuTheme(MenuThemeMode::K2Only, false, std::nullopt));
}

TEST(P1K2MenusTheme, idle_fade_switches_once_at_full_dark) {
    MenuThemeFade fade;
    EXPECT_FALSE(fade.update(MenuThemeFade::kIdleSeconds - 1.0f, true));
    EXPECT_FLOAT_EQ(0.0f, fade.alpha());
    // Input (or a sub-menu) holds the idle clock at zero.
    EXPECT_FALSE(fade.update(10.0f, false));
    EXPECT_FLOAT_EQ(0.0f, fade.idleSeconds());

    EXPECT_FALSE(fade.update(MenuThemeFade::kIdleSeconds, true));
    EXPECT_TRUE(fade.fading());
    EXPECT_FALSE(fade.update(MenuThemeFade::kFadeSeconds / 2.0f, true));
    EXPECT_NEAR(0.5f, fade.alpha(), 1e-4f);
    EXPECT_TRUE(fade.update(MenuThemeFade::kFadeSeconds, true));

    fade.beginFadeIn();
    EXPECT_FLOAT_EQ(1.0f, fade.alpha());
    EXPECT_FALSE(fade.update(MenuThemeFade::kFadeSeconds, true));
    EXPECT_FALSE(fade.fading());
    EXPECT_FLOAT_EQ(0.0f, fade.alpha());
}

TEST(P1K2MenusLineup, default_table_mixes_both_games_with_sion_and_nihilus) {
    const auto &lineup = defaultMenuLineup();
    std::set<std::string> k1, k2;
    for (const auto &entry : lineup) {
        (entry.source == MenuTheme::K1 ? k1 : k2).insert(entry.name);
    }
    EXPECT_EQ(5u, k1.size());
    EXPECT_TRUE(k1.count("Bastila") && k1.count("Carth") && k1.count("Revan") && k1.count("Malak") && k1.count("HK-47"));
    EXPECT_TRUE(k2.count("Kreia") && k2.count("Atton") && k2.count("Bao-Dur") && k2.count("Visas"));
    EXPECT_TRUE(k2.count("Darth Sion") && k2.count("Darth Nihilus"));
}

TEST(P1K2MenusLineup, one_character_at_a_time_rotates_through_the_table) {
    const auto &lineup = defaultMenuLineup();
    auto first = nextMenuCharacter(lineup, true, "");
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(0u, *first);
    // Each show continues after the last one, wrapping at the end.
    std::set<size_t> seen;
    std::string last;
    for (size_t i = 0; i < lineup.size(); ++i) {
        auto next = nextMenuCharacter(lineup, true, last);
        ASSERT_TRUE(next.has_value());
        seen.insert(*next);
        last = lineup[*next].name;
    }
    EXPECT_EQ(lineup.size(), seen.size());
    EXPECT_EQ(0u, *nextMenuCharacter(lineup, true, last));
    // Without K2 data only K1 characters come up.
    last.clear();
    for (int i = 0; i < 10; ++i) {
        auto next = nextMenuCharacter(lineup, false, last);
        ASSERT_TRUE(next.has_value());
        EXPECT_EQ(MenuTheme::K1, lineup[*next].source);
        last = lineup[*next].name;
    }
    EXPECT_FALSE(nextMenuCharacter({}, true, "").has_value());
}

TEST(P1K2MenusLineup, parse_skips_comments_and_bad_lines) {
    std::istringstream input(
        "# name, game, appearance\n"
        "Bastila, k1, Party_NPC_Bastila\n"
        "\n"
        "  Kreia ,k2,Party_NPC_Kreia  \r\n"
        "Broken, k3, Nope\n"
        "Short, k1\n");
    auto lineup = parseMenuLineup(input);
    ASSERT_EQ(2u, lineup.size());
    EXPECT_EQ("Bastila", lineup[0].name);
    EXPECT_EQ(MenuTheme::K1, lineup[0].source);
    EXPECT_EQ("Kreia", lineup[1].name);
    EXPECT_EQ(MenuTheme::K2, lineup[1].source);
    EXPECT_EQ("Party_NPC_Kreia", lineup[1].appearance);
}

TEST(P1K2MenusLineup, pick_alternates_games_and_falls_back_to_k1) {
    const auto &lineup = defaultMenuLineup();
    auto first = [](int min, int) { return min; };
    auto picked = pickMenuLineup(lineup, true, 4, first);
    ASSERT_EQ(4u, picked.size());
    std::set<size_t> distinct(picked.begin(), picked.end());
    EXPECT_EQ(4u, distinct.size());
    for (size_t i = 1; i < picked.size(); ++i) {
        EXPECT_NE(lineup[picked[i - 1]].source, lineup[picked[i]].source);
    }

    // Without K2 data, only K1 party members: Bastila, Carth, HK-47.
    auto k1Only = pickMenuLineup(lineup, false, 4, first);
    ASSERT_EQ(3u, k1Only.size());
    for (auto index : k1Only) {
        EXPECT_EQ(MenuTheme::K1, lineup[index].source);
    }
}

TEST(P1K2MenusLineup, a_show_never_mixes_party_members_and_villains) {
    const auto &lineup = defaultMenuLineup();
    for (int groupRoll : {0, 1}) {
        auto roll = [groupRoll, calls = 0](int min, int max) mutable {
            // The first draw picks the group; later draws pick characters.
            return calls++ == 0 ? std::clamp(groupRoll, min, max) : min;
        };
        auto picked = pickMenuLineup(lineup, true, 4, roll);
        ASSERT_EQ(4u, picked.size());
        for (auto index : picked) {
            EXPECT_EQ(lineup[picked[0]].group, lineup[index].group);
        }
    }
    std::istringstream input(
        "Revan, k1, Unique_Darth_Revan, villain\n"
        "Carth, k1, Party_NPC_Carth, party\n"
        "Odd, k1, X, pirate\n");
    auto parsed = parseMenuLineup(input);
    ASSERT_EQ(2u, parsed.size());
    EXPECT_EQ(MenuLineupGroup::Villain, parsed[0].group);
    EXPECT_EQ(MenuLineupGroup::Party, parsed[1].group);
}
