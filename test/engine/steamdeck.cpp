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

// Steam Deck defaults in the engine's option parser (POSIX environment calls: Linux only).

#include <gtest/gtest.h>

#ifndef _WIN32

#include <cstdlib>
#include <filesystem>

#include "../../src/apps/engine/optionsparser.h"

using namespace reone;

namespace {

std::unique_ptr<Options> parseArgs(std::vector<std::string> args) {
    args.insert(args.begin(), "engine");
    std::vector<char *> argv;
    for (auto &arg : args) {
        argv.push_back(arg.data());
    }
    return OptionsParser(static_cast<int>(argv.size()), argv.data()).parse();
}

class SteamDeckOptions : public testing::Test {
protected:
    void SetUp() override {
        _previous = std::filesystem::current_path();
        _dir = std::filesystem::temp_directory_path() / "reone_steamdeck_options";
        std::filesystem::create_directories(_dir);
        std::filesystem::current_path(_dir);
        const char *deck = std::getenv("SteamDeck");
        _hadDeck = deck != nullptr;
        if (deck) {
            _deck = deck;
        }
        const char *user = std::getenv("USER");
        _hadUser = user != nullptr;
        if (user) {
            _user = user;
        }
        ::setenv("USER", "tester", 1);
        ::unsetenv("SteamDeck");
    }

    void TearDown() override {
        _hadDeck ? ::setenv("SteamDeck", _deck.c_str(), 1) : ::unsetenv("SteamDeck");
        _hadUser ? ::setenv("USER", _user.c_str(), 1) : ::unsetenv("USER");
        std::filesystem::current_path(_previous);
        std::error_code ec;
        std::filesystem::remove_all(_dir, ec);
    }

private:
    std::filesystem::path _previous, _dir;
    std::string _deck, _user;
    bool _hadDeck {false}, _hadUser {false};
};

} // namespace

TEST_F(SteamDeckOptions, desktop_keeps_the_usual_defaults) {
    auto o = parseArgs({});
    EXPECT_EQ(1024, o->graphics.width);
    EXPECT_EQ(768, o->graphics.height);
    EXPECT_FALSE(o->graphics.fullscreen);
    EXPECT_FLOAT_EQ(1.0f, o->graphics.guiScale);
}

TEST_F(SteamDeckOptions, deck_defaults_to_1280x800_fullscreen_with_a_larger_gui) {
    ::setenv("SteamDeck", "1", 1);
    auto o = parseArgs({});
    EXPECT_EQ(1280, o->graphics.width);
    EXPECT_EQ(800, o->graphics.height);
    EXPECT_TRUE(o->graphics.fullscreen);
    EXPECT_GT(o->graphics.guiScale, 1.0f);
}

TEST_F(SteamDeckOptions, deck_defaults_can_be_overridden) {
    ::setenv("SteamDeck", "1", 1);
    auto o = parseArgs({"--width", "1920", "--height", "1080", "--fullscreen", "0", "--guiscale", "1.5"});
    EXPECT_EQ(1920, o->graphics.width);
    EXPECT_EQ(1080, o->graphics.height);
    EXPECT_FALSE(o->graphics.fullscreen);
    EXPECT_FLOAT_EQ(1.5f, o->graphics.guiScale);
}

#endif
