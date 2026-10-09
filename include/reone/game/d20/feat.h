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

#pragma once

#include <functional>
#include <memory>
#include <set>
#include <vector>

#include "reone/graphics/texture.h"

#include "../types.h"

namespace reone {

namespace game {

struct Feat {
    std::string name;
    std::string description;
    std::shared_ptr<graphics::Texture> icon;
    uint32_t minCharLevel {0};
    FeatType preReqFeat1 {FeatType::Invalid};
    FeatType preReqFeat2 {FeatType::Invalid};
    FeatType successor {FeatType::Invalid};
    uint32_t pips {1}; // 1-3, position in a feat chain
    uint32_t category {0};  // talent category code (feat.2da category), 0 if none
    int maxCR {0};          // feat.2da maxcr, used to rank talents
    uint32_t exclusion {0}; // feat.2da exclusion bits
    int useType {-1};       // feat.2da usetype: 0 melee attack feat, 1 ranged, -1 neither
};

/**
 * The attack feats a creature can pick from the target menu for its weapon
 * type: feats whose usetype matches (0 melee, 1 ranged), showing only the
 * highest owned tier of each chain (a feat whose successor is also owned is
 * left out). Sorted by feat id.
 */
std::vector<FeatType> selectAttackFeats(
    const std::set<FeatType> &owned,
    bool rangedWeapon,
    const std::function<std::shared_ptr<Feat>(FeatType)> &lookup);

} // namespace game

} // namespace reone
