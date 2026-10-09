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

#include "reone/resource/types.h"
#include "reone/system/types.h"

namespace reone {

namespace texgen {

struct TextureSource {
    std::string name; ///< lower-case resource name
    resource::ResType type {resource::ResType::Tpc}; ///< Tpc or Tga
    std::string origin; ///< where it came from, for messages
    std::function<ByteBuffer()> read;
    bool fromOverride {false}; ///< a loose file of the override folder, where generated maps may sit
};

/**
 * The textures of an install, by name. Where several places hold one name the first
 * wins, in the engine's order: override, modules, texture packs, then the key/BIF archives;
 * a TPC beats a TGA of the same place.
 */
class SourceSet {
public:
    std::map<std::string, TextureSource> textures;
    std::map<std::string, std::function<std::optional<ByteBuffer>()>> txis;
    std::vector<std::string> warnings;

    /// Adds the texture unless the name is taken.
    bool add(TextureSource source);
    void addTxi(const std::string &name, std::function<std::optional<ByteBuffer>()> read);
    bool has(const std::string &name) const { return textures.count(name) != 0; }
};

/**
 * Finds the textures under a KotOR install directory (chitin.key and its BIFs, texturepacks,
 * modules, override), or in one file: a KEY, ERF/MOD, RIM, TGA or TPC.
 */
SourceSet scanSources(const std::filesystem::path &path);

/// An in-memory source (tests, tools).
TextureSource memorySource(const std::string &name, resource::ResType type, ByteBuffer bytes);

} // namespace texgen

} // namespace reone
