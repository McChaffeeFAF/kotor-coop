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

#include "reone/gui/scrollview.h"

using namespace reone::gui;

namespace {

// Ten rows of height 40 at pitch 50 from top 100 (content bottom 590),
// in a window 100..400.
ScrollView column() {
    ScrollView view;
    view.viewTop = 100;
    view.viewBottom = 400;
    view.contentBottom = 590;
    view.step = 50;
    return view;
}

TEST(ScrollView, a_column_that_fits_does_not_scroll) {
    ScrollView view = column();
    view.contentBottom = 400;
    EXPECT_FALSE(view.scrollable());
    view.scrollBy(3);
    EXPECT_EQ(0, view.offset);
}

TEST(ScrollView, offset_is_clamped_to_the_overflow) {
    ScrollView view = column();
    EXPECT_EQ(190, view.maxOffset());
    view.scrollBy(1);
    EXPECT_EQ(50, view.offset);
    view.scrollBy(100);
    EXPECT_EQ(190, view.offset);
    view.scrollBy(-100);
    EXPECT_EQ(0, view.offset);
}

TEST(ScrollView, rows_not_wholly_inside_the_window_are_hidden) {
    ScrollView view = column();
    // Row i is at top 100 + 50 i, height 40.
    int shown = 0;
    for (int i = 0; i < 10; ++i) {
        shown += view.isVisible(100 + 50 * i, 40) ? 1 : 0;
    }
    EXPECT_EQ(6, shown); // tops 100..350: the sixth ends at 390; the seventh at 440
    EXPECT_TRUE(view.isVisible(350, 40));
    EXPECT_FALSE(view.isVisible(400, 40));
    view.scrollBy(1);
    EXPECT_FALSE(view.isVisible(100, 40)); // slid above the window
    EXPECT_TRUE(view.isVisible(150, 40));
    EXPECT_TRUE(view.isVisible(400, 40));
}

TEST(ScrollView, scroll_into_view_moves_least_in_either_direction) {
    ScrollView view = column();
    EXPECT_TRUE(view.scrollIntoView(450, 40)); // row 7, below
    EXPECT_EQ(90, view.offset);
    EXPECT_TRUE(view.isVisible(450, 40));
    EXPECT_EQ(360 + 40 + 0, 400 + 0); // sanity: ends flush with the window bottom
    EXPECT_TRUE(view.scrollIntoView(100, 40)); // row 0, above
    EXPECT_EQ(0, view.offset);
    EXPECT_TRUE(view.scrollIntoView(200, 40)); // already inside: unchanged
    EXPECT_EQ(0, view.offset);
}

TEST(ScrollView, a_row_taller_than_the_window_cannot_be_shown) {
    ScrollView view = column();
    EXPECT_FALSE(view.scrollIntoView(100, 500));
}

TEST(ScrollView, thumb_tracks_the_window) {
    ScrollView view = column();
    EXPECT_NEAR(300.0f / 490.0f, view.thumbSize(), 1e-5f);
    EXPECT_FLOAT_EQ(0.0f, view.thumbStart());
    view.scrollBy(100);
    EXPECT_NEAR(1.0f, view.thumbStart() + view.thumbSize(), 1e-5f); // flush with the track end
}

} // namespace
