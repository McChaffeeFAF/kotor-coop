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

#include <filesystem>
#include <optional>

namespace reone {

namespace resource {

/**
 * Where save slots live when it is not the game install's "saves" folder.
 * The engine points this at the saves folder beside its executable so that
 * no save is ever written into a game install.
 */
inline std::optional<std::filesystem::path> &savesRootOverride() {
    static std::optional<std::filesystem::path> root;
    return root;
}

} // namespace resource

} // namespace reone
