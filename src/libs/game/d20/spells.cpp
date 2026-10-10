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

#include "reone/game/d20/spells.h"

#include <cctype>

#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/strings.h"

#include "reone/game/d20/attributes.h"
#include "reone/game/d20/class.h"

using namespace reone::graphics;
using namespace reone::audio;
using namespace reone::resource;

namespace reone {

namespace game {

static const std::vector<std::pair<ClassType, std::string>> kClassLevelColumns = {
    {ClassType::JediGuardian, "guardian"},
    {ClassType::JediConsular, "consular"},
    {ClassType::JediSentinel, "sentinel"},
    {ClassType::JediWeaponMaster, "weapmstr"},
    {ClassType::JediMaster, "jedimaster"},
    {ClassType::JediWatchman, "watchman"},
    {ClassType::SithMarauder, "marauder"},
    {ClassType::SithLord, "sithlord"},
    {ClassType::SithAssassin, "assassin"}};

static std::optional<SpellType> parseSpellType(const std::string &value) {
    if (value.empty()) {
        return std::nullopt;
    }

    try {
        size_t parsedLength = 0;
        int parsed = std::stoi(value, &parsedLength);
        if (parsedLength == value.size() && parsed >= 0) {
            return static_cast<SpellType>(parsed);
        }
    } catch (const std::exception &) {
    }

    return std::nullopt;
}

static std::vector<SpellType> parsePrerequisites(const std::string &value) {
    std::vector<SpellType> result;
    std::vector<std::string> tokens;
    boost::split(tokens, value, boost::is_any_of("_"));
    for (const auto &token : tokens) {
        auto maybeSpell = parseSpellType(token);
        if (maybeSpell) {
            result.push_back(*maybeSpell);
        }
    }
    return result;
}

static bool isPickerVisible(const Spell &spell, const CreatureClass &clazz) {
    auto maybeRequirement = spell.getClassLevelRequirement(clazz.type());
    return spell.userType == 1 && maybeRequirement && *maybeRequirement >= 0;
}

static bool prerequisitesSatisfied(
    const Spell &spell,
    const CreatureAttributes &attributes,
    const std::set<SpellType> &chosen) {
    return std::all_of(spell.prerequisites.begin(), spell.prerequisites.end(), [&](SpellType prerequisite) {
        return attributes.hasSpell(prerequisite) || chosen.count(prerequisite) > 0;
    });
}

static int getVisualDepth(
    SpellType type,
    const std::unordered_map<SpellType, std::shared_ptr<Spell>> &spells,
    const std::set<SpellType> &visible,
    std::unordered_map<SpellType, int> &depths,
    std::set<SpellType> &visited) {
    auto maybeDepth = depths.find(type);
    if (maybeDepth != depths.end()) {
        return maybeDepth->second;
    }
    if (!visited.insert(type).second) {
        return 0;
    }

    int result = 0;
    auto spell = spells.at(type);
    for (auto prerequisite : spell->prerequisites) {
        if (visible.count(prerequisite) > 0) {
            result = std::max(result, getVisualDepth(prerequisite, spells, visible, depths, visited) + 1);
        }
    }

    visited.erase(type);
    depths.emplace(type, result);
    return result;
}

static std::optional<SpellType> getDisplayParent(
    const Spell &spell,
    const std::set<SpellType> &visible,
    const std::unordered_map<SpellType, int> &depths) {
    std::optional<SpellType> result;
    int deepest = -1;
    for (auto prerequisite : spell.prerequisites) {
        auto maybeDepth = depths.find(prerequisite);
        if (visible.count(prerequisite) > 0 && maybeDepth != depths.end() && maybeDepth->second > deepest) {
            result = prerequisite;
            deepest = maybeDepth->second;
        }
    }
    return result;
}

static SpellType getChainRoot(
    SpellType type,
    const std::unordered_map<SpellType, std::optional<SpellType>> &parents) {
    std::set<SpellType> visited;
    SpellType result = type;
    while (visited.insert(result).second) {
        auto maybeParent = parents.find(result);
        if (maybeParent == parents.end() || !maybeParent->second) {
            break;
        }
        result = *maybeParent->second;
    }
    return result;
}

std::vector<SpellType> pickRecommendedPowers(
    const ISpells &spells,
    const CreatureAttributes &attributes,
    const CreatureClass &clazz,
    int count,
    bool darkSide) {

    std::vector<SpellType> result;
    std::set<SpellType> chosen;
    while (static_cast<int>(result.size()) < count) {
        std::optional<SpellType> best;
        int bestRank = 0;
        for (auto type : spells.getLevelUpCandidates(attributes, clazz, chosen)) {
            auto spell = spells.get(type);
            int rank = spell ? (darkSide ? spell->darkRecommendation : spell->lightRecommendation) : 0;
            if (rank > 0 && (!best || rank < bestRank)) {
                best = type;
                bestRank = rank;
            }
        }
        if (!best) {
            break;
        }
        result.push_back(*best);
        chosen.insert(*best);
    }
    return result;
}

int getSpellRangeRow(const std::string &letter) {
    // ranges.2da labels these rows SpellRngPers, SpellRngTouch, SpellRngShrt,
    // SpellRngMed, SpellRngLng and SpellRngThrow.
    std::string key(boost::to_upper_copy(letter));
    if (key == "P") {
        return 0;
    }
    if (key == "T") {
        return 1;
    }
    if (key == "S") {
        return 2;
    }
    if (key == "M") {
        return 3;
    }
    if (key == "L") {
        return 4;
    }
    if (key == "W") {
        return 19;
    }
    return -1;
}

void Spells::init() {
    std::shared_ptr<TwoDA> spells(_twoDas.get("spells"));
    if (!spells)
        return;
    std::shared_ptr<TwoDA> ranges(_twoDas.get("ranges"));

    for (int row = 0; row < spells->getRowCount(); ++row) {
        SpellType type = static_cast<SpellType>(row);
        std::string name(_strings.getText(spells->getInt(row, "name", -1)));
        std::string description(_strings.getText(spells->getInt(row, "spelldesc", -1)));
        std::shared_ptr<Texture> icon(_textures.get(spells->getString(row, "iconresref"), TextureUsage::GUI));
        uint32_t pips = spells->getHexInt(row, "pips");
        std::vector<SpellType> prerequisites(parsePrerequisites(spells->getString(row, "prerequisites")));
        std::optional<SpellType> masterSpell(parseSpellType(spells->getString(row, "masterspell")));
        int userType = spells->getInt(row, "usertype", -1);
        uint32_t category = spells->getHexInt(row, "category");
        int maxCR = spells->getInt(row, "maxcr");
        uint32_t exclusion = spells->getHexInt(row, "exclusion");
        std::string impactScript = spells->getString(row, "impactscript");
        std::string castAnim = spells->getString(row, "castanim");
        std::string castSound = spells->getString(row, "castsound");
        float conjTime = spells->getInt(row, "conjtime") / 1000.0f;
        float castTime = spells->getInt(row, "casttime") / 1000.0f;
        uint32_t itemTargeting = spells->getInt(row, "itemtargeting");
        bool hostile = spells->getBool(row, "hostilesetting");
        std::string projModel = spells->getString(row, "projmodel");

        auto spell = std::make_shared<Spell>();
        spell->type = type;
        spell->name = std::move(name);
        spell->label = spells->getString(row, "label");
        spell->description = std::move(description);
        spell->icon = std::move(icon);
        spell->pips = pips;
        spell->prerequisites = std::move(prerequisites);
        spell->masterSpell = masterSpell;
        spell->userType = userType;
        for (const auto &[clazz, column] : kClassLevelColumns) {
            auto maybeRequirement = spells->getIntOpt(row, column);
            if (maybeRequirement) {
                spell->classLevelRequirements.emplace(clazz, *maybeRequirement);
            }
        }
        spell->category = category;
        spell->maxCR = maxCR;
        spell->exclusion = exclusion;
        spell->impactScript = impactScript;
        spell->castAnim = castAnim;
        spell->castSound = castSound.empty() ? nullptr : _audioClips.get(castSound);
        spell->conjTime = conjTime;
        spell->castTime = castTime;
        spell->itemTargeting = itemTargeting;
        spell->hostile = hostile;
        int rangeRow = getSpellRangeRow(spells->getString(row, "range"));
        if (ranges && rangeRow >= 0 && rangeRow < ranges->getRowCount()) {
            spell->range = ranges->getFloat(rangeRow, "primaryrange", 0.0f);
        }
        spell->darkRecommendation = spells->getInt(row, "dark_recom", 0);
        spell->lightRecommendation = spells->getInt(row, "light_recom", 0);
        spell->forcePoints = std::max(0, spells->getInt(row, "forcepoints", 0));
        std::string goodEvil = spells->getString(row, "goodevil");
        if (!goodEvil.empty()) {
            char tag = static_cast<char>(std::toupper(static_cast<unsigned char>(goodEvil[0])));
            spell->goodEvil = (tag == 'G' || tag == 'E') ? tag : 0;
        }
        spell->projModel = projModel.empty() ? nullptr : _models.get(projModel);
        _spells.insert(std::make_pair(type, std::move(spell)));
    }
}

std::shared_ptr<Spell> Spells::get(SpellType type) const {
    auto it = _spells.find(type);
    return it != _spells.end() ? it->second : nullptr;
}

bool Spells::isLevelUpCandidate(
    SpellType type,
    const CreatureAttributes &attributes,
    const CreatureClass &clazz,
    const std::set<SpellType> &chosen) const {
    auto maybeSpell = _spells.find(type);
    if (maybeSpell == _spells.end() ||
        !isPickerVisible(*maybeSpell->second, clazz) ||
        attributes.hasSpell(type) ||
        chosen.count(type) > 0) {
        return false;
    }

    int targetClassLevel = attributes.getClassLevel(clazz.type()) + 1;
    auto maybeRequirement = maybeSpell->second->getClassLevelRequirement(clazz.type());
    return maybeRequirement && targetClassLevel >= *maybeRequirement &&
           prerequisitesSatisfied(*maybeSpell->second, attributes, chosen);
}

std::vector<SpellType> Spells::getLevelUpCandidates(
    const CreatureAttributes &attributes,
    const CreatureClass &clazz,
    const std::set<SpellType> &chosen) const {
    std::vector<SpellType> result;
    for (const auto &[type, spell] : _spells) {
        if (isLevelUpCandidate(type, attributes, clazz, chosen)) {
            result.push_back(type);
        }
    }
    std::sort(result.begin(), result.end(), [](SpellType left, SpellType right) {
        return static_cast<int>(left) < static_cast<int>(right);
    });
    return result;
}

std::vector<SpellDisplayEntry> Spells::getLevelUpDisplayEntries(
    const CreatureAttributes &attributes,
    const CreatureClass &clazz,
    const std::set<SpellType> &chosen) const {
    std::set<SpellType> visible;
    for (const auto &[type, spell] : _spells) {
        if (isPickerVisible(*spell, clazz)) {
            visible.insert(type);
        }
    }

    std::unordered_map<SpellType, int> depths;
    for (auto type : visible) {
        std::set<SpellType> visited;
        getVisualDepth(type, _spells, visible, depths, visited);
    }

    std::unordered_map<SpellType, std::optional<SpellType>> parents;
    for (auto type : visible) {
        parents.emplace(type, getDisplayParent(*_spells.at(type), visible, depths));
    }

    int targetClassLevel = attributes.getClassLevel(clazz.type()) + 1;
    std::vector<SpellDisplayEntry> result;
    for (const auto &[type, spell] : _spells) {
        SpellDisplayEntry entry;
        entry.type = type;
        entry.name = spell->name;
        entry.description = spell->description;
        entry.icon = spell->icon;
        entry.prerequisites = spell->prerequisites;
        entry.sourceOrder = static_cast<int>(type);
        entry.known = attributes.hasSpell(type);
        entry.chosen = chosen.count(type) > 0;
        entry.visible = visible.count(type) > 0;
        if (!entry.visible) {
            result.push_back(std::move(entry));
            continue;
        }

        entry.displayParent = parents.at(type);
        entry.chainRoot = getChainRoot(type, parents);
        entry.visualDepth = depths.at(type);
        auto maybeRequirement = spell->getClassLevelRequirement(clazz.type());
        if (entry.known) {
            entry.availability = SpellAvailability::Known;
        } else if (entry.chosen) {
            entry.availability = SpellAvailability::Chosen;
        } else if (maybeRequirement && targetClassLevel < *maybeRequirement) {
            entry.availability = SpellAvailability::LockedClassLevel;
        } else if (!prerequisitesSatisfied(*spell, attributes, chosen)) {
            entry.availability = SpellAvailability::LockedMissingPrerequisite;
        } else {
            entry.availability = SpellAvailability::Selectable;
            entry.selectable = true;
        }
        result.push_back(std::move(entry));
    }

    std::sort(result.begin(), result.end(), [](const SpellDisplayEntry &left, const SpellDisplayEntry &right) {
        if (left.visible != right.visible) {
            return left.visible;
        }
        if (!left.visible) {
            return left.sourceOrder < right.sourceOrder;
        }
        if (left.chainRoot != right.chainRoot) {
            return static_cast<int>(left.chainRoot) < static_cast<int>(right.chainRoot);
        }
        if (left.visualDepth != right.visualDepth) {
            return left.visualDepth < right.visualDepth;
        }
        return left.sourceOrder < right.sourceOrder;
    });
    return result;
}

} // namespace game

} // namespace reone
