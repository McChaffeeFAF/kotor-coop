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

// Linux install detection on synthetic directory trees (empty files; no game data).

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "reone/system/fileutil.h"
#include "reone/system/gamedetect.h"

using namespace reone;
namespace fs = std::filesystem;

namespace {

class GameDetect : public testing::Test {
protected:
    void SetUp() override {
        _root = fs::temp_directory_path() / (std::string("reone_gamedetect_") + testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(_root);
        home = _root / "home";
        fs::create_directories(home);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(_root, ec);
    }

    fs::path canon(const fs::path &p) const {
        return fs::weakly_canonical(p);
    }

    void touch(const fs::path &p) {
        fs::create_directories(p.parent_path());
        std::ofstream(p).flush();
    }

    void write(const fs::path &p, const std::string &text) {
        fs::create_directories(p.parent_path());
        std::ofstream(p) << text;
    }

    fs::path fakeGame(const fs::path &dir, const std::string &exe, const std::string &chitin = "chitin.key") {
        touch(dir / chitin);
        touch(dir / exe);
        return dir;
    }

    std::optional<GameInstall> first(KotorGame game, const std::vector<fs::path> &mounts = {}) {
        auto all = findGameInstalls(home, game, mounts);
        if (all.empty()) {
            return std::nullopt;
        }
        return all.front();
    }

    fs::path _root;
    fs::path home;
};

} // namespace

TEST(GameDetectVdf, library_folders_new_format) {
    const std::string vdf =
        "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"/home/u/.local/share/Steam\"\n\t\t\"label\"\t\t\"\"\n"
        "\t\t\"apps\"\n\t\t{\n\t\t\t\"32370\"\t\t\"1\"\n\t\t}\n\t}\n"
        "\t\"1\"\n\t{\n\t\t\"path\"\t\t\"/run/media/mmcblk0p1\"\n\t}\n}\n";
    auto paths = parseLibraryFolders(vdf);
    ASSERT_EQ(2u, paths.size());
    EXPECT_EQ("/home/u/.local/share/Steam", paths[0]);
    EXPECT_EQ("/run/media/mmcblk0p1", paths[1]);
}

TEST(GameDetectVdf, old_format_has_no_path_key_and_escapes_are_undone) {
    EXPECT_TRUE(parseLibraryFolders("\"LibraryFolders\"\n{\n\t\"TimeNextStatsReport\"\t\"1\"\n\t\"1\"\t\t\"/mnt/games\"\n}\n").empty());
    auto paths = parseLibraryFolders("\"path\" \"D:\\\\SteamLibrary\"\n\"path\" \"\"\n\"path\"\n");
    ASSERT_EQ(1u, paths.size());
    EXPECT_EQ("D:\\SteamLibrary", paths[0]);
}

TEST(GameDetectVdf, install_dir_from_manifest) {
    EXPECT_EQ("My KotOR", parseInstallDir("\"AppState\"\n{\n\t\"appid\"\t\"32370\"\n\t\"installdir\"\t\"My KotOR\"\n}\n"));
    EXPECT_EQ("", parseInstallDir("\"AppState\"\n{\n}\n"));
    EXPECT_EQ("", parseInstallDir(""));
}

TEST_F(GameDetect, native_steam_both_roots) {
    auto k1 = fakeGame(home / ".local/share/Steam/steamapps/common/swkotor", "swkotor.exe");
    auto k2 = fakeGame(home / ".steam/steam/steamapps/common/Knights of the Old Republic II", "swkotor2.exe");
    auto a = first(KotorGame::K1);
    ASSERT_TRUE(a);
    EXPECT_EQ(canon(k1), a->path);
    EXPECT_EQ("Steam", a->source);
    auto b = first(KotorGame::K2);
    ASSERT_TRUE(b);
    EXPECT_EQ(canon(k2), b->path);
}

TEST_F(GameDetect, dot_steam_symlink_is_one_root) {
    fakeGame(home / ".local/share/Steam/steamapps/common/swkotor", "swkotor.exe");
    fs::create_directories(home / ".steam");
    std::error_code ec;
    fs::create_directory_symlink(home / ".local/share/Steam", home / ".steam/steam", ec);
    if (ec) {
        GTEST_SKIP() << "symlinks unavailable: " << ec.message();
    }
    EXPECT_EQ(1u, steamRoots(home).size());
    EXPECT_EQ(1u, findGameInstalls(home, KotorGame::K1).size());
}

TEST_F(GameDetect, flatpak_steam) {
    auto k1 = fakeGame(home / ".var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/swkotor", "swkotor.exe");
    auto a = first(KotorGame::K1);
    ASSERT_TRUE(a);
    EXPECT_EQ(canon(k1), a->path);
    EXPECT_EQ("Steam (Flatpak)", a->source);
}

TEST_F(GameDetect, proton_install_is_the_windows_build_in_common) {
    // Steam on Linux installs the Windows build (swkotor.exe) and runs it with Proton; a compatdata
    // prefix sits beside it. The game folder is what matters.
    auto steam = home / ".local/share/Steam/steamapps";
    fs::create_directories(steam / "compatdata/32370/pfx/drive_c");
    auto k1 = fakeGame(steam / "common/swkotor", "swkotor.exe");
    auto a = first(KotorGame::K1);
    ASSERT_TRUE(a);
    EXPECT_EQ(canon(k1), a->path);
    EXPECT_FALSE(first(KotorGame::K2));
}

TEST_F(GameDetect, library_from_libraryfolders_vdf) {
    auto lib = _root / "drive2/SteamLibrary";
    fs::create_directories(lib / "steamapps/common");
    write(home / ".steam/steam/steamapps/libraryfolders.vdf",
          "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"" + (home / ".steam/steam").string() + "\"\n\t}\n"
          "\t\"1\"\n\t{\n\t\t\"path\"\t\t\"" + lib.string() + "\"\n\t}\n}\n");
    auto k2 = fakeGame(lib / "steamapps/common/Knights of the Old Republic II", "swkotor2.exe");
    EXPECT_EQ(2u, steamLibraries(home).size());
    auto a = first(KotorGame::K2);
    ASSERT_TRUE(a);
    EXPECT_EQ(canon(k2), a->path);
}

TEST_F(GameDetect, vdf_naming_a_missing_library_is_skipped) {
    write(home / ".steam/steam/steamapps/libraryfolders.vdf", "\"libraryfolders\"\n{\n\"1\"\n{\n\"path\" \"/nonexistent/lib\"\n}\n}\n");
    EXPECT_EQ(1u, steamLibraries(home).size());
}

TEST_F(GameDetect, renamed_folder_found_through_the_manifest) {
    auto apps = home / ".steam/steam/steamapps";
    auto k1 = fakeGame(apps / "common/My KotOR", "swkotor.exe");
    write(apps / "appmanifest_32370.acf", "\"AppState\"\n{\n\t\"appid\"\t\"32370\"\n\t\"installdir\"\t\"My KotOR\"\n}\n");
    auto a = first(KotorGame::K1);
    ASSERT_TRUE(a);
    EXPECT_EQ(canon(k1), a->path);
}

TEST_F(GameDetect, manifest_installdir_cannot_escape_common) {
    auto apps = home / ".steam/steam/steamapps";
    fs::create_directories(apps / "common");
    fakeGame(_root / "elsewhere", "swkotor.exe");
    write(apps / "appmanifest_32370.acf", "\"AppState\"\n{\n\t\"installdir\"\t\"../../../../elsewhere\"\n}\n");
    EXPECT_FALSE(first(KotorGame::K1));
}

TEST_F(GameDetect, mixed_case_folders_and_files) {
    auto k1 = fakeGame(home / ".local/share/Steam/SteamApps/Common/SWKOTOR", "SWKOTOR.EXE", "Chitin.KEY");
    auto a = first(KotorGame::K1);
    ASSERT_TRUE(a);
    EXPECT_EQ(canon(k1), a->path);
}

TEST_F(GameDetect, resource_folders_resolve_in_any_case) {
    auto game = fakeGame(home / "GOG Games/swkotor", "swkotor.exe");
    touch(game / "Override/placeholder.txt");
    touch(game / "StreamMusic/m.txt");
    auto o = findFileIgnoreCase(game, "override");
    ASSERT_TRUE(o);
    EXPECT_EQ("Override", o->filename().string());
    auto m = findFileIgnoreCase(game, "streammusic/m.txt");
    ASSERT_TRUE(m);
    EXPECT_TRUE(fs::exists(*m));
    EXPECT_FALSE(findFileIgnoreCase(game, "modules"));
    // The exact name wins over a case variant (only where the file system can hold both).
    if (fs::exists(game / "OVERRIDE")) {
        GTEST_SKIP() << "case-insensitive file system: Override and override are one folder";
    }
    touch(game / "override/other.txt");
    EXPECT_EQ("override", findFileIgnoreCase(game, "override")->filename().string());
}

TEST_F(GameDetect, folder_without_chitin_key_or_with_the_other_exe_is_not_an_install) {
    touch(home / ".local/share/Steam/steamapps/common/swkotor/swkotor.exe");
    EXPECT_FALSE(first(KotorGame::K1));
    fakeGame(home / ".local/share/Steam/steamapps/common/other", "swkotor.exe");
    EXPECT_TRUE(first(KotorGame::K1));
    EXPECT_FALSE(first(KotorGame::K2));
}

TEST_F(GameDetect, sd_card_library_on_a_mount) {
    fs::create_directories(home / ".steam/steam/steamapps");
    auto sd = _root / "run/media/mmcblk0p1";
    auto k1 = fakeGame(sd / "steamapps/common/swkotor", "swkotor.exe");
    EXPECT_FALSE(first(KotorGame::K1));
    auto a = first(KotorGame::K1, {sd});
    ASSERT_TRUE(a);
    EXPECT_EQ(canon(k1), a->path);
}

TEST_F(GameDetect, gog_default_folders) {
    auto k1 = fakeGame(home / "GOG Games/STAR WARS - Knights of the Old Republic", "swkotor.exe");
    auto k2 = fakeGame(home / "Games/Heroic/Star Wars Knights of the Old Republic II - The Sith Lords", "swkotor2.exe");
    auto a = first(KotorGame::K1);
    ASSERT_TRUE(a);
    EXPECT_EQ(canon(k1), a->path);
    EXPECT_EQ("GOG", a->source);
    auto b = first(KotorGame::K2);
    ASSERT_TRUE(b);
    EXPECT_EQ(canon(k2), b->path);
}

TEST_F(GameDetect, lutris_and_heroic_wine_prefixes) {
    auto k1 = fakeGame(home / "Games/kotor/drive_c/GOG Games/Star Wars - KotOR", "swkotor.exe");
    auto k2 = fakeGame(home / "Games/Heroic/Prefixes/kotor2/pfx/drive_c/Program Files (x86)/LucasArts/SWKotOR2", "swkotor2.exe");
    auto a = first(KotorGame::K1);
    ASSERT_TRUE(a);
    EXPECT_EQ(canon(k1), a->path);
    auto b = first(KotorGame::K2);
    ASSERT_TRUE(b);
    EXPECT_EQ(canon(k2), b->path);
}

TEST_F(GameDetect, flatpak_heroic) {
    auto k1 = fakeGame(home / ".var/app/com.heroicgameslauncher.hgl/Games/Heroic/Star Wars KotOR", "swkotor.exe");
    auto a = first(KotorGame::K1);
    ASSERT_TRUE(a);
    EXPECT_EQ(canon(k1), a->path);
}

TEST_F(GameDetect, steam_comes_before_gog) {
    auto steam = fakeGame(home / ".local/share/Steam/steamapps/common/swkotor", "swkotor.exe");
    fakeGame(home / "GOG Games/swkotor", "swkotor.exe");
    auto all = findGameInstalls(home, KotorGame::K1);
    ASSERT_EQ(2u, all.size());
    EXPECT_EQ(canon(steam), all[0].path);
    EXPECT_EQ("GOG", all[1].source);
}

TEST_F(GameDetect, nothing_installed) {
    EXPECT_TRUE(findGameInstalls(home, KotorGame::K1).empty());
    EXPECT_TRUE(findGameInstalls(_root / "no-such-home", KotorGame::K2).empty());
}

TEST(GameDetectDeck, recognised_by_environment_or_user) {
    EXPECT_TRUE(isSteamDeck("1", "someone"));
    EXPECT_TRUE(isSteamDeck(nullptr, "deck"));
    EXPECT_FALSE(isSteamDeck("0", "alice"));
    EXPECT_FALSE(isSteamDeck(nullptr, "alice"));
}
