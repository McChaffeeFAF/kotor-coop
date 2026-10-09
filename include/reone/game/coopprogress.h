/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "reone/net/protocol.h"

namespace reone {

namespace game {

class CreatureAttributes;
class Creature;
class Game;

/**
 * A co-op client's level-up for its companion (co-op design notes item 5):
 * the choices of one level, as the client's level-up screens made them.
 * The host checks every choice against its own rules and applies the level
 * only when all of them hold.
 */
struct CoopLevelUpChoice {
    int clazz {-1};              // ClassType the level goes into
    int ability {-1};            // Ability raised by one (levels divisible by four), -1 for none
    std::map<int, int> skills;   // SkillType -> ranks added
    std::vector<int> feats;      // FeatType picks, in order
    std::vector<int> powers;     // SpellType picks, in order

    bool operator==(const CoopLevelUpChoice &o) const {
        return clazz == o.clazz && ability == o.ability && skills == o.skills && feats == o.feats && powers == o.powers;
    }
};

/** The choices as the command's text field: "c<class>|a<ability>|s<skill>:<ranks>,...|f<feat>,...|p<power>,...". */
std::string encodeCoopLevelUp(const CoopLevelUpChoice &choice);

/** False for anything that is not exactly that form (bounded counts, numbers only). */
bool decodeCoopLevelUp(const std::string &text, CoopLevelUpChoice &choice);

/** What a level-up screen changed: before is the member's sheet, after the screens' result. */
CoopLevelUpChoice coopLevelUpDiff(const CreatureAttributes &before, const CreatureAttributes &after, int clazz);

/**
 * Host: check a client's level-up for its companion. The companion must have
 * the XP for the next level, and that level may not pass the host's own
 * (a companion never out-levels the host). The class is the one the level
 * goes into, the ability point comes only every fourth level, skill ranks
 * are paid from the class's points within the rank caps, and every feat and
 * power is a level-up candidate by the game's own rules, in order.
 */
net::CommandReject validateCoopLevelUp(Game &game, const Creature &companion, int hostLevel,
                                       const CoopLevelUpChoice &choice, CreatureAttributes &result);

/** Host: apply a validated level, as the level-up screen's finish does. */
void applyCoopLevelUp(Game &game, Creature &companion, const CreatureAttributes &result);

/**
 * Host: the sheet of the creature a client controls: what its character,
 * equipment and level-up screens show. levelUpAllowed carries the host's cap.
 */
net::CharacterSheet coopCharacterSheet(const Creature &creature, bool levelUpAllowed);

/** Client: bring the local copy's attributes, XP and gear to the host's sheet. */
void applyCoopCharacterSheet(Game &game, Creature &creature, const net::CharacterSheet &sheet);

} // namespace game

} // namespace reone
