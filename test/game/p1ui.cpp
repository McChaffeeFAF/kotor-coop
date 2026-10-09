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

// P1-UI: key bindings and player settings (design note p1_ui).

#include <gtest/gtest.h>

#include "reone/game/keymap.h"
#include "reone/game/types.h"
#include "reone/game/usersettings.h"

#include <sstream>

using namespace reone;
using namespace reone::game;
using input::KeyCode;

TEST(P1UiKeymap, defaults_follow_k1_and_keymap_rows) {
    Keymap keymap;
    EXPECT_EQ(KeyCode::W, keymap.keyFor(KeyAction::ActionUp));
    EXPECT_EQ(KeyCode::Z, keymap.keyFor(KeyAction::ActionLeft));
    EXPECT_EQ(KeyCode::Escape, keymap.keyFor(KeyAction::GameMenu));
    EXPECT_EQ(KeyCode::Q, keymap.keyFor(KeyAction::SelectPrev));

    keymap.loadDefaults({{"SelectPrev", "F6"}, {"CAPTLOCKX", "Q"}, {"Freelook", "CAPTLOCK"}, {"Map", "bogus"}});
    EXPECT_EQ(KeyCode::F6, keymap.keyFor(KeyAction::SelectPrev));
    EXPECT_EQ(KeyCode::CapsLock, keymap.keyFor(KeyAction::Freelook));
    EXPECT_EQ(KeyCode::M, keymap.keyFor(KeyAction::Map));
}

TEST(P1UiKeymap, translate_forwards_engine_keys_and_dispatches_the_rest) {
    Keymap keymap;
    auto w = keymap.translate(KeyCode::W);
    EXPECT_EQ(KeyCode::W, w.forward);
    EXPECT_FALSE(w.dispatch);

    auto escape = keymap.translate(KeyCode::Escape);
    ASSERT_TRUE(escape.dispatch);
    EXPECT_EQ(KeyAction::GameMenu, *escape.dispatch);

    auto i = keymap.translate(KeyCode::I);
    ASSERT_TRUE(i.dispatch);
    EXPECT_EQ(KeyAction::Inventory, *i.dispatch);

    // Keys the keymap does not know pass through.
    EXPECT_EQ(KeyCode::B, keymap.translate(KeyCode::B).forward);
}

TEST(P1UiKeymap, rebinding_swaps_and_moves_the_engine_key) {
    Keymap keymap;
    keymap.bind(KeyAction::ActionUp, KeyCode::Up);
    EXPECT_EQ(KeyCode::Up, keymap.keyFor(KeyAction::ActionUp));
    // The arrow now walks forward, as W did; W alone does nothing.
    EXPECT_EQ(KeyCode::W, keymap.translate(KeyCode::Up).forward);
    auto w = keymap.translate(KeyCode::W);
    EXPECT_EQ(KeyCode::Unknown, w.forward);
    EXPECT_FALSE(w.dispatch);

    // Binding a held key swaps: Inventory takes Map's M, Map takes I.
    keymap.bind(KeyAction::Inventory, KeyCode::M);
    EXPECT_EQ(KeyCode::M, keymap.keyFor(KeyAction::Inventory));
    EXPECT_EQ(KeyCode::I, keymap.keyFor(KeyAction::Map));

    // Escape is not remappable, and cannot be taken.
    keymap.bind(KeyAction::GameMenu, KeyCode::F9);
    EXPECT_EQ(KeyCode::Escape, keymap.keyFor(KeyAction::GameMenu));
    keymap.bind(KeyAction::Map, KeyCode::Escape);
    EXPECT_EQ(KeyCode::I, keymap.keyFor(KeyAction::Map));
}

TEST(P1UiKeymap, bindings_round_trip_through_settings) {
    Keymap keymap;
    keymap.bind(KeyAction::Quests, KeyCode::F7);
    UserSettings settings;
    keymap.storeSettings(settings);
    EXPECT_EQ("F7", settings.get("key.Quests").value_or(""));
    EXPECT_FALSE(settings.has("key.Map")); // defaults are not stored

    Keymap loaded;
    loaded.applySettings(settings);
    EXPECT_EQ(KeyCode::F7, loaded.keyFor(KeyAction::Quests));
    loaded.resetToDefaults();
    EXPECT_EQ(KeyCode::L, loaded.keyFor(KeyAction::Quests));
}

TEST(P1UiKeymap, key_names_parse_back) {
    for (auto code : {KeyCode::A, KeyCode::Key7, KeyCode::Tab, KeyCode::CapsLock, KeyCode::F12, KeyCode::LeftControl, KeyCode::Up}) {
        EXPECT_EQ(code, parseKeyName(keyName(code))) << keyName(code);
    }
    EXPECT_EQ(KeyCode::CapsLock, parseKeyName("CAPTLOCK"));
    EXPECT_EQ(KeyCode::LeftControl, parseKeyName("LeftCTRL"));
    EXPECT_EQ(KeyCode::Unknown, parseKeyName("NotAKey"));
}

TEST(P1UiSettings, text_round_trip_and_typed_reads) {
    UserSettings settings;
    settings.setInt("audio.music", 40);
    settings.setBool("hud.fixedActionBar", false);
    settings.setFloat("select.radius", 42.5f);
    std::ostringstream out;
    settings.write(out);

    UserSettings loaded;
    std::istringstream in(out.str() + "\n# comment\n  junk line\nunknown.key = kept \n");
    loaded.read(in);
    EXPECT_EQ(40, loaded.getInt("audio.music", 0));
    EXPECT_FALSE(loaded.getBool("hud.fixedActionBar", true));
    EXPECT_FLOAT_EQ(42.5f, loaded.getFloat("select.radius", 0.0f));
    EXPECT_EQ("kept", loaded.get("unknown.key").value_or(""));
    EXPECT_EQ(7, loaded.getInt("missing", 7));
    loaded.set("audio.voice", "loud");
    EXPECT_EQ(85, loaded.getInt("audio.voice", 85));
}

TEST(P1UiSettings, selection_radius_default_is_well_past_upstream) {
    EXPECT_GE(kDefaultSelectionRadius, 2.0f * kSelectionDistance);
    EXPECT_LE(kDefaultSelectionRadius, kMaxSelectionRadius);
}
