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

#include "reone/game/difficultyoptions.h"

#include "reone/game/twodautil.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/system/exception/validation.h"

#include <algorithm>
#include <utility>

namespace reone {

namespace game {

static constexpr int kRequiredDifficultyOptionCount = 4;
static constexpr int kDefaultDifficultyOptionRow = 3;

void DifficultyOptions::init() {
    auto table = getRequiredTwoDA(_twoDas, "difficultyopt");
    if (table->getRowCount() < kRequiredDifficultyOptionCount) {
        throw ValidationException(
            "difficultyopt.2da requires at least " +
            std::to_string(kRequiredDifficultyOptionCount) + " rows");
    }

    std::vector<DifficultyOption> options;
    options.reserve(table->getRowCount());

    for (int row = 0; row < table->getRowCount(); ++row) {
        DifficultyOption option;
        option.nameStrRef = table->getInt(row, "name", -1);
        option.description = table->getString(row, "desc");

        auto multiplier = table->getFloatOpt(row, "multiplier");
        if (multiplier) {
            option.damageMultiplier = *multiplier;
        } else if (row != kDefaultDifficultyOptionRow) {
            throw ValidationException(
                "difficultyopt.2da multiplier missing at row " +
                std::to_string(row));
        }

        options.push_back(std::move(option));
    }

    _options = std::move(options);

    // diffsettings.2da: optional. A missing table or an empty cell reads 0,
    // which turns that setting off.
    _settings = {};
    static constexpr const char *kColumns[kDiffSettingsColumnCount] = {
        "dmeasy", "easy", "normal", "hardcore", "dmplayers"};
    if (auto settings = _twoDas.get("diffsettings")) {
        const int rows = std::min(settings->getRowCount(), kDiffSettingsRowCount);
        for (int column = 0; column < kDiffSettingsColumnCount; ++column) {
            for (int row = 0; row < rows; ++row) {
                _settings[column][row] = settings->getInt(row, kColumns[column], 0);
            }
        }
    }
}

int DifficultyOptions::setting(DifficultySetting setting) const {
    return this->setting(setting, kDiffSettingsColumn);
}

int DifficultyOptions::setting(DifficultySetting setting, int column) const {
    int row = static_cast<int>(setting);
    if (column < 0 || column >= kDiffSettingsColumnCount || row < 0 || row >= kDiffSettingsRowCount) {
        return 0;
    }
    return _settings[column][row];
}

int applyMinHP1(const IDifficultyOptions &options, int damage, int currentHitPoints) {
    if (options.setting(DifficultySetting::MinHP1) == 1 && currentHitPoints - damage < 1) {
        return std::max(0, currentHitPoints - 1);
    }
    return damage;
}

int adjustDamageRollForDifficulty(const IDifficultyOptions &options, int roll, int maxRoll,
                                  bool attackerIsPC, bool targetIsPC,
                                  const std::function<int(int)> &randomBelow) {
    if (maxRoll <= 0) {
        return roll;
    }
    if (attackerIsPC && !targetIsPC) {
        int percent = options.setting(DifficultySetting::MinPCDamagePercent);
        if (percent <= 0) {
            return roll;
        }
        if (percent >= 100 || percent * maxRoll / 100 == maxRoll) {
            return maxRoll;
        }
        int raised = (randomBelow(100 - percent) + percent) * maxRoll / 100 + roll;
        return std::min(raised, maxRoll);
    }
    if (!attackerIsPC && targetIsPC) {
        int percent = options.setting(DifficultySetting::MaxNPCDamagePercent);
        if (percent <= 0 || percent == 100) {
            return roll;
        }
        if (percent < 100) {
            return std::max(1, roll - (100 - percent) * maxRoll / 100);
        }
        return roll + randomBelow(percent - 100) * maxRoll / 100;
    }
    return roll;
}

const DifficultyOption &DifficultyOptions::get(int difficulty) const {
    if (difficulty < 0 || difficulty >= static_cast<int>(_options.size())) {
        throw ValidationException(
            "difficultyopt.2da row out of range: " +
            std::to_string(difficulty) + "/" +
            std::to_string(_options.size()));
    }
    return _options[difficulty];
}

} // namespace game

} // namespace reone
