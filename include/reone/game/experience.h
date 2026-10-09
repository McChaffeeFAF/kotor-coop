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

namespace reone {

namespace resource {
class ITwoDAs;
} // namespace resource

namespace game {

// Experience rules read from the game's tables (design note c11_data).

/**
 * exptable.2da: row i holds the experience a creature needs to reach level
 * i + 1. Returns nothing when the table has no row for that level or marks
 * it unreachable (the 0xFFFFFFFF past the last level), or has no table.
 */
std::optional<int> experienceToReachLevel(resource::ITwoDAs &twoDas, int level);

/**
 * Experience a creature at currentLevel needs for its next level. At the
 * last level the table defines this is the largest int, so a level-up is
 * never pending. Without an exptable the K1 values (level * (level + 1) *
 * 500) are used.
 */
int experienceForNextLevel(resource::ITwoDAs &twoDas, int currentLevel);

/**
 * The level an experience total reaches: the highest exptable row whose
 * threshold it meets (at least 1). Kill XP is read at this level of the
 * party's experience, not at the class levels (K2 behaviour).
 */
int levelForExperience(resource::ITwoDAs &twoDas, int experience);

/**
 * XP for killing a creature (K2 behaviour, design note c11_data): the xptable cell at
 * row referenceLevel - 1 (clamped to the table) and column position CR + 1
 * (the CR truncated toward zero; column 0 is the level label, so CR 0 reads
 * "c0"), times npc.2da's GAME_XP_General percent when the table has that
 * row, rounded up. 0 when the cell is missing.
 */
int killExperience(resource::ITwoDAs &twoDas, int referenceLevel, float challengeRating);

} // namespace game

} // namespace reone
