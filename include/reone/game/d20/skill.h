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

namespace game {

struct Skill {
    std::string name;
    std::string description;
    std::shared_ptr<graphics::Texture> icon;
    bool untrained {true};                  // usable with no ranks (skills.2da untrained)
    Ability keyAbility {Ability::Intelligence}; // modifier added to ranks (keyability)
    bool armorCheckPenalty {false};         // body armour penalty applies (armorcheckpenalty)
    bool droidCanUse {true};                // droids have the skill (droidcanuse)
    bool npcCanUse {true};                  // creatures outside the party have it (npccanuse)
};

struct SkillUser {
    int ranks {0};
    int keyAbilityModifier {0};
    bool droid {false};
    bool partyMember {true};
    int armorCheckPenalty {0}; // of the equipped body armour, >= 0
};

/**
 * The value GetSkillRank reports: -1 when the creature cannot have the
 * skill at all (a droid and droidcanuse=0, or a non-party creature and
 * npccanuse=0), 0 when the skill needs training and the creature has no
 * ranks, otherwise ranks + key ability modifier - armour check penalty.
 */
int computeSkillValue(const Skill &skill, const SkillUser &user);

} // namespace game

} // namespace reone
