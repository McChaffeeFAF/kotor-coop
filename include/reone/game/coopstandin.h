/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec2.hpp>

namespace reone {

namespace game {

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

/** Template field of a stand-in: "@standin:<appearance>:<gender>:<source host id>". */
constexpr const char *kStandInPrefix = "@standin:";

std::string coopStandInResRef(int appearance, int gender, uint32_t sourceHostId);

/** False for anything that is not a well-formed stand-in template field. */
bool parseCoopStandInResRef(const std::string &resref, int &appearance, int &gender, uint32_t &sourceHostId);

/**
 * Where to try placing stand-in number index (0 for the first player's)
 * beside a solo character facing the given way: offsets in metres, best
 * first, behind and to the side so the stand-in never blocks the way ahead.
 */
std::vector<glm::vec2> coopStandInOffsets(float facing, int index);

} // namespace game

} // namespace reone
