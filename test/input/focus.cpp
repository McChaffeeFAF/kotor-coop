/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#include "reone/input/focus.h"

using namespace reone::input;

static Event keyDown(KeyCode c) {
    return Event::newKeyDown(KeyEvent(true, c, 0, false));
}
static Event keyUp(KeyCode c) {
    return Event::newKeyUp(KeyEvent(false, c, 0, false));
}
static Event down(MouseButton b) {
    return Event::newMouseButtonDown(MouseButtonEvent(b, true, 1, 5, 5));
}
static Event up(MouseButton b) {
    return Event::newMouseButtonUp(MouseButtonEvent(b, false, 1, 5, 5));
}

TEST(FocusTracker, LosingFocusReleasesHeldKeysAndButtons) {
    FocusTracker t;
    t.observe(keyDown(KeyCode::W));
    t.observe(down(MouseButton::Right));
    t.observe(keyDown(KeyCode::A));
    t.observe(keyUp(KeyCode::A));
    auto releases = t.loseFocus(-1, -1);
    ASSERT_EQ(2u, releases.size());
    int keyUps = 0, buttonUps = 0;
    for (auto &e : releases) {
        if (e.type == EventType::KeyUp) {
            ++keyUps;
            EXPECT_EQ(KeyCode::W, e.key.code);
        } else if (e.type == EventType::MouseButtonUp) {
            ++buttonUps;
            EXPECT_EQ(MouseButton::Right, e.button.button);
            EXPECT_EQ(-1, e.button.x);
        }
    }
    EXPECT_EQ(1, keyUps);
    EXPECT_EQ(1, buttonUps);
    EXPECT_FALSE(t.focused());
    EXPECT_EQ(0u, t.heldKeys());
    EXPECT_EQ(0u, t.heldButtons());
}

TEST(FocusTracker, SecondLossAndRegainAreQuiet) {
    FocusTracker t;
    t.observe(down(MouseButton::Left));
    EXPECT_EQ(1u, t.loseFocus(0, 0).size());
    EXPECT_TRUE(t.loseFocus(0, 0).empty());
    t.gainFocus();
    EXPECT_TRUE(t.focused());
    EXPECT_TRUE(t.loseFocus(0, 0).empty());
}

TEST(FocusTracker, NothingHeldMeansNothingReleased) {
    FocusTracker t;
    t.observe(down(MouseButton::Left));
    t.observe(up(MouseButton::Left));
    EXPECT_TRUE(t.loseFocus(0, 0).empty());
}

TEST(FocusTracker, RelativeMouseOnlyWhenNothingNeedsAPointer) {
    EXPECT_TRUE(FocusTracker::allowRelativeMouse(true, false, true));
    EXPECT_FALSE(FocusTracker::allowRelativeMouse(false, false, true));
    EXPECT_FALSE(FocusTracker::allowRelativeMouse(true, true, true));  // a pop-up needs a pointer
    EXPECT_FALSE(FocusTracker::allowRelativeMouse(true, false, false)); // not focused
}

TEST(FocusTracker, UnderAPopUpOnlyReleasesReachTheGameAndMotionMovesThePointer) {
    EXPECT_TRUE(FocusTracker::reachesGameUnderModal(EventType::MouseButtonUp));
    EXPECT_TRUE(FocusTracker::reachesGameUnderModal(EventType::KeyUp));
    EXPECT_FALSE(FocusTracker::reachesGameUnderModal(EventType::MouseButtonDown));
    EXPECT_FALSE(FocusTracker::reachesGameUnderModal(EventType::KeyDown));
    EXPECT_FALSE(FocusTracker::reachesGameUnderModal(EventType::MouseMotion));
    // The pointer the game draws follows the mouse over the Accept / Decline pop-up.
    EXPECT_TRUE(FocusTracker::movesPointerUnderModal(EventType::MouseMotion));
    EXPECT_FALSE(FocusTracker::movesPointerUnderModal(EventType::MouseButtonDown));
}

TEST(FocusTracker, OnlyASessionOthersDependOnKeepsRunningWithoutFocus) {
    // Single player, windowed: sleeps without focus, as before.
    EXPECT_TRUE(FocusTracker::sleepsUnfocused(false, false, false, false, false));
    EXPECT_FALSE(FocusTracker::sleepsUnfocused(true, false, false, false, false));
    EXPECT_FALSE(FocusTracker::sleepsUnfocused(false, false, false, true, false)); // --run-unfocused
    // A co-op host (or dedicated host) keeps simulating for its guests.
    EXPECT_FALSE(FocusTracker::sleepsUnfocused(false, false, false, false, true));
    EXPECT_TRUE(FocusTracker::throttlesUnfocused(false, true));
    EXPECT_FALSE(FocusTracker::throttlesUnfocused(true, true));
    EXPECT_FALSE(FocusTracker::throttlesUnfocused(false, false));
    // Headless: only the harness's injected loss counts.
    EXPECT_FALSE(FocusTracker::sleepsUnfocused(false, true, false, false, false));
    EXPECT_TRUE(FocusTracker::sleepsUnfocused(false, true, true, false, false));
    EXPECT_FALSE(FocusTracker::sleepsUnfocused(false, true, true, false, true));
}
