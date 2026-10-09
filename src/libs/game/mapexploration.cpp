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

#include "reone/game/mapexploration.h"

namespace reone {

namespace game {

void MapExploration::reset(int columns, int rows) {
    _columns = std::max(1, columns);
    _rows = std::max(1, rows);
    _cells.assign(static_cast<size_t>(_columns) * _rows, false);
}

void MapExploration::revealAll() {
    std::fill(_cells.begin(), _cells.end(), true);
}

void MapExploration::reveal(const glm::vec2 &mapPoint, int radius) {
    if (_cells.empty() || radius < 0) {
        return;
    }
    int centreColumn = std::clamp(static_cast<int>(std::floor(mapPoint.x * _columns)), 0, _columns - 1);
    int centreRow = std::clamp(static_cast<int>(std::floor(mapPoint.y * _rows)), 0, _rows - 1);
    for (int dx = -radius; dx <= radius; ++dx) {
        int span = radius - std::abs(dx);
        for (int dy = -span; dy <= span; ++dy) {
            int column = std::clamp(centreColumn + dx, 0, _columns - 1);
            int row = std::clamp(centreRow + dy, 0, _rows - 1);
            _cells[static_cast<size_t>(row) * _columns + column] = true;
        }
    }
}

bool MapExploration::isExplored(int column, int row) const {
    if (column < 0 || row < 0 || column >= _columns || row >= _rows) {
        return false;
    }
    return _cells[static_cast<size_t>(row) * _columns + column];
}

std::vector<uint32_t> MapExploration::packed() const {
    std::vector<uint32_t> words(_cells.size() / 32 + 1, 0u);
    for (size_t i = 0; i < _cells.size(); ++i) {
        if (_cells[i]) {
            words[i / 32] |= 1u << (i % 32);
        }
    }
    return words;
}

bool MapExploration::unpack(int columns, int rows, const std::vector<uint32_t> &words) {
    if (columns != _columns || rows != _rows || words.size() * 32 < _cells.size()) {
        return false;
    }
    for (size_t i = 0; i < _cells.size(); ++i) {
        _cells[i] = (words[i / 32] >> (i % 32)) & 1u;
    }
    return true;
}

int MapExploration::exploredCount() const {
    return static_cast<int>(std::count(_cells.begin(), _cells.end(), true));
}

} // namespace game

} // namespace reone
