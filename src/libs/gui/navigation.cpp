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

#include "reone/gui/navigation.h"

#include <cstdlib>
#include <limits>

namespace reone {

namespace gui {

std::optional<int> navigateFocus(const std::vector<NavItem> &items, std::optional<int> current, input::NavigateAction direction) {
    if (items.empty()) {
        return std::nullopt;
    }
    const NavItem *from = nullptr;
    if (current) {
        for (const auto &item : items) {
            if (item.id == *current) {
                from = &item;
                break;
            }
        }
    }
    if (!from) {
        // Reading order: top to bottom, left to right.
        const NavItem *first = &items.front();
        for (const auto &item : items) {
            if (item.top < first->top || (item.top == first->top && item.left < first->left)) {
                first = &item;
            }
        }
        return first->id;
    }
    if (direction == input::NavigateAction::Accept) {
        return from->id;
    }
    const NavItem *best = nullptr;
    long bestScore = std::numeric_limits<long>::max();
    for (const auto &item : items) {
        if (item.id == from->id) {
            continue;
        }
        long dx = item.centerX() - from->centerX();
        long dy = item.centerY() - from->centerY();
        long along = 0;
        long across = 0;
        switch (direction) {
        case input::NavigateAction::Up:
            along = -dy;
            across = dx;
            break;
        case input::NavigateAction::Down:
            along = dy;
            across = dx;
            break;
        case input::NavigateAction::Left:
            along = -dx;
            across = dy;
            break;
        default:
            along = dx;
            across = dy;
            break;
        }
        if (along <= 0) {
            continue;
        }
        long score = along + 2 * std::labs(across);
        if (score < bestScore || (score == bestScore && best && item.id < best->id)) {
            bestScore = score;
            best = &item;
        }
    }
    return best ? best->id : from->id;
}

} // namespace gui

} // namespace reone
