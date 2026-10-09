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

#include "reone/game/experience.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>

#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"

using namespace reone::resource;

namespace reone {

namespace game {

static constexpr char kExpTableResRef[] = "exptable";

std::optional<int> experienceToReachLevel(ITwoDAs &twoDas, int level) {
    if (level < 1) {
        return std::nullopt;
    }
    std::shared_ptr<TwoDA> table;
    try {
        table = twoDas.get(kExpTableResRef);
    } catch (const std::exception &) {
        table.reset();
    }
    int row = level - 1;
    if (!table || row >= table->getRowCount()) {
        return std::nullopt;
    }
    const std::string &cell = table->getString(row, "xp");
    if (cell.empty()) {
        return std::nullopt;
    }
    unsigned long long value = 0;
    try {
        value = std::stoull(cell, nullptr, 0);
    } catch (const std::exception &) {
        return std::nullopt;
    }
    // K1 ends the table with 0xFFFFFFFF: that level cannot be reached.
    if (value > static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

int experienceForNextLevel(ITwoDAs &twoDas, int currentLevel) {
    std::shared_ptr<TwoDA> table;
    try {
        table = twoDas.get(kExpTableResRef);
    } catch (const std::exception &) {
        table.reset();
    }
    if (!table || table->getRowCount() == 0) {
        return currentLevel * (currentLevel + 1) * 500;
    }
    auto needed = experienceToReachLevel(twoDas, currentLevel + 1);
    return needed ? *needed : std::numeric_limits<int>::max();
}

int levelForExperience(ITwoDAs &twoDas, int experience) {
    int level = 1;
    for (int candidate = 2;; ++candidate) {
        auto needed = experienceToReachLevel(twoDas, candidate);
        if (!needed || experience < *needed) {
            break;
        }
        level = candidate;
    }
    return level;
}

static std::shared_ptr<TwoDA> tryTable(ITwoDAs &twoDas, const char *resRef) {
    try {
        return twoDas.get(resRef);
    } catch (const std::exception &) {
        return nullptr;
    }
}

int killExperience(ITwoDAs &twoDas, int referenceLevel, float challengeRating) {
    auto xpTable = tryTable(twoDas, "xptable");
    if (!xpTable || xpTable->getRowCount() == 0) {
        return 0;
    }
    int row = std::clamp(referenceLevel - 1, 0, xpTable->getRowCount() - 1);
    int cr = std::max(0, static_cast<int>(challengeRating));
    size_t column = static_cast<size_t>(cr) + 1;
    const auto &columns = xpTable->columns();
    if (column >= columns.size()) {
        return 0;
    }
    double amount = xpTable->getInt(row, columns[column], 0);
    // npc.2da GAME_XP_General scales every award in K2; K1 ships no such
    // row, so its awards are unscaled.
    if (auto npc = tryTable(twoDas, "npc")) {
        for (int i = 0; i < npc->getRowCount(); ++i) {
            if (npc->getString(i, "label") == "GAME_XP_General" ||
                npc->getString(i, "tag") == "GAME_XP_General") {
                amount = amount * npc->getInt(i, "percentxp", 100) / 100.0;
                break;
            }
        }
    }
    return static_cast<int>(std::ceil(amount));
}

} // namespace game

} // namespace reone
