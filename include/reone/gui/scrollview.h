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

#include <algorithm>

namespace reone {

namespace gui {

/**
 * The vertical arithmetic of a column of rows scrolling inside a window.
 * Rows are authored at content positions; scrolling slides them up. A row is
 * shown only when it lies wholly inside the window, so nothing ever draws over
 * what is beyond the window's edges.
 */
struct ScrollView {
    int viewTop {0};       /**< window top, in the rows' coordinates */
    int viewBottom {0};    /**< window bottom */
    int contentBottom {0}; /**< bottom of the lowest row, unscrolled */
    int step {1};          /**< a wheel notch or a pad press moves this far */
    int offset {0};

    int maxOffset() const { return std::max(0, contentBottom - viewBottom); }
    bool scrollable() const { return maxOffset() > 0; }

    void scrollBy(int rows) { offset = std::clamp(offset + rows * step, 0, maxOffset()); }
    void clamp() { offset = std::clamp(offset, 0, maxOffset()); }

    int shiftedTop(int top) const { return top - offset; }
    bool isVisible(int top, int height) const {
        int shifted = shiftedTop(top);
        return shifted >= viewTop && shifted + height <= viewBottom;
    }

    /** Scroll the least that shows the row; false if it cannot be shown. */
    bool scrollIntoView(int top, int height) {
        if (height > viewBottom - viewTop) {
            return false;
        }
        if (shiftedTop(top) < viewTop) {
            offset = top - viewTop;
        } else if (shiftedTop(top) + height > viewBottom) {
            offset = top + height - viewBottom;
        }
        clamp();
        return isVisible(top, height);
    }

    /** Thumb position and size as fractions of the track, 0..1. */
    float thumbSize() const {
        int total = (viewBottom - viewTop) + maxOffset();
        return total > 0 ? static_cast<float>(viewBottom - viewTop) / static_cast<float>(total) : 1.0f;
    }
    float thumbStart() const {
        int total = (viewBottom - viewTop) + maxOffset();
        return total > 0 ? static_cast<float>(offset) / static_cast<float>(total) : 0.0f;
    }
};

} // namespace gui

} // namespace reone
