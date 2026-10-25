/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/vec2.hpp>

#include "reone/game/types.h"

namespace reone {

namespace game {

class Creature;
class Game;

/**
 * Co-op stand-ins (co-op design notes item 12, design note coop_standins).
 *
 * When the story leaves a player without a companion (a solo section, or
 * the story takes theirs away), the host gives that player a stand-in: a
 * copy of the character the party is down to. It is a follower outside the
 * party table, as K2's puppets are, so scripts that count or check the
 * party still see the solo character alone.
 */

/** What stand-ins do in a section. */
enum class CoopStandInRule {
    Play, // stand-ins spawn beside the solo character and fight
    Watch // no stand-ins: the player watches until a companion is free
};

struct CoopStandInSection {
    const char *module; // module resref, lower case
    CoopStandInRule rule;
    const char *reason;
};

/** The per-section rules table (sections not listed play). */
const std::vector<CoopStandInSection> &coopStandInSections();

/** The rule for a module; Play when the module is not listed. */
CoopStandInRule coopStandInRule(const std::string &module);

const char *coopStandInRuleName(CoopStandInRule rule);

/**
 * Template field of a stand-in: "@standin:<appearance>:<gender>:<source host id>",
 * and since protocol 12 ":<template>" after it when the stand-in was built from
 * a creature template (a companion copy, a character of the place).
 */
constexpr const char *kStandInPrefix = "@standin:";

std::string coopStandInResRef(int appearance, int gender, uint32_t sourceHostId, const std::string &templ = std::string());

/** False for anything that is not a well-formed stand-in template field. */
bool parseCoopStandInResRef(const std::string &resref, int &appearance, int &gender, uint32_t &sourceHostId,
                            std::string *templ = nullptr);

/** A creature template name as the template field carries it: 1-16 of a-z, 0-9 and _. */
bool isCoopTemplateName(const std::string &templ);

/** The name tag of a stand-in built from a template: "HK-47 (John)". */
std::string coopStandInLabel(const std::string &creatureName, const std::string &playerName);

/**
 * Where to try placing stand-in number index (0 for the first player's)
 * beside a solo character facing the given way: offsets in metres, best
 * first, behind and to the side so the stand-in never blocks the way ahead.
 */
std::vector<glm::vec2> coopStandInOffsets(float facing, int index);

/**
 * John Shepard (coopShepard in coopcast.h): a new creature with his identity
 * (name, appearance, head, portrait, sound set), level 1 in the class, then
 * levelled to level with the Recommended picks (autolevel.h), wearing the
 * outfit (slot, item template) given; items whose template is missing are
 * skipped. Not added to an area, no scripts, no conversation, faction
 * Player. Null when the class does not load.
 */
std::shared_ptr<Creature> buildCoopShepard(
    Game &game,
    ClassType clazz,
    int level,
    const std::vector<std::pair<int, const char *>> &outfit);

} // namespace game

} // namespace reone
