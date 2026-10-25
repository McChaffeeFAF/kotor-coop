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

#include "reone/game/d20/class.h"

#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/strings.h"

#include "reone/game/d20/classes.h"
#include "reone/game/twodautil.h"

using namespace reone::resource;

namespace reone {

namespace game {

static const char kSkillsTwoDAResRef[] = "skills";
static const char kDefenseBonusTwoDAResRef[] = "acbonus";
static const char kFeatTwoDAResRef[] = "feat";
static const char kFeatGainTwoDAResRef[] = "featgain";
static const char kPowerGainTwoDAResRef[] = "classpowergain";
static const char kSpellGainTwoDAResRef[] = "cls_spgn_jedi";

static bool hasColumn(const TwoDA &twoDa, const std::string &column) {
    return std::find(twoDa.columns().begin(), twoDa.columns().end(), column) != twoDa.columns().end();
}

static std::optional<int> getLevel(const TwoDA &twoDa, int row) {
    if (auto level = twoDa.getIntOpt(row, "label")) {
        return level;
    }
    return twoDa.getIntOpt(row, "level");
}

void CreatureClass::load(const TwoDA &twoDa, int row) {
    _name = _strings.getText(twoDa.getInt(row, "name"));
    _description = _strings.getText(twoDa.getInt(row, "description"));
    _hitdie = twoDa.getInt(row, "hitdie");
    _skillPointBase = twoDa.getInt(row, "skillpointbase");
    _forceDie = twoDa.getInt(row, "forcedie", 0);
    _spellCaster = twoDa.getInt(row, "spellcaster", 0) != 0;
    std::string primary(boost::to_upper_copy(twoDa.getString(row, "primaryabil")));
    static const std::pair<const char *, Ability> kAbilities[] {
        {"STR", Ability::Strength}, {"DEX", Ability::Dexterity}, {"CON", Ability::Constitution},
        {"INT", Ability::Intelligence}, {"WIS", Ability::Wisdom}, {"CHA", Ability::Charisma}};
    for (const auto &[label, ability] : kAbilities) {
        if (primary == label) {
            _primaryAbility = ability;
        }
    }

    _defaultAttributes.addClassLevels(this, 1);
    _defaultAttributes.setAbilityScore(Ability::Strength, twoDa.getInt(row, "str"));
    _defaultAttributes.setAbilityScore(Ability::Dexterity, twoDa.getInt(row, "dex"));
    _defaultAttributes.setAbilityScore(Ability::Constitution, twoDa.getInt(row, "con"));
    _defaultAttributes.setAbilityScore(Ability::Intelligence, twoDa.getInt(row, "int"));
    _defaultAttributes.setAbilityScore(Ability::Wisdom, twoDa.getInt(row, "wis"));
    _defaultAttributes.setAbilityScore(Ability::Charisma, twoDa.getInt(row, "cha"));

    std::string skillsTable(boost::to_lower_copy(twoDa.getString(row, "skillstable")));
    loadClassSkills(skillsTable);

    std::string savingThrowTable(boost::to_lower_copy(
        twoDa.getString(row, "savingthrowtable")));
    loadSavingThrows(savingThrowTable);

    std::string attackBonusTable(boost::to_lower_copy(
        twoDa.getString(row, "attackbonustable")));
    loadAttackBonuses(attackBonusTable);

    std::string defenseBonusColumn(boost::to_lower_copy(
        twoDa.getString(row, "armorclasscolumn")));
    if (!defenseBonusColumn.empty()) {
        loadDefenseBonuses(defenseBonusColumn);
    }

    std::string featsPrefix(boost::to_lower_copy(twoDa.getString(row, "featstable")));
    loadFeatListValues(featsPrefix);
    grantFeatsUpToLevel(_defaultAttributes, 1);

    std::string featGainPrefix(boost::to_lower_copy(twoDa.getString(row, "featgain")));
    loadFeatGains(featGainPrefix);

    std::string powerGainPrefix(boost::to_lower_copy(twoDa.getString(row, "spellgaintable")));
    loadPowerGains(powerGainPrefix);
}

void CreatureClass::loadClassSkills(const std::string &skillsTable) {
    std::shared_ptr<TwoDA> skills(_twoDas.get(kSkillsTwoDAResRef));
    std::vector<std::pair<int, SkillType>> recommended;
    for (int row = 0; row < skills->getRowCount(); ++row) {
        if (skills->getInt(row, skillsTable + "_class") == 1) {
            _classSkills.insert(static_cast<SkillType>(row));
        }
        // <table>_reco ranks the skills the Recommended button fills, 1 first.
        int priority = skills->getInt(row, skillsTable + "_reco", -1);
        if (priority > 0) {
            recommended.emplace_back(priority, static_cast<SkillType>(row));
        }
    }
    std::stable_sort(recommended.begin(), recommended.end(), [](const auto &left, const auto &right) {
        return left.first < right.first;
    });
    for (const auto &[priority, skill] : recommended) {
        _recommendedSkills.push_back(skill);
    }
}

void CreatureClass::loadSavingThrows(const std::string &savingThrowTable) {
    auto twoDa = getRequiredTwoDA(_twoDas, savingThrowTable);
    for (int row = 0; row < twoDa->getRowCount(); ++row) {
        int level = twoDa->getInt(row, "level");

        SavingThrows throws;
        throws.fortitude = twoDa->getInt(row, "fortsave");
        throws.reflex = twoDa->getInt(row, "refsave");
        throws.will = twoDa->getInt(row, "willsave");

        _savingThrowsByLevel.insert(std::make_pair(level, std::move(throws)));
    }
}

void CreatureClass::loadAttackBonuses(const std::string &attackBonusTable) {
    auto twoDa = getRequiredTwoDA(_twoDas, attackBonusTable);
    for (int row = 0; row < twoDa->getRowCount(); ++row) {
        _attackBonuses.push_back(twoDa->getInt(row, "bab"));
    }
}

void CreatureClass::loadDefenseBonuses(const std::string &defenseBonusColumn) {
    auto twoDa = getRequiredTwoDA(_twoDas, kDefenseBonusTwoDAResRef);
    for (int row = 0; row < twoDa->getRowCount(); ++row) {
        _defenseBonuses.push_back(twoDa->getInt(row, defenseBonusColumn));
    }
}

void CreatureClass::loadFeatListValues(const std::string &featsPrefix) {
    if (featsPrefix.empty()) {
        return;
    }

    std::shared_ptr<TwoDA> feats(_twoDas.get(kFeatTwoDAResRef));
    std::string listColumn(featsPrefix + "_list");
    if (!feats || !hasColumn(*feats, listColumn)) {
        return;
    }

    std::vector<std::pair<int, FeatType>> recommended;
    for (int row = 0; row < feats->getRowCount(); ++row) {
        auto feat = static_cast<FeatType>(row);
        int listValue = feats->getInt(row, listColumn, 4);
        _featListValues.insert(std::make_pair(feat, listValue));

        // <prefix>_granted: the class level at which the class hands the feat
        // out automatically (-1 or blank: never).
        int grantedLevel = feats->getInt(row, featsPrefix + "_granted", -1);
        if (grantedLevel > 0) {
            _grantedFeatsByLevel[grantedLevel].push_back(feat);
        }
        // <prefix>_recom: order in which the Recommended button picks feats.
        int priority = feats->getInt(row, featsPrefix + "_recom", -1);
        if (priority > 0) {
            recommended.emplace_back(priority, feat);
        }
    }
    std::stable_sort(recommended.begin(), recommended.end(), [](const auto &left, const auto &right) {
        return left.first < right.first;
    });
    for (const auto &[priority, feat] : recommended) {
        _recommendedFeats.push_back(feat);
    }
}

std::vector<FeatType> CreatureClass::getGrantedFeats(int classLevel) const {
    auto it = _grantedFeatsByLevel.find(classLevel);
    return it != _grantedFeatsByLevel.end() ? it->second : std::vector<FeatType>();
}

void CreatureClass::grantFeatsUpToLevel(CreatureAttributes &attributes, int classLevel) const {
    for (const auto &[level, feats] : _grantedFeatsByLevel) {
        if (level > classLevel) {
            break;
        }
        for (auto feat : feats) {
            attributes.addFeat(feat);
        }
    }
}

void CreatureClass::loadFeatGains(const std::string &featGainPrefix) {
    if (featGainPrefix.empty()) {
        return;
    }

    std::shared_ptr<TwoDA> featGain(_twoDas.get(kFeatGainTwoDAResRef));
    std::string regularGainColumn(featGainPrefix + "_reg");
    if (!featGain || !hasColumn(*featGain, regularGainColumn)) {
        return;
    }

    for (int row = 0; row < featGain->getRowCount(); ++row) {
        auto level = getLevel(*featGain, row);
        if (level) {
            _featGainsByLevel.insert(std::make_pair(*level, featGain->getInt(row, regularGainColumn, 0)));
        }
    }
}

void CreatureClass::loadPowerGains(const std::string &powerGainPrefix) {
    if (powerGainPrefix.empty()) {
        return;
    }

    std::shared_ptr<TwoDA> powerGain(_twoDas.get(kPowerGainTwoDAResRef));
    if (!powerGain || !hasColumn(*powerGain, powerGainPrefix)) {
        return;
    }

    // cls_spgn_jedi gates the gain (K2 behaviour, design note c11_data): a
    // level whose numspelllevels is 0 gains no power. Every K1 row holds 1.
    std::shared_ptr<TwoDA> spellGain(_twoDas.get(kSpellGainTwoDAResRef));
    for (int row = 0; row < powerGain->getRowCount(); ++row) {
        auto level = getLevel(*powerGain, row);
        if (level) {
            int gain = powerGain->getInt(row, powerGainPrefix, 0);
            if (spellGain && !powerGainAllowed(*spellGain, *level)) {
                gain = 0;
            }
            _powerGainsByLevel.insert(std::make_pair(*level, gain));
        }
    }
}

bool powerGainAllowed(const TwoDA &spellGain, int level) {
    for (int row = 0; row < spellGain.getRowCount(); ++row) {
        if (spellGain.getInt(row, "level", -1) == level) {
            return spellGain.getInt(row, "numspelllevels", 1) != 0;
        }
    }
    return true;
}

bool CreatureClass::isClassSkill(SkillType skill) const {
    return _classSkills.count(skill) > 0;
}

const SavingThrows &CreatureClass::getSavingThrows(int level) const {
    auto maybeThrows = _savingThrowsByLevel.find(level);
    if (maybeThrows == _savingThrowsByLevel.end()) {
        throw std::logic_error("Saving throws not found for level " + std::to_string(level));
    }
    return maybeThrows->second;
}

int CreatureClass::getAttackBonus(int level) const {
    if (level == 0) {
        return 0; // a freshly added multiclass has no levels yet
    }
    if (level < 1 || level > static_cast<int>(_attackBonuses.size())) {
        throw std::out_of_range(str(boost::format("Level out of range: %d/%d") % level % static_cast<int>(_attackBonuses.size())));
    }
    return _attackBonuses[level - 1];
}

int CreatureClass::getDefenseBonus(int level) const {
    assert(level >= 0);
    assert(!_defenseBonuses.empty());

    if (level >= static_cast<int>(_defenseBonuses.size())) {
        return _defenseBonuses.back();
    }
    return _defenseBonuses[level];
}

int CreatureClass::getFeatGain(int level) const {
    auto maybeFeatGain = _featGainsByLevel.find(level);
    return maybeFeatGain != _featGainsByLevel.end() ? maybeFeatGain->second : 0;
}

int CreatureClass::getPowerGain(int level) const {
    auto maybePowerGain = _powerGainsByLevel.find(level);
    return maybePowerGain != _powerGainsByLevel.end() ? maybePowerGain->second : 0;
}

int CreatureClass::getFeatListValue(FeatType feat) const {
    auto maybeFeatListValue = _featListValues.find(feat);
    return maybeFeatListValue != _featListValues.end() ? maybeFeatListValue->second : 4;
}

} // namespace game

} // namespace reone
