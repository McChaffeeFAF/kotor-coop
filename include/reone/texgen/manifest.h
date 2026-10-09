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

namespace reone {

namespace texgen {

struct ManifestEntry {
    std::string texture;
    std::string className;
    std::string status; ///< "generated" or "skipped"
    std::string reason; ///< why a texture was skipped
    float strength {0.0f};
    std::string sourceHash; ///< hash of the source bytes and of the settings that made the outputs
};

/**
 * What a run produced, per texture, so a later run can leave unchanged textures alone.
 * Stored as JSON: {"tool": "texgen", "version": 1, "textures": [{texture, class, status, reason, strength, sourceHash}]}.
 */
class Manifest {
public:
    std::map<std::string, ManifestEntry> entries;

    /// An absent file is an empty manifest; a damaged one throws.
    static Manifest load(const std::filesystem::path &path);
    /// Written to a temporary file and renamed, so a crash leaves the old manifest.
    void save(const std::filesystem::path &path) const;

    std::string toText() const;
    static Manifest fromText(const std::string &text);
};

/// FNV-1a, 64 bits, as 16 hex digits. Not cryptographic; it only notices changes.
class Hasher {
public:
    void add(const void *data, size_t size);
    void add(const std::string &text) { add(text.data(), text.size()); }
    std::string hex() const;

private:
    uint64_t _state {1469598103934665603ull};
};

} // namespace texgen

} // namespace reone
