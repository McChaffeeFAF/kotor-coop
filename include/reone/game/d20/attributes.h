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

#include "../types.h"

#include "savingthrows.h"

namespace reone {

namespace game {

class CreatureClass;

/**
 * Aggregate for creature role-playing attributes: classes, abilities, skills,
 * feats and Force Powers.
 */
class CreatureAttributes {
public:
    int getDefense() const;

    // Class Levels

    void addClassLevels(CreatureClass *clazz, int levels);

    /**
     * Give the creature a new class at level 0 (AddMultiClass): appended when
     * it has fewer than two classes, otherwise replacing the second slot.
     * @return false for a null class or one the creature already has
     */
    bool addMultiClass(CreatureClass *clazz);

    /**
     * Give the creature a prestige class in a third class slot at level 0
     * (the K2 systems merge, design note k2_merge): base, Jedi, prestige.
     * @return false for a null class, one the creature already has, or when
     *         it already has three classes
     */
    bool addPrestigeClass(CreatureClass *clazz);

    /**
     * @return class type at the specified position (1-based)
     */
    ClassType getClassByPosition(int position) const;

    /**
     * @return class level at the specified position (1-based)
     */
    int getLevelByPosition(int position) const;

    /**
     * @return the sum of all class levels, aka "hit dice"
     */
    int getAggregateLevel() const;

    /**
     * @return the sum of (level * hitdie) of all classes
     */
    int getAggregateHitDie() const;

    /**
     * @return base force points from class levels: forcedie per level of
     *         every spellcaster class (ability modifiers not included)
     */
    int getAggregateForceDie() const;

    /** Derive permanent runtime vitality from a serialized base maximum. */
    int getPermanentMaxHitPoints(int baseHitPoints) const;

    ClassType getEffectiveClass() const;
    int getClassLevel(ClassType clazz) const;
    int getAggregateAttackBonus() const;
    int getAggregateDefenseBonus() const;
    SavingThrows getAggregateSavingThrows() const;
    /**
     * ModifyFortitudeSavingThrowBase and its siblings: a script's change to the
     * base saves, on top of what the classes grant. Saved games do not carry it.
     * @param save 0 fortitude, 1 reflex, 2 will
     */
    void modifyBaseSavingThrow(int save, int delta);
    const std::vector<std::pair<CreatureClass *, int>> &classLevels() const {
        return _classLevels;
    }

    // END Class Levels

    // Abilities

    int getAbilityScore(Ability ability) const;
    int getAbilityModifier(Ability ability) const;

    const std::map<Ability, int> &abilityScores() const { return _abilityScores; }
    int strength() const;
    int dexterity() const;
    int constitution() const;
    int intelligence() const;
    int wisdom() const;
    int charisma() const;

    void setAbilityScore(Ability ability, int score);

    // END Abilities

    // Skills

    bool hasSkill(SkillType skill) const;

    int getSkillRank(SkillType skill) const;

    const std::map<SkillType, int> &skillRanks() const { return _skillRanks; }
    int computerUse() const;
    int demolitions() const;
    int stealth() const;
    int awareness() const;
    int persuade() const;
    int repair() const;
    int security() const;
    int treatInjury() const;

    void setSkillRank(SkillType skill, int rank);

    // END Skills

    // Feats

    bool hasFeat(FeatType type) const { return _feats.count(type) > 0; }

    void addFeat(FeatType type) { _feats.insert(type); }
    void removeFeat(FeatType type) { _feats.erase(type); }
    const std::set<FeatType> &feats() const { return _feats; }

    // END Feats

    // Force Powers

    bool hasSpell(SpellType type) const { return _spells.count(type) > 0; }

    void addSpell(SpellType type) { _spells.insert(type); }
    void removeSpell(SpellType type) { _spells.erase(type); }
    const std::set<SpellType> &spells() const { return _spells; }

    // END Force Powers

private:
    std::vector<std::pair<CreatureClass *, int>> _classLevels;
    SavingThrows _savingThrowAdjustments;
    std::map<Ability, int> _abilityScores;
    std::map<SkillType, int> _skillRanks;
    std::set<FeatType> _feats;
    std::set<SpellType> _spells;
};

} // namespace game

} // namespace reone
