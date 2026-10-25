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

#include <cstdint>
#include <vector>

#include <glm/vec2.hpp>

namespace reone {

namespace game {

/**
 * The "explored" grid over an area's map (RevealMap). Cells are addressed in
 * the map's normalized texture space, the same space the ARE Map struct's
 * MapPt values use.
 */
class MapExploration {
public:
    /** Size the grid; clears it. */
    void reset(int columns, int rows);

    /** Mark every cell explored. */
    void revealAll();

    /**
     * Mark the cells within `radius` grid steps (|dx| + |dy|) of the cell
     * holding normalized map point `mapPoint`. Points outside the map clamp
     * to its edge.
     */
    void reveal(const glm::vec2 &mapPoint, int radius);

    bool isExplored(int column, int row) const;
    int exploredCount() const;

    /** The party's reveal radius each tick, in grid steps. */
    static constexpr int kPartyRevealRadius = 1;

    /**
     * The grid as saved (ARE/GIT "AreaMap"): one bit per cell, row-major,
     * least significant bit first, packed into 32-bit words.
     */
    std::vector<uint32_t> packed() const;
    /** Restore a packed grid; false (and no change) if the size differs. */
    bool unpack(int columns, int rows, const std::vector<uint32_t> &words);

    /**
     * The grid for the wire (co-op shared map): one bit per cell, row-major,
     * least significant bit first, (columns * rows + 7) / 8 bytes.
     */
    std::vector<uint8_t> bytes() const;
    /** Replace the grid from bytes(); false (and no change) if the size differs. */
    bool setBytes(int columns, int rows, const std::vector<uint8_t> &bytes);
    /** Explore every cell `bytes` has explored; false (and no change) if the size differs. */
    bool mergeBytes(int columns, int rows, const std::vector<uint8_t> &bytes);

    int columns() const { return _columns; }
    int rows() const { return _rows; }

private:
    int _columns {0};
    int _rows {0};
    std::vector<bool> _cells;
};

} // namespace game

} // namespace reone
