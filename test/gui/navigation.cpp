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

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "reone/gui/navigation.h"

using namespace reone;
using namespace reone::gui;
using input::NavigateAction;

namespace {

/** A synthetic control tree: just the facts navigation reads. */
struct Node {
    int id {0};
    int left {0};
    int top {0};
    int width {10};
    int height {10};
    bool visible {true};
    bool enabled {true};
    bool selectable {true};
    std::vector<Node> children;
};

std::vector<NavItem> flatten(Node &root) {
    return collectNavItems(
        root,
        [](Node &node) {
            std::vector<Node *> kids;
            for (auto &child : node.children) {
                kids.push_back(&child);
            }
            return kids;
        },
        [](const Node &node) { return node.visible; },
        [](const Node &node) { return node.enabled; },
        [](const Node &node) { return node.selectable; },
        [](const Node &node) { return NavItem {node.id, node.left, node.top, node.width, node.height}; });
}

std::optional<int> go(const std::vector<NavItem> &items, std::optional<int> from, NavigateAction direction) {
    return navigateFocus(items, from, direction);
}

// A 3x2 grid of buttons: ids 1..3 in the top row, 4..6 below.
std::vector<NavItem> grid() {
    std::vector<NavItem> items;
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 3; ++column) {
            items.push_back({1 + row * 3 + column, column * 100, row * 50, 80, 30});
        }
    }
    return items;
}

} // namespace

TEST(MenuNavigation, nothing_focused_starts_at_the_first_control_in_reading_order) {
    auto items = grid();
    std::reverse(items.begin(), items.end());
    for (auto direction : {NavigateAction::Up, NavigateAction::Down, NavigateAction::Left, NavigateAction::Right, NavigateAction::Accept}) {
        EXPECT_EQ(1, go(items, std::nullopt, direction));
    }
    EXPECT_EQ(1, go(items, 999, NavigateAction::Down)) << "a focus that is no longer there counts as none";
}

TEST(MenuNavigation, focus_moves_to_the_neighbour_in_that_direction) {
    auto items = grid();
    EXPECT_EQ(2, go(items, 1, NavigateAction::Right));
    EXPECT_EQ(3, go(items, 2, NavigateAction::Right));
    EXPECT_EQ(2, go(items, 3, NavigateAction::Left));
    EXPECT_EQ(5, go(items, 2, NavigateAction::Down));
    EXPECT_EQ(2, go(items, 5, NavigateAction::Up));
    EXPECT_EQ(4, go(items, 1, NavigateAction::Down));
    EXPECT_EQ(6, go(items, 5, NavigateAction::Right));
}

TEST(MenuNavigation, at_the_edge_focus_stays) {
    auto items = grid();
    EXPECT_EQ(1, go(items, 1, NavigateAction::Left));
    EXPECT_EQ(1, go(items, 1, NavigateAction::Up));
    EXPECT_EQ(3, go(items, 3, NavigateAction::Right));
    EXPECT_EQ(6, go(items, 6, NavigateAction::Down));
}

TEST(MenuNavigation, a_control_in_line_beats_a_nearer_one_off_to_the_side) {
    // From the top one, "down" prefers the wide button straight below to a nearer small one off to the right.
    std::vector<NavItem> items {
        {1, 0, 0, 100, 20},
        {2, 150, 25, 20, 20}, // close below, far to the right
        {3, 0, 80, 100, 20},  // straight below, farther
    };
    EXPECT_EQ(3, go(items, 1, NavigateAction::Down));
    EXPECT_EQ(2, go(items, 1, NavigateAction::Right));
}

TEST(MenuNavigation, a_single_column_of_buttons_walks_up_and_down) {
    std::vector<NavItem> items;
    for (int i = 0; i < 5; ++i) {
        items.push_back({10 + i, 20, i * 40, 200, 30});
    }
    std::optional<int> focus;
    focus = go(items, focus, NavigateAction::Down);
    EXPECT_EQ(10, focus);
    for (int expected = 11; expected <= 14; ++expected) {
        focus = go(items, focus, NavigateAction::Down);
        EXPECT_EQ(expected, focus);
    }
    EXPECT_EQ(14, go(items, focus, NavigateAction::Down));
    EXPECT_EQ(13, go(items, focus, NavigateAction::Up));
    EXPECT_EQ(14, go(items, focus, NavigateAction::Left)) << "no column to the left";
}

TEST(MenuNavigation, no_controls_means_no_focus) {
    EXPECT_FALSE(go({}, std::nullopt, NavigateAction::Down));
    EXPECT_FALSE(go({}, 3, NavigateAction::Accept));
}

TEST(MenuNavigation, accept_keeps_the_focus_where_it_is) {
    auto items = grid();
    EXPECT_EQ(5, go(items, 5, NavigateAction::Accept));
}

TEST(MenuNavigation, a_tree_is_flattened_to_visible_enabled_selectable_controls_in_authored_order) {
    Node root;
    root.selectable = false;
    root.children = {
        Node {1, 0, 0},
        Node {2, 0, 20, 10, 10, true, false},        // disabled: skipped
        Node {3, 0, 40, 10, 10, true, true, false},  // not selectable: skipped, but its children count
        Node {4, 0, 60, 10, 10, false},              // hidden: skipped with everything under it
        Node {5, 0, 80},
    };
    root.children[2].children = {Node {30, 0, 41}, Node {31, 0, 42}};
    root.children[3].children = {Node {40, 0, 61}};
    auto items = flatten(root);
    std::vector<int> ids;
    for (const auto &item : items) {
        ids.push_back(item.id);
    }
    EXPECT_EQ((std::vector<int> {1, 30, 31, 5}), ids);
}

TEST(MenuNavigation, focus_moves_through_a_tree_and_skips_what_cannot_be_used) {
    Node root;
    root.selectable = false;
    Node panel;
    panel.selectable = false;
    panel.id = 100;
    panel.children = {Node {1, 0, 0, 100, 30}, Node {2, 0, 40, 100, 30, true, false}, Node {3, 0, 80, 100, 30}};
    root.children = {panel, Node {4, 200, 0, 60, 30}};
    auto items = flatten(root);
    EXPECT_EQ(3, go(items, 1, NavigateAction::Down)) << "the disabled button in between is stepped over";
    EXPECT_EQ(1, go(items, 3, NavigateAction::Up));
    EXPECT_EQ(4, go(items, 1, NavigateAction::Right));
    EXPECT_EQ(1, go(items, 4, NavigateAction::Left));
}
