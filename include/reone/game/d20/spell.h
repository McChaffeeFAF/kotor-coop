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

#include "reone/graphics/texture.h"

#include "../types.h"

namespace reone {

namespace audio {
class AudioClip;
}

namespace graphics {
class Model;
}

namespace game {

class Spell {
public:
    SpellType type;
    std::string name;
    std::string description;
    std::shared_ptr<graphics::Texture> icon;
    uint32_t pips {1};
    std::vector<SpellType> prerequisites;
    std::optional<SpellType> masterSpell;
    int userType {-1};
    std::unordered_map<ClassType, int> classLevelRequirements;
    uint32_t category {0};  // talent category code (spells.2da category), 0 if none
    int maxCR {0};          // spells.2da maxcr, used to rank talents
    uint32_t exclusion {0}; // spells.2da exclusion bits (0x01 droid-only, 0x02 organic-only)
    std::string impactScript;
    std::string castAnim;
    std::shared_ptr<audio::AudioClip> castSound;
    float conjTime {0.0f};
    float castTime {0.0f};
    uint32_t itemTargeting {0};
    bool hostile {false};
    float range {0.0f};          // spells.2da range letter resolved through ranges.2da (0: personal / unknown)
    int darkRecommendation {0};  // spells.2da dark_recom: Recommended order for dark-side characters (1 first)
    int lightRecommendation {0}; // spells.2da light_recom: the same for light-side characters
    int forcePoints {0};         // spells.2da forcepoints: base force point cost (0: free)
    char goodEvil {0};           // spells.2da goodevil: 'G' light side, 'E' dark side, 0 neither
    std::shared_ptr<graphics::Model> projModel;

    std::optional<int> getClassLevelRequirement(ClassType clazz) const {
        auto maybeRequirement = classLevelRequirements.find(clazz);
        return maybeRequirement != classLevelRequirements.end()
                   ? std::optional<int>(maybeRequirement->second)
                   : std::nullopt;
    }
};

} // namespace game

} // namespace reone
