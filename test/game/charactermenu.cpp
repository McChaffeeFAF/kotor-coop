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

// The character screen's alignment backdrop: charrec_light's poses are
// "evil", "align1" to "align19" and "good", one per 5 points of good/evil.

#include <gtest/gtest.h>

#include "reone/game/gui/ingame/character.h"

using namespace reone::game;

TEST(CharacterMenu, alignment_backdrop_poses_cover_every_band) {
    EXPECT_EQ("evil", CharacterMenu::alignmentAnimation(0));
    EXPECT_EQ("evil", CharacterMenu::alignmentAnimation(4));
    EXPECT_EQ("align1", CharacterMenu::alignmentAnimation(5));
    EXPECT_EQ("align9", CharacterMenu::alignmentAnimation(49));
    EXPECT_EQ("align10", CharacterMenu::alignmentAnimation(50));
    EXPECT_EQ("align19", CharacterMenu::alignmentAnimation(99));
    EXPECT_EQ("good", CharacterMenu::alignmentAnimation(100));
}

TEST(CharacterMenu, alignment_backdrop_clamps_out_of_range_values) {
    EXPECT_EQ("evil", CharacterMenu::alignmentAnimation(-20));
    EXPECT_EQ("good", CharacterMenu::alignmentAnimation(140));
}
