/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/autolevel.h"

#include <algorithm>

#include "reone/game/d20/class.h"
#include "reone/game/d20/classes.h"
#include "reone/game/d20/feats.h"
#include "reone/game/d20/k2feats.h"
#include "reone/game/d20/skills.h"
#include "reone/game/d20/spells.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

namespace {

constexpr Ability kAbilities[] {Ability::Strength, Ability::Dexterity, Ability::Constitution,
                                Ability::Intelligence, Ability::Wisdom, Ability::Charisma};

// The class's own skill list, or a Class Skill: <skill> feat (K2 merge) the character owns.
bool classSkill(const CreatureClass &clazz, const CreatureAttributes &attributes, SkillType skill) {
    if (clazz.isClassSkill(skill)) {
        return true;
    }
    auto feat = k2feats::classSkillFeat(skill);
    return feat != FeatType::Invalid && attributes.hasFeat(feat);
}

} // namespace

Ability autoLevelAbility(const CreatureClass &clazz) {
    if (auto primary = clazz.primaryAbility()) {
        return *primary;
    }
    // As the abilities screen's Recommended does: the ability the class's
    // default spread favours most (the first of equals).
    const auto &defaults = clazz.defaultAttributes();
    Ability best = Ability::Strength;
    for (Ability ability : kAbilities) {
        if (defaults.getAbilityScore(ability) > defaults.getAbilityScore(best)) {
            best = ability;
        }
    }
    return best;
}

AutoLevelPicks autoLevelPicks(
    const IFeats &feats,
    const ISpells &spells,
    const CreatureClass &clazz,
    const CreatureAttributes &attributes,
    bool darkSide) {

    AutoLevelPicks picks;
    picks.clazz = clazz.type();
    CreatureAttributes working(attributes);
    int newLevel = attributes.getAggregateLevel() + 1;

    // Abilities: one point on every fourth level.
    if (newLevel % 4 == 0) {
        auto ability = autoLevelAbility(clazz);
        picks.ability = static_cast<int>(ability);
        working.setAbilityScore(ability, working.getAbilityScore(ability) + 1);
    }

    // Skills: this level's points in the recommended order.
    int points = std::max(1, (clazz.skillPointBase() + working.getAbilityModifier(Ability::Intelligence)) / 2);
    picks.skillPoints = points;
    std::map<SkillType, int> ranks(working.skillRanks());
    std::map<SkillType, int> before(ranks);
    picks.skillPointsLeft = allocateRecommendedSkills(
        clazz.recommendedSkills(),
        ranks,
        points,
        [&](SkillType skill) { return classSkill(clazz, working, skill) ? 1 : 2; },
        [&](SkillType skill) { return classSkill(clazz, working, skill) ? newLevel + 3 : (newLevel + 3) / 2; });
    for (const auto &[skill, rank] : ranks) {
        int added = rank - (before.count(skill) ? before.at(skill) : 0);
        if (added > 0) {
            picks.skills[skill] = added;
            working.setSkillRank(skill, rank);
        }
    }

    // Feats: as many as the new class level grants.
    picks.featChoices = feats.getLevelUpChoiceCount(working, clazz);
    if (picks.featChoices > 0) {
        picks.feats = pickRecommendedFeats(feats, working, clazz, picks.featChoices);
        for (auto feat : picks.feats) {
            working.addFeat(feat);
        }
    }

    // Powers: as many as the class gains at its new level.
    picks.powerChoices = std::max(0, clazz.getPowerGain(working.getClassLevel(clazz.type()) + 1));
    if (picks.powerChoices > 0) {
        picks.powers = pickRecommendedPowers(spells, working, clazz, picks.powerChoices, darkSide);
    }
    return picks;
}

CreatureAttributes applyAutoLevelPicks(
    CreatureClass &clazz,
    const CreatureAttributes &attributes,
    const AutoLevelPicks &picks) {

    CreatureAttributes result(attributes);
    if (picks.ability >= 0) {
        auto ability = static_cast<Ability>(picks.ability);
        result.setAbilityScore(ability, result.getAbilityScore(ability) + 1);
    }
    for (const auto &[skill, added] : picks.skills) {
        result.setSkillRank(skill, result.getSkillRank(skill) + added);
    }
    for (auto feat : picks.feats) {
        result.addFeat(feat);
    }
    for (auto power : picks.powers) {
        result.addSpell(power);
    }
    // The level-up screen's finish: the class level, then what it grants.
    result.addClassLevels(&clazz, 1);
    clazz.grantFeatsUpToLevel(result, result.getClassLevel(clazz.type()));
    return result;
}

int autoLevelCreature(Game &game, Creature &creature, int targetLevel) {
    auto &services = game.services().game;
    int added = 0;
    // Never more than the level table has (20 in K1, 30 with K2's).
    targetLevel = std::min(targetLevel, 50);
    while (creature.attributes().getAggregateLevel() < targetLevel) {
        auto type = creature.attributes().getEffectiveClass();
        std::shared_ptr<CreatureClass> clazz;
        try {
            clazz = type != ClassType::Invalid ? services.classes.get(type) : nullptr;
        } catch (const std::exception &) {
            clazz.reset();
        }
        if (!clazz) {
            break;
        }
        auto picks = autoLevelPicks(services.feats, services.spells, *clazz, creature.attributes(), creature.goodEvil() < 50);
        creature.attributes() = applyAutoLevelPicks(*clazz, creature.attributes(), picks);
        creature.setMaxHitPoints(creature.hitPoints() + clazz->hitdie());
        creature.addBaseForcePoints(clazz->forcePointsPerLevel());
        ++added;
    }
    return added;
}

int autoLevelPartyCompanions(Game &game) {
    auto &party = game.party();
    auto player = party.player();
    int added = 0;
    for (const auto &member : party.members()) {
        const auto &creature = member.creature;
        if (!creature || creature == player || member.npc == kNpcPlayer || creature->isPC() || creature->isDead()) {
            continue;
        }
        int before = creature->attributes().getAggregateLevel();
        // One level at a time, while the experience allows another.
        while (creature->isLevelUpPending() &&
               autoLevelCreature(game, *creature, creature->attributes().getAggregateLevel() + 1) == 1) {
            creature->setCurrentHitPoints(std::min(creature->currentHitPoints(), creature->maxHitPoints()));
        }
        int now = creature->attributes().getAggregateLevel();
        if (now > before) {
            added += now - before;
            info("Auto Level Up: " + creature->tag() + " " + std::to_string(before) + " -> " + std::to_string(now));
        }
    }
    return added;
}

} // namespace game

} // namespace reone
