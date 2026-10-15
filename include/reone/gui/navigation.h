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

#include <optional>
#include <vector>

#include "reone/input/event.h"

namespace reone {

namespace gui {

/** A control as focus navigation sees it: where it is on screen. */
struct NavItem {
    int id {0};
    int left {0};
    int top {0};
    int width {0};
    int height {0};

    int centerX() const { return left + width / 2; }
    int centerY() const { return top + height / 2; }
};

/**
 * Where focus goes from the current control when a direction is pressed: the nearest control
 * that lies that way, preferring the ones in line (a control straight below beats a nearer one
 * far off to the side). Nothing focused yet: the first control in reading order, whichever
 * direction. No control that way: focus stays (nullopt when there is nothing at all).
 * Returns the id of the item to focus.
 */
std::optional<int> navigateFocus(const std::vector<NavItem> &items, std::optional<int> current, input::NavigateAction direction);

/**
 * Flattens a tree of controls into the items navigation chooses among: visible, enabled,
 * selectable nodes (a node that is not visible hides its children). Templated on the tree so
 * it works on any node type (`children` gives a node's child pointers); `root` itself is skipped.
 */
template <class Node, class Children, class Visible, class Enabled, class Selectable, class ItemOf>
std::vector<NavItem> collectNavItems(Node &root, Children children, Visible visible, Enabled enabled, Selectable selectable, ItemOf itemOf) {
    std::vector<NavItem> result;
    // Depth first in authored order: the stack holds the nodes still to visit, the next one last.
    std::vector<Node *> stack;
    auto push = [&](Node &parent) {
        auto kids = children(parent);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
            stack.push_back(*it);
        }
    };
    push(root);
    while (!stack.empty()) {
        Node *node = stack.back();
        stack.pop_back();
        if (!visible(*node)) {
            continue;
        }
        if (enabled(*node) && selectable(*node)) {
            result.push_back(itemOf(*node));
        }
        push(*node);
    }
    return result;
}

} // namespace gui

} // namespace reone
