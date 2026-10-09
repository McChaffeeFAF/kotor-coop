/* Copyright (c) 2026 The reone project contributors */

// Co-op progression (co-op design notes items 4 and 5): the character sheet a
// client needs for its companion's screens, and the client's level-up, which
// the host checks against its own rules before applying.

#include "reone/game/coopprogress.h"

#include <algorithm>
#include <set>

#include <boost/algorithm/string.hpp>

#include "reone/game/d20/attributes.h"
#include "reone/game/d20/class.h"
#include "reone/game/d20/classes.h"
#include "reone/game/d20/feats.h"
#include "reone/game/d20/forms.h"
#include "reone/game/d20/k2feats.h"
#include "reone/game/d20/prestige.h"
#include "reone/game/d20/spells.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/resource/k2merge.h"

using namespace reone::net;

namespace reone {

namespace game {

namespace {

constexpr size_t kMaxChoiceList = 12;
constexpr int kSkillCount = 8;
constexpr int kAbilityCount = 6;

bool parseInt(const std::string &text, int lo, int hi, int &out) {
    if (text.empty() || text.size() > 6) {
        return false;
    }
    size_t start = text[0] == '-' ? 1 : 0;
    if (start == text.size() ||
        !std::all_of(text.begin() + start, text.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return false;
    }
    int value = std::stoi(text);
    if (value < lo || value > hi) {
        return false;
    }
    out = value;
    return true;
}

bool parseList(const std::string &text, std::vector<int> &out) {
    out.clear();
    if (text.empty()) {
        return true;
    }
    std::vector<std::string> parts;
    boost::split(parts, text, boost::is_any_of(","));
    if (parts.size() > kMaxChoiceList) {
        return false;
    }
    for (const auto &part : parts) {
        int value = 0;
        if (!parseInt(part, 0, 65535, value)) {
            return false;
        }
        out.push_back(value);
    }
    return true;
}

std::string joinList(const std::vector<int> &values) {
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) {
            out += ",";
        }
        out += std::to_string(values[i]);
    }
    return out;
}

// The class's own skill list, or a Class Skill feat (K2 merge) the character
// owns: as the level-up skills screen decides.
bool isClassSkill(const CreatureClass &clazz, const CreatureAttributes &attributes, SkillType skill) {
    if (clazz.isClassSkill(skill)) {
        return true;
    }
    auto feat = k2feats::classSkillFeat(skill);
    return feat != FeatType::Invalid && attributes.hasFeat(feat);
}

// K2 Force forms due at the new Force-class level (K2 merge only), as the
// level-up screen grants them.
void grantDueForms(Game &game, CreatureAttributes &attributes, int goodEvil) {
    if (!game.services().resource.k2Merge) {
        return;
    }
    ClassType line = ClassType::Invalid;
    int forceLevel = 0;
    for (const auto &[clazz, level] : attributes.classLevels()) {
        if (clazz->forcePointsPerLevel() <= 0) {
            continue;
        }
        forceLevel += level;
        if (prestige::isPrestigeClass(clazz->type()) || line == ClassType::Invalid) {
            line = clazz->type();
        }
    }
    for (int form : forms::formsDue(line, forceLevel, goodEvil)) {
        attributes.addSpell(static_cast<SpellType>(form));
    }
}

} // namespace

std::string encodeCoopLevelUp(const CoopLevelUpChoice &choice) {
    std::string skills;
    for (const auto &[skill, ranks] : choice.skills) {
        if (!skills.empty()) {
            skills += ",";
        }
        skills += std::to_string(skill) + ":" + std::to_string(ranks);
    }
    return "c" + std::to_string(choice.clazz) + "|a" + std::to_string(choice.ability) + "|s" + skills + "|f" +
           joinList(choice.feats) + "|p" + joinList(choice.powers);
}

bool decodeCoopLevelUp(const std::string &text, CoopLevelUpChoice &choice) {
    choice = CoopLevelUpChoice();
    if (text.size() > kMaxStringBytes) {
        return false;
    }
    std::vector<std::string> parts;
    boost::split(parts, text, boost::is_any_of("|"));
    if (parts.size() != 5 || parts[0].rfind("c", 0) != 0 || parts[1].rfind("a", 0) != 0 || parts[2].rfind("s", 0) != 0 ||
        parts[3].rfind("f", 0) != 0 || parts[4].rfind("p", 0) != 0) {
        return false;
    }
    if (!parseInt(parts[0].substr(1), 0, 255, choice.clazz) || !parseInt(parts[1].substr(1), -1, kAbilityCount - 1, choice.ability)) {
        return false;
    }
    auto skills = parts[2].substr(1);
    if (!skills.empty()) {
        std::vector<std::string> entries;
        boost::split(entries, skills, boost::is_any_of(","));
        if (entries.size() > static_cast<size_t>(kSkillCount)) {
            return false;
        }
        for (const auto &entry : entries) {
            auto colon = entry.find(':');
            int skill = 0;
            int ranks = 0;
            if (colon == std::string::npos || !parseInt(entry.substr(0, colon), 0, kSkillCount - 1, skill) ||
                !parseInt(entry.substr(colon + 1), 1, 60, ranks) || choice.skills.count(skill)) {
                return false;
            }
            choice.skills[skill] = ranks;
        }
    }
    if (!parseList(parts[3].substr(1), choice.feats) || !parseList(parts[4].substr(1), choice.powers)) {
        return false;
    }
    return true;
}

CoopLevelUpChoice coopLevelUpDiff(const CreatureAttributes &before, const CreatureAttributes &after, int clazz) {
    CoopLevelUpChoice choice;
    choice.clazz = clazz;
    for (int ability = 0; ability < kAbilityCount; ++ability) {
        auto type = static_cast<Ability>(ability);
        if (after.getAbilityScore(type) > before.getAbilityScore(type)) {
            choice.ability = ability;
        }
    }
    for (int skill = 0; skill < kSkillCount; ++skill) {
        auto type = static_cast<SkillType>(skill);
        int added = after.getSkillRank(type) - before.getSkillRank(type);
        if (added > 0) {
            choice.skills[skill] = added;
        }
    }
    for (auto feat : after.feats()) {
        if (!before.hasFeat(feat)) {
            choice.feats.push_back(static_cast<int>(feat));
        }
    }
    for (auto spell : after.spells()) {
        if (!before.hasSpell(spell)) {
            choice.powers.push_back(static_cast<int>(spell));
        }
    }
    return choice;
}

CommandReject validateCoopLevelUp(Game &game, const Creature &companion, int hostLevel,
                                  const CoopLevelUpChoice &choice, CreatureAttributes &result) {
    const auto &before = companion.attributes();
    int level = before.getAggregateLevel();
    if (!companion.isLevelUpPending() || level + 1 > hostLevel) {
        return CommandReject::LevelUpUnavailable;
    }
    auto &services = game.services().game;
    if (choice.clazz < 0 || choice.clazz > static_cast<int>(ClassType::SithAssassin)) {
        return CommandReject::LevelUpInvalid;
    }
    auto classType = static_cast<ClassType>(choice.clazz);
    // The level goes into the class the level-up screen offers: the
    // character's current one, or a new one the host's rules allow: a second
    // base class, or a K2 prestige class once unlocked.
    bool newClass = classType != before.getEffectiveClass();
    bool known = false;
    for (const auto &[existing, existingLevel] : before.classLevels()) {
        known = known || existing->type() == classType;
    }
    if (newClass && known) {
        // Going back to an earlier slot is not something the screen offers.
        return CommandReject::LevelUpInvalid;
    }
    if (newClass) {
        switch (prestige::checkNewClass(game.prestigeFactsFor(companion, true), classType)) {
        case prestige::NewClassVerdict::Allowed:
            break;
        case prestige::NewClassVerdict::Locked:
            return CommandReject::ClassLocked;
        case prestige::NewClassVerdict::Alignment:
            return CommandReject::ClassAlignment;
        case prestige::NewClassVerdict::SlotsFull:
            return CommandReject::ClassSlotsFull;
        default:
            return CommandReject::LevelUpInvalid;
        }
    }
    std::shared_ptr<CreatureClass> clazz;
    try {
        clazz = services.classes.get(classType);
    } catch (const std::exception &) {
        clazz.reset();
    }
    if (!clazz) {
        return CommandReject::LevelUpInvalid;
    }
    result = before;
    if (newClass) {
        bool added = prestige::isPrestigeClass(classType)
                         ? result.addPrestigeClass(clazz.get())
                         : result.addMultiClass(clazz.get());
        if (!added) {
            return CommandReject::ClassSlotsFull;
        }
        if (prestige::isPrestigeClass(classType) && prestige::grantsGreyFeat(companion.goodEvil())) {
            if (auto *merge = game.services().resource.k2Merge) {
                int grey = merge->ownRowIndex("feat", prestige::kGreyFeatLabel);
                if (grey >= 0) {
                    result.addFeat(static_cast<FeatType>(grey));
                }
            }
        }
    }
    int newLevel = level + 1;
    // Abilities: one point on every fourth level, never otherwise.
    if (choice.ability >= 0) {
        if (newLevel % 4 != 0) {
            return CommandReject::LevelUpInvalid;
        }
        auto ability = static_cast<Ability>(choice.ability);
        result.setAbilityScore(ability, result.getAbilityScore(ability) + 1);
    }
    // Skills: paid from this level's points; class skills cost one, others
    // two; ranks capped at level + 3 (half for cross-class).
    int points = std::max(1, (clazz->skillPointBase() + result.getAbilityModifier(Ability::Intelligence)) / 2);
    for (const auto &[skill, ranks] : choice.skills) {
        auto type = static_cast<SkillType>(skill);
        bool classSkill = isClassSkill(*clazz, result, type);
        int cost = (classSkill ? 1 : 2) * ranks;
        int cap = classSkill ? newLevel + 3 : (newLevel + 3) / 2;
        int rank = result.getSkillRank(type) + ranks;
        if (cost > points || rank > cap) {
            return CommandReject::LevelUpInvalid;
        }
        points -= cost;
        result.setSkillRank(type, rank);
    }
    // Feats: as many as the level grants, each a candidate given the ones before.
    int featChoices = services.feats.getLevelUpChoiceCount(result, *clazz);
    // The Grey feat comes with the prestige class (added above), not as a pick.
    int greyFeat = -1;
    if (auto *merge = game.services().resource.k2Merge; merge && newClass && prestige::isPrestigeClass(classType)) {
        greyFeat = merge->ownRowIndex("feat", prestige::kGreyFeatLabel);
    }
    std::vector<int> picks;
    for (int feat : choice.feats) {
        if (feat != greyFeat) {
            picks.push_back(feat);
        }
    }
    if (static_cast<int>(picks.size()) > featChoices) {
        return CommandReject::LevelUpInvalid;
    }
    for (int feat : picks) {
        auto type = static_cast<FeatType>(feat);
        if (result.hasFeat(type) || !services.feats.isLevelUpCandidate(type, result, *clazz)) {
            return CommandReject::LevelUpInvalid;
        }
        result.addFeat(type);
    }
    // Powers: as many as the class gains at its new level.
    int powerGain = std::max(0, clazz->getPowerGain(result.getClassLevel(classType) + 1));
    if (static_cast<int>(choice.powers.size()) > powerGain) {
        return CommandReject::LevelUpInvalid;
    }
    std::set<SpellType> chosen;
    for (int power : choice.powers) {
        auto type = static_cast<SpellType>(power);
        if (result.hasSpell(type) || chosen.count(type) || !services.spells.isLevelUpCandidate(type, result, *clazz, chosen)) {
            return CommandReject::LevelUpInvalid;
        }
        chosen.insert(type);
    }
    for (auto type : chosen) {
        result.addSpell(type);
    }
    return CommandReject::None;
}

void applyCoopLevelUp(Game &game, Creature &companion, const CreatureAttributes &result) {
    // The level-up screen's finish, on the host's creature.
    CreatureAttributes attributes(result);
    ClassType classType = attributes.getEffectiveClass();
    auto clazz = game.services().game.classes.get(classType);
    if (!clazz) {
        return;
    }
    attributes.addClassLevels(clazz.get(), 1);
    clazz->grantFeatsUpToLevel(attributes, attributes.getClassLevel(classType));
    grantDueForms(game, attributes, companion.goodEvil());
    companion.attributes() = attributes;
    companion.setMaxHitPoints(companion.hitPoints() + clazz->hitdie());
    companion.addBaseForcePoints(clazz->forcePointsPerLevel());
}

CharacterSheet coopCharacterSheet(const Creature &creature, bool levelUpAllowed) {
    CharacterSheet sheet;
    sheet.creature = creature.id();
    const auto &attributes = creature.attributes();
    sheet.xp = static_cast<int32_t>(creature.xp());
    sheet.levelUp = levelUpAllowed && creature.isLevelUpPending();
    for (const auto &[clazz, level] : attributes.classLevels()) {
        if (clazz && sheet.classes.size() < 3) {
            sheet.classes.push_back({static_cast<int16_t>(clazz->type()), static_cast<int16_t>(level)});
        }
    }
    for (int ability = 0; ability < kAbilityCount; ++ability) {
        sheet.abilities[ability] = static_cast<uint8_t>(std::clamp(attributes.getAbilityScore(static_cast<Ability>(ability)), 0, 255));
    }
    for (int skill = 0; skill < kSkillCount; ++skill) {
        sheet.skills[skill] = static_cast<uint8_t>(std::clamp(attributes.getSkillRank(static_cast<SkillType>(skill)), 0, 255));
    }
    for (auto feat : attributes.feats()) {
        if (sheet.feats.size() < kMaxSheetEntries && static_cast<int>(feat) >= 0) {
            sheet.feats.push_back(static_cast<uint16_t>(feat));
        }
    }
    for (auto spell : attributes.spells()) {
        if (sheet.spells.size() < kMaxSheetEntries && static_cast<int>(spell) >= 0) {
            sheet.spells.push_back(static_cast<uint16_t>(spell));
        }
    }
    sheet.baseHp = static_cast<int16_t>(std::clamp(creature.hitPoints(), 0, 32767));
    sheet.baseFp = static_cast<int16_t>(std::clamp(creature.forcePoints(), 0, 32767));
    for (const auto &[slot, item] : creature.equipment()) {
        if (!item || slot < 0 || slot > 255 || sheet.equipment.size() >= kMaxSheetEquipment) {
            continue;
        }
        auto resref = boost::to_lower_copy(item->blueprintResRef());
        if (resref.empty() || resref.size() > 16) {
            continue;
        }
        sheet.equipment.push_back({static_cast<uint8_t>(slot), item->id(), resref});
    }
    return sheet;
}

void applyCoopCharacterSheet(Game &game, Creature &creature, const CharacterSheet &sheet) {
    auto &services = game.services().game;
    CreatureAttributes attributes;
    for (const auto &entry : sheet.classes) {
        if (auto clazz = services.classes.get(static_cast<ClassType>(entry.clazz))) {
            attributes.addClassLevels(clazz.get(), entry.level);
        }
    }
    for (int ability = 0; ability < kAbilityCount; ++ability) {
        attributes.setAbilityScore(static_cast<Ability>(ability), sheet.abilities[ability]);
    }
    for (int skill = 0; skill < kSkillCount; ++skill) {
        attributes.setSkillRank(static_cast<SkillType>(skill), sheet.skills[skill]);
    }
    for (auto feat : sheet.feats) {
        attributes.addFeat(static_cast<FeatType>(feat));
    }
    for (auto spell : sheet.spells) {
        attributes.addSpell(static_cast<SpellType>(spell));
    }
    creature.attributes() = attributes;
    creature.setXP(sheet.xp);
    // Every equipment slot, by the client's own copy of the host's item.
    std::set<int> slots;
    for (const auto &entry : sheet.equipment) {
        slots.insert(entry.slot);
        auto have = creature.getEquippedItem(entry.slot);
        if (have && boost::iequals(have->blueprintResRef(), entry.resref)) {
            continue;
        }
        if (have) {
            if (auto taken = creature.takeEquippedItem(have)) {
                game.destroyRuntimeObjectGraph(taken);
            }
        }
        auto item = game.newItemFromBlueprint(entry.resref);
        if (item && !creature.equip(entry.slot, item)) {
            game.destroyRuntimeObjectGraph(item);
        }
    }
    std::vector<std::shared_ptr<Item>> stale;
    for (const auto &[slot, item] : creature.equipment()) {
        if (item && !slots.count(slot)) {
            stale.push_back(item);
        }
    }
    for (auto &item : stale) {
        if (auto taken = creature.takeEquippedItem(item)) {
            game.destroyRuntimeObjectGraph(taken);
        }
    }
}

} // namespace game

} // namespace reone
