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

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace reone {

enum class KotorGame {
    K1,
    K2
};

/** A KotOR install found on disk: where it came from and its folder. */
struct GameInstall {
    std::string source; /**< "Steam", "Steam (Flatpak)", "GOG" */
    std::filesystem::path path;
};

/**
 * Library paths named by a Steam libraryfolders.vdf (old and new formats):
 * the value of every "path" key, with VDF backslash escapes undone.
 */
std::vector<std::string> parseLibraryFolders(std::string_view vdf);

/** The "installdir" value of a Steam appmanifest_*.acf, or empty. */
std::string parseInstallDir(std::string_view acf);

/**
 * Whether dir is an install of the game: chitin.key and the game's exe
 * (swkotor.exe, swkotor2.exe) are present, names matched ignoring case. The
 * Windows exe is what Steam installs on Linux too (it runs under Proton); only
 * the data files are read.
 */
bool isGameInstall(const std::filesystem::path &dir, KotorGame game);

/** Steam installation roots that exist under home (native, Flatpak, legacy symlinks). */
std::vector<std::filesystem::path> steamRoots(const std::filesystem::path &home);

/**
 * Every Steam library's steamapps folder: each root's own, those named by
 * libraryfolders.vdf, and those of any extra mount points (Steam Deck SD card).
 */
std::vector<std::filesystem::path> steamLibraries(const std::filesystem::path &home,
                                                  const std::vector<std::filesystem::path> &mounts = {});

/**
 * Installs of the game, most reliable first: Steam libraries (the appmanifest's
 * installdir, then any folder under common), then the default prefixes and
 * folders of GOG/Heroic/Lutris/Wine. Only folders that pass isGameInstall are
 * returned; duplicates are removed. The first entry is the one to use.
 */
std::vector<GameInstall> findGameInstalls(const std::filesystem::path &home, KotorGame game,
                                          const std::vector<std::filesystem::path> &mounts = {});

/** Mount points worth scanning for a Steam library on a real machine (SD card, /mnt). */
std::vector<std::filesystem::path> defaultMounts(const std::string &user);

/** Whether this is a Steam Deck: the SteamDeck environment variable is 1, or the user is "deck". */
bool isSteamDeck(const char *steamDeckEnv, const std::string &user);

} // namespace reone
