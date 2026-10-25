/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <map>
#include <vector>

#include "reone/game/d20/attributes.h"
#include "reone/game/types.h"

namespace reone {

namespace game {

class Creature;
class CreatureClass;
class Game;
class IFeats;
class ISpells;

/**
 * Automatic level-ups (design note companion_standins, "Auto-level").
 *
 * K1 has no scripted level-up for party members: the level-up screens'
 * Recommended buttons and the "Auto Level Up" gameplay option both take the
 * picks the class data orders. This is that routine, with no screen:
 *
 * - every fourth level one ability point, to classes.2da primaryabil (else
 *   the ability the class's default spread favours most);
 * - skill points max(1, (skillpointbase + INT modifier) / 2), spent in the
 *   order of skills.2da <skillstable>_reco, class skills at one point a rank
 *   and others at two, ranks capped at level + 3 (half for cross-class);
 * - as many feats as featgain.2da <featgain>_reg grants for the new class
 *   level, in the order of feat.2da <featstable>_recom, each a candidate
 *   given the ones before;
 * - as many powers as classpowergain.2da grants, in the order of spells.2da
 *   light_recom (dark_recom for a dark-side character);
 * - then the class level itself, the feats the class grants at it
 *   (feat.2da <featstable>_granted), the hit die and the force die.
 *
 * The picks are deterministic: the same attributes give the same level.
 */
struct AutoLevelPicks {
    ClassType clazz {ClassType::Invalid};
    int ability {-1};                // Ability raised by one, -1 for none
    std::map<SkillType, int> skills; // ranks added
    int skillPoints {0};             // points the level had
    int skillPointsLeft {0};         // points no recommended skill could take
    int featChoices {0};
    std::vector<FeatType> feats;
    int powerChoices {0};
    std::vector<SpellType> powers;
};

/** The ability a class raises every fourth level. */
Ability autoLevelAbility(const CreatureClass &clazz);

/** The next level's picks in the class, from attributes before the level. */
AutoLevelPicks autoLevelPicks(
    const IFeats &feats,
    const ISpells &spells,
    const CreatureClass &clazz,
    const CreatureAttributes &attributes,
    bool darkSide);

/** The attributes after the level the picks make (the class level added, its granted feats). */
CreatureAttributes applyAutoLevelPicks(
    CreatureClass &clazz,
    const CreatureAttributes &attributes,
    const AutoLevelPicks &picks);

/**
 * Levels a creature up in its effective class until its level reaches
 * targetLevel, as the level-up screen's finish does (hit die, force die).
 * Returns the levels added.
 */
int autoLevelCreature(Game &game, Creature &creature, int targetLevel);

/** User setting behind Options > Gameplay > "Auto Level Up" (CB_LEVELUP). */
constexpr const char *kAutoLevelUpSetting = "optgameplay.CB_LEVELUP";

/**
 * Single player with "Auto Level Up" on: every companion in the party with a
 * level pending takes it (and any further ones) by itself; the player
 * character still levels through the screen. Returns the levels added.
 */
int autoLevelPartyCompanions(Game &game);

} // namespace game

} // namespace reone
