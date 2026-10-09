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
#include <set>

namespace reone {

namespace game {

/**
 * Per-dialog participant locks set by SetLockOrientationInDialog and
 * SetLockHeadFollowInDialog. An object in `orientation` is not turned to
 * face the speaker; one in `headFollow` does not turn its head to follow
 * the speaker. Cleared when a conversation ends.
 */
struct DialogLocks {
    std::set<uint32_t> orientation;
    std::set<uint32_t> headFollow;

    static void set(std::set<uint32_t> &locks, uint32_t objectId, bool locked) {
        if (locked) {
            locks.insert(objectId);
        } else {
            locks.erase(objectId);
        }
    }

    bool isOrientationLocked(uint32_t objectId) const { return orientation.count(objectId) != 0; }
    bool isHeadFollowLocked(uint32_t objectId) const { return headFollow.count(objectId) != 0; }

    void clear() {
        orientation.clear();
        headFollow.clear();
    }
};

} // namespace game

} // namespace reone
