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

#include "reone/game/usersettings.h"

#include "reone/system/logutil.h"

#include <fstream>
#include <sstream>

namespace reone {

namespace game {

namespace {

std::string trimmed(const std::string &s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

} // namespace

const std::map<std::string, std::string> &UserSettings::builtinDefaults() {
    static const std::map<std::string, std::string> defaults {
        {"audio.movie", "62"},
        {"audio.music", "39"},
        {"audio.sound", "60"},
        {"audio.voice", "89"},
        {"graphics.antialiasing", "2"},
        {"optgameplay.CB_DISABLEMOVE", "1"},
        {"optgameplay.CB_LEVELUP", "0"},
        {"optgameplay.CB_CAMEOS", "1"},
        {"optgameplay.CB_SMARTCOMPANIONS", "1"},
        {"optgraphicsadv.CB_AMBIENTOCC", "1"},
        {"optgraphicsadv.CB_AUTOEXPOSURE", "1"},
        {"optgraphicsadv.CB_BLOOM", "1"},
        {"optgraphicsadv.CB_CHROMATIC", "0"},
        {"optgraphicsadv.CB_DOF", "0"},
        {"optgraphicsadv.CB_FILMGRAIN", "0"},
        {"optgraphicsadv.CB_GRADING", "1"},
        {"optgraphicsadv.CB_LENSFLARES", "0"},
        {"optgraphicsadv.CB_MOTIONBLUR", "0"},
        {"optgraphicsadv.CB_NORMALMAPS", "1"},
        {"optgraphicsadv.CB_REFLECTIONS", "1"},
        {"optgraphicsadv.CB_RESHADE", "0"},
        {"optgraphicsadv.CB_SABERFX", "1"},
        {"optgraphicsadv.CB_SHARPEN", "1"},
        {"optgraphicsadv.CB_SOFTPARTICLES", "1"},
        {"optgraphicsadv.CB_SOFTSHADOWS", "1"},
        {"optgraphicsadv.CB_SSGI", "0"},
        {"optgraphicsadv.CB_TONEMAP", "1"},
        {"optgraphicsadv.CB_VIGNETTE", "1"},
        {"optgraphicsadv.CB_VOLUMETRICS", "1"},
    };
    return defaults;
}

std::optional<std::string> UserSettings::get(const std::string &key) const {
    auto it = _values.find(key);
    if (it == _values.end()) {
        const auto &defaults = builtinDefaults();
        auto def = defaults.find(key);
        if (def == defaults.end()) {
            return std::nullopt;
        }
        return def->second;
    }
    return it->second;
}

bool UserSettings::getBool(const std::string &key, bool defValue) const {
    auto value = get(key);
    if (!value) {
        return defValue;
    }
    return *value == "1" || *value == "true" || *value == "on";
}

int UserSettings::getInt(const std::string &key, int defValue) const {
    auto value = get(key);
    if (!value) {
        return defValue;
    }
    try {
        return std::stoi(*value);
    } catch (const std::exception &) {
        return defValue;
    }
}

float UserSettings::getFloat(const std::string &key, float defValue) const {
    auto value = get(key);
    if (!value) {
        return defValue;
    }
    try {
        return std::stof(*value);
    } catch (const std::exception &) {
        return defValue;
    }
}

void UserSettings::set(const std::string &key, std::string value) {
    _values[key] = std::move(value);
}

void UserSettings::setFloat(const std::string &key, float value) {
    std::ostringstream stream;
    stream << value;
    set(key, stream.str());
}

void UserSettings::erase(const std::string &key) {
    _values.erase(key);
}

void UserSettings::read(std::istream &input) {
    for (std::string line; std::getline(input, line);) {
        auto text = trimmed(line);
        if (text.empty() || text[0] == '#' || text[0] == ';') {
            continue;
        }
        auto eq = text.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        auto key = trimmed(text.substr(0, eq));
        if (!key.empty()) {
            _values[key] = trimmed(text.substr(eq + 1));
        }
    }
}

void UserSettings::write(std::ostream &output) const {
    output << "# reone player settings (options screens, key bindings)\n";
    for (const auto &[key, value] : _values) {
        output << key << '=' << value << '\n';
    }
}

void UserSettings::load(const std::filesystem::path &path) {
    _path = path;
    if (path.empty()) {
        return;
    }
    std::ifstream input(path);
    if (input) {
        read(input);
    }
}

void UserSettings::save() const {
    if (_path.empty()) {
        return;
    }
    auto temporary = _path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            warn("Settings: cannot write " + temporary.string());
            return;
        }
        write(output);
    }
    std::error_code ec;
    std::filesystem::rename(temporary, _path, ec);
    if (ec) {
        warn("Settings: cannot replace " + _path.string() + ": " + ec.message());
    }
}

} // namespace game

} // namespace reone
