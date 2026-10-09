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
#include <iosfwd>
#include <map>
#include <optional>
#include <string>

namespace reone {

namespace game {

/**
 * The player's own settings (options screens, key bindings), kept as text
 * "key=value" lines in a file of our own next to reone.cfg in the build
 * folder. Never written to a game install. Unknown keys are kept.
 */
class UserSettings {
public:
    static constexpr const char *kFilename = "reone_settings.ini";

    bool has(const std::string &key) const { return _values.count(key) > 0; }
    std::optional<std::string> get(const std::string &key) const;
    bool getBool(const std::string &key, bool defValue) const;
    int getInt(const std::string &key, int defValue) const;
    float getFloat(const std::string &key, float defValue) const;

    void set(const std::string &key, std::string value);
    void setBool(const std::string &key, bool value) { set(key, value ? "1" : "0"); }
    void setInt(const std::string &key, int value) { set(key, std::to_string(value)); }
    void setFloat(const std::string &key, float value);
    void erase(const std::string &key);

    const std::map<std::string, std::string> &values() const { return _values; }

    void read(std::istream &input);
    void write(std::ostream &output) const;

    /** Load from a file; a missing file leaves the settings empty. */
    void load(const std::filesystem::path &path);
    /** Save to the path set by load; no-op when it is empty. */
    void save() const;
    const std::filesystem::path &path() const { return _path; }
    void setPath(std::filesystem::path path) { _path = std::move(path); }

private:
    std::map<std::string, std::string> _values;
    std::filesystem::path _path;
};

} // namespace game

} // namespace reone
