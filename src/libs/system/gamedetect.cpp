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

#include "reone/system/gamedetect.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>

#include "reone/system/fileutil.h"

namespace reone {

namespace fs = std::filesystem;

static std::string lower(std::string s) {
    for (auto &c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// The value of every `"key" "value"` pair in a VDF/ACF text.
static std::vector<std::string> vdfValues(std::string_view text, std::string_view key) {
    std::vector<std::string> values;
    std::string quotedKey = "\"" + std::string(key) + "\"";
    size_t pos = 0;
    while ((pos = text.find(quotedKey, pos)) != std::string_view::npos) {
        size_t cur = pos + quotedKey.size();
        pos = cur;
        while (cur < text.size() && (text[cur] == ' ' || text[cur] == '\t')) {
            ++cur;
        }
        if (cur >= text.size() || text[cur] != '"') {
            continue;
        }
        ++cur;
        std::string value;
        while (cur < text.size() && text[cur] != '"' && text[cur] != '\n') {
            if (text[cur] == '\\' && cur + 1 < text.size() && (text[cur + 1] == '\\' || text[cur + 1] == '"')) {
                ++cur;
            }
            value += text[cur++];
        }
        if (cur < text.size() && text[cur] == '"') {
            values.push_back(std::move(value));
        }
    }
    return values;
}

std::vector<std::string> parseLibraryFolders(std::string_view vdf) {
    std::vector<std::string> paths;
    for (auto &p : vdfValues(vdf, "path")) {
        if (!p.empty()) {
            paths.push_back(std::move(p));
        }
    }
    return paths;
}

std::string parseInstallDir(std::string_view acf) {
    auto values = vdfValues(acf, "installdir");
    return values.empty() ? std::string() : values.front();
}

static bool isDir(const fs::path &p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

// A (possibly nested, "/"-separated) entry of dir, matched ignoring case.
static std::optional<fs::path> child(const fs::path &dir, const std::string &name) {
    if (!isDir(dir)) {
        return std::nullopt;
    }
    try {
        return findFileIgnoreCase(dir, lower(name));
    } catch (const fs::filesystem_error &) {
        return std::nullopt;
    }
}

static std::vector<fs::path> subdirs(const fs::path &dir) {
    std::vector<fs::path> result;
    if (!isDir(dir)) {
        return result;
    }
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_directory(e2)) {
            result.push_back(it->path());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

static std::string readAll(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static fs::path canon(const fs::path &p) {
    std::error_code ec;
    auto c = fs::weakly_canonical(p, ec);
    return ec ? p : c;
}

bool isGameInstall(const fs::path &dir, KotorGame game) {
    if (!isDir(dir)) {
        return false;
    }
    return child(dir, "chitin.key") && child(dir, game == KotorGame::K1 ? "swkotor.exe" : "swkotor2.exe");
}

static const char *const kSteamRoots[] = {
    ".steam/steam",
    ".steam/root",
    ".local/share/Steam",
    ".var/app/com.valvesoftware.Steam/.local/share/Steam",
    ".var/app/com.valvesoftware.Steam/.steam/steam",
};

static void addUnique(std::vector<fs::path> &list, const fs::path &p) {
    auto c = canon(p);
    if (std::find(list.begin(), list.end(), c) == list.end()) {
        list.push_back(c);
    }
}

std::vector<fs::path> steamRoots(const fs::path &home) {
    std::vector<fs::path> roots;
    for (auto rel : kSteamRoots) {
        auto root = home / rel;
        if (child(root, "steamapps")) {
            addUnique(roots, root);
        }
    }
    return roots;
}

std::vector<fs::path> steamLibraries(const fs::path &home, const std::vector<fs::path> &mounts) {
    std::vector<fs::path> libs;
    for (const auto &root : steamRoots(home)) {
        auto apps = child(root, "steamapps");
        if (!apps) {
            continue;
        }
        addUnique(libs, *apps);
        if (auto vdf = child(*apps, "libraryfolders.vdf")) {
            for (const auto &p : parseLibraryFolders(readAll(*vdf))) {
                if (auto lib = child(fs::path(p), "steamapps")) {
                    addUnique(libs, *lib);
                }
            }
        }
    }
    for (const auto &mount : mounts) {
        if (auto lib = child(mount, "steamapps")) {
            addUnique(libs, *lib);
        }
        // A library can sit in a folder of the mount.
        if (auto lib = child(mount, "SteamLibrary/steamapps")) {
            addUnique(libs, *lib);
        }
    }
    return libs;
}

static bool isFlatpakPath(const fs::path &p) {
    // generic_string: forward slashes on every platform (Windows paths use backslashes).
    return p.generic_string().find("/.var/app/com.valvesoftware.Steam/") != std::string::npos;
}

static const char *const kK1Names[] = {"swkotor", "Star Wars Knights of the Old Republic", "Star Wars - Knights of the Old Republic"};
static const char *const kK2Names[] = {"Knights of the Old Republic II", "swkotor2", "Star Wars Knights of the Old Republic II",
                                       "Star Wars - Knights of the Old Republic II - The Sith Lords"};

std::vector<GameInstall> findGameInstalls(const fs::path &home, KotorGame game, const std::vector<fs::path> &mounts) {
    std::vector<GameInstall> found;
    auto add = [&](const std::string &source, const fs::path &dir) {
        if (!isGameInstall(dir, game)) {
            return;
        }
        auto c = canon(dir);
        for (const auto &f : found) {
            if (f.path == c) {
                return;
            }
        }
        found.push_back({source, c});
    };

    // Steam: native, Flatpak, other libraries, SD card. The Windows build that Proton runs has
    // the same folder layout (steamapps/common/swkotor with swkotor.exe), so the files are
    // found the same way.
    int appId = game == KotorGame::K1 ? 32370 : 208580;
    for (const auto &apps : steamLibraries(home, mounts)) {
        std::string source = isFlatpakPath(apps) ? "Steam (Flatpak)" : "Steam";
        auto common = child(apps, "common");
        if (!common) {
            continue;
        }
        if (auto manifest = child(apps, "appmanifest_" + std::to_string(appId) + ".acf")) {
            auto dir = parseInstallDir(readAll(*manifest));
            if (!dir.empty() && isSafePathComponent(dir)) {
                if (auto p = child(*common, dir)) {
                    add(source, *p);
                }
            }
        }
        for (auto n : game == KotorGame::K1 ? std::vector<const char *>(std::begin(kK1Names), std::end(kK1Names))
                                            : std::vector<const char *>(std::begin(kK2Names), std::end(kK2Names))) {
            if (auto p = child(*common, n)) {
                add(source, *p);
            }
        }
        // Localised store names are many; what identifies an install is chitin.key plus the exe.
        for (const auto &d : subdirs(*common)) {
            add(source, d);
        }
    }

    // Non-Steam stores: GOG's installer folders, the default Heroic and Lutris locations (also
    // their Flatpak builds) and their Wine prefixes, where the game sits under drive_c.
    static const char *const kGogRoots[] = {
        "GOG Games",
        "Games/GOG",
        "Games/gog",
        "Games/Heroic",
        "Games/Heroic/Prefixes",
        "Games/Heroic/Prefixes/default",
        "Games",
        ".wine/drive_c/GOG Games",
        ".wine/drive_c/Program Files (x86)/GOG Galaxy/Games",
        ".var/app/com.heroicgameslauncher.hgl/Games/Heroic",
        ".var/app/com.heroicgameslauncher.hgl/Games/Heroic/Prefixes/default",
        ".var/app/com.heroicgameslauncher.hgl/Games",
        ".var/app/net.lutris.Lutris/Games",
    };
    static const char *const kPrefixBases[] = {
        "GOG Games",
        "Program Files (x86)",
        "Program Files",
        "Program Files (x86)/GOG Galaxy/Games",
        "Program Files (x86)/LucasArts",
        "Program Files/LucasArts",
        "Games",
    };
    for (auto rel : kGogRoots) {
        fs::path root = home / rel;
        if (!isDir(root)) {
            continue;
        }
        add("GOG", root);
        for (const auto &d : subdirs(root)) {
            add("GOG", d);
            // <prefix>/drive_c/<base>/<game>, as Lutris makes it; Heroic and Proton keep it under pfx/.
            for (const auto &prefix : {d, d / "pfx"}) {
                auto driveC = child(prefix, "drive_c");
                if (!driveC) {
                    continue;
                }
                for (auto sub : kPrefixBases) {
                    if (auto base = child(*driveC, sub)) {
                        add("GOG", *base);
                        for (const auto &g : subdirs(*base)) {
                            add("GOG", g);
                        }
                    }
                }
            }
        }
    }
    return found;
}

std::vector<fs::path> defaultMounts(const std::string &user) {
    std::vector<fs::path> mounts {"/run/media/mmcblk0p1"};
    auto addChildren = [&](const fs::path &dir) {
        for (const auto &d : subdirs(dir)) {
            mounts.push_back(d);
        }
    };
    addChildren("/run/media/deck");
    if (!user.empty()) {
        addChildren(fs::path("/run/media") / user);
        addChildren(fs::path("/media") / user);
    }
    addChildren("/mnt");
    return mounts;
}

bool isSteamDeck(const char *steamDeckEnv, const std::string &user) {
    return (steamDeckEnv && std::string(steamDeckEnv) == "1") || user == "deck";
}

} // namespace reone
