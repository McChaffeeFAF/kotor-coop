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

#include "reone/game/d20/skills.h"

#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/strings.h"

using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace game {

static Ability parseKeyAbility(const std::string &value) {
    std::string key(boost::to_upper_copy(value));
    if (key == "STR") {
        return Ability::Strength;
    }
    if (key == "DEX") {
        return Ability::Dexterity;
    }
    if (key == "CON") {
        return Ability::Constitution;
    }
    if (key == "WIS") {
        return Ability::Wisdom;
    }
    if (key == "CHA") {
        return Ability::Charisma;
    }
    return Ability::Intelligence;
}

int computeSkillValue(const Skill &skill, const SkillUser &user) {
    if ((user.droid && !skill.droidCanUse) || (!user.partyMember && !skill.npcCanUse)) {
        return -1;
    }
    if (!skill.untrained && user.ranks <= 0) {
        return 0;
    }
    int value = user.ranks + user.keyAbilityModifier;
    if (skill.armorCheckPenalty) {
        value -= user.armorCheckPenalty;
    }
    return value;
}

int allocateRecommendedSkills(
    const std::vector<SkillType> &order,
    std::map<SkillType, int> &ranks,
    int points,
    const std::function<int(SkillType)> &pointCost,
    const std::function<int(SkillType)> &maxRank) {

    for (auto skill : order) {
        int cost = pointCost(skill);
        if (cost <= 0) {
            continue;
        }
        int limit = maxRank(skill);
        while (points >= cost && ranks[skill] < limit) {
            ++ranks[skill];
            points -= cost;
        }
    }
    return points;
}

void Skills::init() {
    std::shared_ptr<TwoDA> skills(_twoDas.get("skills"));
    if (!skills) {
        return;
    }

    for (int row = 0; row < skills->getRowCount(); ++row) {
        std::string name(_strings.getText(skills->getInt(row, "name", -1)));
        std::string description(_strings.getText(skills->getInt(row, "description", -1)));
        std::shared_ptr<Texture> icon(_textures.get(skills->getString(row, "icon"), TextureUsage::GUI));

        auto skill = std::make_shared<Skill>();
        skill->name = std::move(name);
        skill->description = std::move(description);
        skill->icon = std::move(icon);
        skill->untrained = skills->getInt(row, "untrained", 1) != 0;
        skill->keyAbility = parseKeyAbility(skills->getString(row, "keyability"));
        skill->armorCheckPenalty = skills->getInt(row, "armorcheckpenalty", 0) != 0;
        skill->droidCanUse = skills->getInt(row, "droidcanuse", 1) != 0;
        skill->npcCanUse = skills->getInt(row, "npccanuse", 1) != 0;
        _skills.insert(std::make_pair(static_cast<SkillType>(row), std::move(skill)));
    }
}

std::shared_ptr<Skill> Skills::get(SkillType type) const {
    auto it = _skills.find(type);
    return it != _skills.end() ? it->second : nullptr;
}

} // namespace game

} // namespace reone
