/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "reone/game/types.h"

namespace reone {

namespace game {

/**
 * Who a Story Mode guest plays when no companion of the party is free
 * (design note companion_standins). The host decides, for each guest, once
 * per module visit:
 *
 * 1. a real companion of the party nobody controls: the slot system
 *    (CoopSession::hostUpdateSlots) hands it over, as before;
 * 2. a copy of a roster companion who plausibly fits the area: never one
 *    present in the module as an NPC, already in the party, played by another
 *    guest, or whose story clashes with the place (the clash table);
 *    preferring a role the party lacks;
 * 3. else the generic character (coopShepard, John Shepard): the same man everywhere, in the
 *    outfit, gear and class of the place;
 * 4. else (no table entry: K2, or a module the tables do not know) a copy
 *    of the solo character, as stand-ins were before.
 *
 * The tables name template resrefs only; the templates are read from the
 * player's install when the stand-in is built.
 */

/** What a party member is good for (classes.2da row). */
enum class CoopPartyRole {
    Tank,   // soldier, Jedi Guardian, combat droid: takes the hits
    Caster, // Jedi Consular and Sentinel: the Force
    Scout   // scout, scoundrel, expert droid: security, computers, repair
};

const char *coopPartyRoleName(CoopPartyRole role);

CoopPartyRole coopClassRole(ClassType clazz);

enum class CoopCastKind {
    None,    // no decision yet
    Copy,    // a copy of a roster companion
    Generic, // a character of the place
    Clone    // a copy of the solo character (no table entry)
};

const char *coopCastKindName(CoopCastKind kind);

/** One K1 companion. */
struct CoopRosterEntry {
    int npc;               // party table row (AddAvailableNPCByTemplate)
    const char *templ;     // the companion's template
    const char *name;      // first name, as FirstName holds it
    const char *key;       // lower-case fragment of the tags/templates of its NPC versions in modules
    ClassType clazz;       // the template's class
    // Gear its template lacks that the companion carries in play (equipped
    // only into an empty slot): the slot and the item template.
    int kitSlot;
    const char *kitItem;
};

const std::vector<CoopRosterEntry> &coopRoster();
const CoopRosterEntry *coopRosterEntry(int npc);

/** The companions who fit a planet, in preference order, and its gear tier. */
struct CoopPlanet {
    const char *prefix; // module resref prefix, lower case
    const char *name;
    int tier;           // 0 Endar Spire and Taris .. 3 the end game
    std::vector<int> companions;
};

const std::vector<CoopPlanet> &coopPlanets();
const CoopPlanet *coopPlanetFor(const std::string &module);

/**
 * A companion whose story clashes with modules starting with prefix: always
 * (global empty), or while the global boolean has the given value.
 */
struct CoopClash {
    int npc;
    const char *prefix;
    const char *global;
    bool value;
    const char *reason;
};

const std::vector<CoopClash> &coopClashes();

/**
 * The character of the place, for when no companion fits: the same man in
 * every area, John Shepard (the owner's joke: "that guy again"; the name is
 * a nod, nothing else). His identity is this one record; each area row gives
 * only his class, outfit and gear, and his level follows the party's.
 * buildCoopShepard (coopstandin.h) makes him for any other use too.
 */
struct CoopShepard {
    const char *firstName;
    const char *lastName;
    int appearance;       // appearance.2da row (a human male player body: the outfit shows)
    int headRow;          // heads.2da row, -1 for the appearance's own head
    int portrait;         // portraits.2da row
    int soundSet;         // soundset.2da row (his barks)
    Gender gender;
};

const CoopShepard &coopShepard();

/** "John Shepard". */
std::string coopShepardName();

/** The template field a stand-in of him carries on the wire: he has no template file. */
constexpr const char *kCoopShepardTemplate = "coop_shepard";

struct CoopGeneric {
    const char *prefix;
    const char *label;   // what he is here
    ClassType clazz;     // the class he levels in here
    // His outfit and gear here (slot, item template).
    std::vector<std::pair<int, const char *>> kit;
};

const std::vector<CoopGeneric> &coopGenerics();
const CoopGeneric *coopGenericFor(const std::string &module);

/** What the host knows when it decides. */
struct CoopCastInput {
    std::string module;
    std::set<int> partyNpcs;    // companions in the party now
    std::set<int> takenNpcs;    // copies other guests play
    std::set<int> presentNpcs;  // companions present in the module as NPCs
    std::set<CoopPartyRole> partyRoles; // roles of the party and the other guests' characters
    std::function<bool(const std::string &)> globalBoolean;
};

struct CoopCastChoice {
    CoopCastKind kind {CoopCastKind::None};
    int npc {-1};
    std::string templ; // the companion's template, or kCoopShepardTemplate
    std::string name;  // the companion's name, or what the generic character is here
    const char *planet {nullptr};
    ClassType clazz {ClassType::Invalid};
    CoopPartyRole role {CoopPartyRole::Tank};
    int tier {0};
    std::string reason;
    std::vector<std::pair<std::string, std::string>> skipped; // companion, why not
};

/** Why a roster companion cannot be copied here, or empty when it can. */
std::string coopCopyExclusion(const CoopCastInput &input, int npc);

/** The decision for one guest (steps 2-4 above). Deterministic. */
CoopCastChoice chooseCoopCast(const CoopCastInput &input);

/**
 * Gear tiers: for a base item type, the item template at each tier. An item
 * of a listed template below the planet's tier is swapped for the tier's
 * one; items the table does not list are kept.
 */
struct CoopGearLine {
    int baseItem; // baseitems.2da row
    const char *label;
    std::vector<const char *> tiers;
};

const std::vector<CoopGearLine> &coopGearLines();

/** The template to wear instead of current at the tier, or empty to keep it. */
std::string coopTieredItem(int baseItem, const std::string &current, int tier);

} // namespace game

} // namespace reone
