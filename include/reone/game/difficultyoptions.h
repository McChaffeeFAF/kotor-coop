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

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace reone {

namespace resource {

class ITwoDAs;

} // namespace resource

namespace game {

struct DifficultyOption {
    int nameStrRef {-1};
    std::string description;
    float damageMultiplier {1.0f};
};

/** diffsettings.2da rows (design note k1hud_parity, "Difficulty"). */
enum class DifficultySetting {
    NoCriticalOnPC = 0,
    NoAoOWithRanged = 1,
    NoAoOWithPotion = 2,
    MinPCDamagePercent = 3,
    MaxNPCDamagePercent = 4,
    MinHP1 = 5
};

/** diffsettings.2da value columns, in order: dmeasy, easy, normal, hardcore, dmplayers. */
constexpr int kDiffSettingsColumnCount = 5;
constexpr int kDiffSettingsRowCount = 6;

/**
 * The diffsettings column the game reads. K2 sets it once to "normal" and
 * never changes it; the player's Easy/Normal/Difficult option does not choose
 * it (that option picks the difficultyopt.2da damage multiplier instead).
 */
constexpr int kDiffSettingsColumn = 2;

class IDifficultyOptions {
public:
    virtual ~IDifficultyOptions() = default;

    virtual const DifficultyOption &get(int difficulty) const = 0;

    /**
     * A diffsettings.2da value at the game's column. 0 (an empty cell, or no
     * table) means the setting is off, which leaves every rule unchanged.
     */
    virtual int setting(DifficultySetting setting) const { return 0; }
};

/**
 * The diffsettings damage-percent rule, applied to one roll of a weapon's (or
 * an unarmed strike's) damage dice. roll is the dice total, maxRoll the most
 * those dice can show. randomBelow(n) returns a uniform integer in [0, n).
 * - A PC hitting a non-PC, with MinPCDamagePercent p > 0: the roll is raised
 *   by a random p..99 percent of maxRoll, capped at maxRoll (p = 100 gives
 *   maxRoll outright).
 * - A non-PC hitting a PC, with MaxNPCDamagePercent p > 0: below 100 the roll
 *   drops by (100 - p) percent of maxRoll, never under 1; above 100 it gains a
 *   random 0..(p - 101) percent of maxRoll; 100 leaves it alone.
 * - Otherwise the roll is unchanged. No random draw is made unless a rule
 *   applies.
 */
/**
 * The diffsettings MinHP1 rule for damage to a PC: when on, damage never
 * takes currentHitPoints below 1 (and is never negative).
 */
int applyMinHP1(const IDifficultyOptions &options, int damage, int currentHitPoints);

int adjustDamageRollForDifficulty(const IDifficultyOptions &options, int roll, int maxRoll,
                                  bool attackerIsPC, bool targetIsPC,
                                  const std::function<int(int)> &randomBelow);

class DifficultyOptions : public IDifficultyOptions, boost::noncopyable {
public:
    explicit DifficultyOptions(resource::ITwoDAs &twoDas) :
        _twoDas(twoDas) {
    }

    void init();

    const DifficultyOption &get(int difficulty) const override;
    int setting(DifficultySetting setting) const override;
    /** A diffsettings.2da value at any column (0-4). */
    int setting(DifficultySetting setting, int column) const;

private:
    resource::ITwoDAs &_twoDas;
    std::vector<DifficultyOption> _options;
    std::array<std::array<int, kDiffSettingsRowCount>, kDiffSettingsColumnCount> _settings {};
};

} // namespace game

} // namespace reone
