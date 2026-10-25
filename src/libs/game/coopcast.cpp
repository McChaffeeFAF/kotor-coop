/* Copyright (c) 2026 The reone project contributors */

// Who a Story Mode guest plays when no companion is free: the tables and the
// decision (design note companion_standins). Templates are named by resref;
// their contents come from the player's install.

#include "reone/game/coopcast.h"

#include <algorithm>

#include <boost/algorithm/string.hpp>

namespace reone {

namespace game {

namespace {

constexpr int kBastila = 0;
constexpr int kCanderous = 1;
constexpr int kCarth = 2;
constexpr int kHK47 = 3;
constexpr int kJolee = 4;
constexpr int kJuhani = 5;
constexpr int kMission = 6;
constexpr int kT3M4 = 7;
constexpr int kZaalbar = 8;

bool startsWith(const std::string &text, const char *prefix) {
    return text.rfind(prefix, 0) == 0;
}

// The entry whose prefix is the longest that starts the module name.
template <class T>
const T *longestPrefix(const std::vector<T> &entries, const std::string &module) {
    auto name = boost::to_lower_copy(module);
    const T *best = nullptr;
    size_t bestLength = 0;
    for (const auto &entry : entries) {
        size_t length = std::char_traits<char>::length(entry.prefix);
        if (length > bestLength && startsWith(name, entry.prefix)) {
            best = &entry;
            bestLength = length;
        }
    }
    return best;
}

} // namespace

const char *coopPartyRoleName(CoopPartyRole role) {
    switch (role) {
    case CoopPartyRole::Caster:
        return "caster";
    case CoopPartyRole::Scout:
        return "scout";
    default:
        return "tank";
    }
}

CoopPartyRole coopClassRole(ClassType clazz) {
    switch (clazz) {
    case ClassType::JediConsular:
    case ClassType::JediSentinel:
    case ClassType::JediMaster:
    case ClassType::JediWatchman:
    case ClassType::SithLord:
    case ClassType::SithAssassin:
        return CoopPartyRole::Caster;
    case ClassType::Scout:
    case ClassType::Scoundrel:
    case ClassType::ExpertDroid:
    case ClassType::TechSpecialist:
        return CoopPartyRole::Scout;
    default:
        return CoopPartyRole::Tank;
    }
}

const char *coopCastKindName(CoopCastKind kind) {
    switch (kind) {
    case CoopCastKind::Copy:
        return "copy";
    case CoopCastKind::Generic:
        return "generic";
    case CoopCastKind::Clone:
        return "clone";
    default:
        return "none";
    }
}

const std::vector<CoopRosterEntry> &coopRoster() {
    // K1's party table rows and companion templates (p_*.utc in the BIFs).
    // Bastila's and Zaalbar's templates carry no weapon (the story hands them
    // theirs): the kit gives a copy the one they fight with.
    static const std::vector<CoopRosterEntry> roster {
        {kBastila, "p_bastilla", "Bastila", "bastil", ClassType::JediSentinel, InventorySlots::rightWeapon, "g_w_dblsbr001"},
        {kCanderous, "p_cand", "Canderous", "cand", ClassType::Soldier, -1, nullptr},
        {kCarth, "p_carth", "Carth", "carth", ClassType::Soldier, -1, nullptr},
        {kHK47, "p_hk47", "HK-47", "hk47", ClassType::CombatDroid, -1, nullptr},
        {kJolee, "p_jolee", "Jolee", "jolee", ClassType::JediConsular, -1, nullptr},
        {kJuhani, "p_juhani", "Juhani", "juhani", ClassType::JediGuardian, -1, nullptr},
        {kMission, "p_mission", "Mission", "mission", ClassType::Scoundrel, -1, nullptr},
        {kT3M4, "p_t3m4", "T3-M4", "t3m4", ClassType::ExpertDroid, -1, nullptr},
        {kZaalbar, "p_zaalbar", "Zaalbar", "zaalbar", ClassType::Scout, InventorySlots::rightWeapon, "g_w_bowcstr003"},
    };
    return roster;
}

const CoopRosterEntry *coopRosterEntry(int npc) {
    for (const auto &entry : coopRoster()) {
        if (entry.npc == npc) {
            return &entry;
        }
    }
    return nullptr;
}

const std::vector<CoopPlanet> &coopPlanets() {
    // Who plausibly travels there, most fitting first. A companion the
    // player has already recruited is either in the party (excluded) or free
    // to take over for real; the copies stand in for the ones not met yet.
    static const std::vector<CoopPlanet> planets {
        // Nobody: Trask and Carth are the story's own on the Endar Spire.
        {"end_", "the Endar Spire", 0, {}},
        // Bastila is a captive until the swoop race, then recruited.
        {"tar_", "Taris", 0, {kMission, kZaalbar, kCanderous, kT3M4, kCarth}},
        {"ebo_m12", "the Ebon Hawk", 1, {kCarth, kMission, kZaalbar, kT3M4, kCanderous, kBastila}},
        {"ebo_", "the Ebon Hawk", 3, {kCarth, kMission, kZaalbar, kT3M4, kCanderous, kJuhani, kJolee, kHK47}},
        {"danm", "Dantooine", 1, {kBastila, kCarth, kJuhani, kMission, kZaalbar, kCanderous, kT3M4}},
        {"tat_", "Tatooine", 2, {kCarth, kMission, kCanderous, kT3M4, kBastila, kJuhani, kZaalbar}},
        {"kas_", "Kashyyyk", 2, {kZaalbar, kJolee, kCarth, kMission, kCanderous, kT3M4, kBastila, kJuhani, kHK47}},
        {"manm", "Manaan", 2, {kCarth, kBastila, kMission, kCanderous, kJuhani, kT3M4, kHK47, kZaalbar, kJolee}},
        {"korr_", "Korriban", 2, {kCanderous, kHK47, kJuhani, kBastila, kCarth, kMission, kT3M4, kJolee, kZaalbar}},
        {"liv_", "Yavin Station", 3, {kCarth, kCanderous, kMission, kZaalbar, kT3M4, kHK47, kJuhani, kJolee, kBastila}},
        // Bastila and Carth are held aboard the Leviathan.
        {"lev_", "the Leviathan", 3, {kCanderous, kJuhani, kJolee, kHK47, kT3M4, kMission, kZaalbar}},
        // Malak has taken Bastila.
        {"unk_", "the Unknown World", 3, {kCarth, kCanderous, kJuhani, kJolee, kHK47, kT3M4, kMission, kZaalbar}},
        {"sta_", "the Star Forge", 3, {kCarth, kCanderous, kJuhani, kJolee, kHK47, kT3M4, kMission, kZaalbar}},
    };
    return planets;
}

const CoopPlanet *coopPlanetFor(const std::string &module) {
    return longestPrefix(coopPlanets(), module);
}

const std::vector<CoopClash> &coopClashes() {
    // Global names from globalcat.2da.
    static const std::vector<CoopClash> clashes {
        {kCarth, "end_", "", false, "is in the Endar Spire's own scene"},
        {kBastila, "end_", "", false, "is fighting on the Endar Spire's bridge"},
        {kBastila, "tar_", "", false, "is a captive on Taris until her rescue"},
        {kBastila, "lev_", "", false, "is a prisoner on the Leviathan"},
        {kCarth, "lev_", "", false, "is a prisoner on the Leviathan"},
        {kBastila, "unk_", "", false, "has been taken by Malak"},
        {kBastila, "sta_", "", false, "has been taken by Malak"},
        {kHK47, "tat_", "", false, "is for sale in Anchorhead until bought"},
        {kJuhani, "danm", "DAN_JUHANI_DONE", false, "is still in the grove"},
        {kJolee, "kas_", "G_JoleeJoined", false, "is still in the Shadowlands"},
        {kZaalbar, "kas_", "kas_ZaalbarPris", true, "is a Czerka prisoner"},
    };
    return clashes;
}

const CoopShepard &coopShepard() {
    // The K1 player head closest to the default Shepard: PMHC02, short dark
    // hair and a square jaw (portraits.2da 24 po_pmhc2), on its own medium
    // male human player body (appearance.2da 170, which carries that head),
    // with the Republic soldier's sound set (soundset.2da 58). The name is a
    // nod, nothing more. Swap any of it here.
    static const CoopShepard shepard {"John", "Shepard", 170, -1, 24, 58, Gender::Male};
    return shepard;
}

std::string coopShepardName() {
    const auto &shepard = coopShepard();
    std::string name(shepard.firstName);
    if (*shepard.lastName) {
        name += std::string(name.empty() ? "" : " ") + shepard.lastName;
    }
    return name;
}

const std::vector<CoopGeneric> &coopGenerics() {
    // Item templates from the BIFs. The body item is his outfit: the player
    // body shows it.
    constexpr int body = InventorySlots::body;
    constexpr int right = InventorySlots::rightWeapon;
    static const std::vector<CoopGeneric> generics {
        {"end_", "Republic soldier", ClassType::Soldier, {{body, "g_a_class4001"}, {right, "g_w_blstrrfl001"}}},
        {"tar_", "Taris mercenary", ClassType::Scoundrel, {{body, "g_a_clothes06"}, {right, "g_w_blstrpstl001"}}},
        {"tar_m03", "swoop gang member", ClassType::Scout, {{body, "g_a_class4001"}, {right, "g_w_vbroswrd01"}}},
        {"tar_m04", "swoop gang member", ClassType::Scout, {{body, "g_a_class4001"}, {right, "g_w_vbroswrd01"}}},
        {"tar_m10", "swoop gang member", ClassType::Scout, {{body, "g_a_class4001"}, {right, "g_w_vbroswrd01"}}},
        {"tar_m11", "swoop gang member", ClassType::Scout, {{body, "g_a_class4001"}, {right, "g_w_vbroswrd01"}}},
        {"ebo_", "Ebon Hawk crewman", ClassType::Scoundrel, {{body, "g_a_clothes03"}, {right, "g_w_blstrpstl001"}}},
        {"danm", "Jedi apprentice", ClassType::JediGuardian, {{body, "g_a_jedirobe01"}, {right, "g_w_lghtsbr01"}}},
        {"tat_", "Anchorhead mercenary", ClassType::Scoundrel, {{body, "g_a_class4001"}, {right, "g_w_blstrpstl001"}}},
        {"kas_", "Czerka deserter", ClassType::Soldier, {{body, "g_a_class5001"}, {right, "g_w_blstrrfl001"}}},
        {"manm", "Republic soldier", ClassType::Soldier, {{body, "g_a_class5001"}, {right, "g_w_blstrrfl001"}}},
        {"korr_", "Sith student", ClassType::JediGuardian, {{body, "g_a_clothes05"}, {right, "g_w_lghtsbr02"}}},
        {"liv_", "station guard", ClassType::Soldier, {{body, "g_a_class5001"}, {right, "g_w_blstrrfl001"}}},
        {"lev_", "Republic prisoner", ClassType::Soldier, {{body, "g_a_clothes01"}, {right, "g_w_blstrpstl001"}}},
        {"unk_", "castaway", ClassType::Scout, {{body, "g_a_clothes02"}, {right, "g_w_qtrstaff01"}}},
        {"sta_", "Jedi", ClassType::JediGuardian, {{body, "g_a_kghtrobe01"}, {right, "g_w_lghtsbr01"}}},
    };
    return generics;
}

const CoopGeneric *coopGenericFor(const std::string &module) {
    return longestPrefix(coopGenerics(), module);
}

std::string coopCopyExclusion(const CoopCastInput &input, int npc) {
    if (input.partyNpcs.count(npc)) {
        return "is in the party";
    }
    if (input.takenNpcs.count(npc)) {
        return "another guest plays them";
    }
    if (input.presentNpcs.count(npc)) {
        return "is in this module";
    }
    auto module = boost::to_lower_copy(input.module);
    for (const auto &clash : coopClashes()) {
        if (clash.npc != npc || !startsWith(module, clash.prefix)) {
            continue;
        }
        if (!*clash.global) {
            return clash.reason;
        }
        bool value = input.globalBoolean ? input.globalBoolean(clash.global) : false;
        if (value == clash.value) {
            return clash.reason;
        }
    }
    return std::string();
}

CoopCastChoice chooseCoopCast(const CoopCastInput &input) {
    CoopCastChoice choice;
    const auto *planet = coopPlanetFor(input.module);
    choice.tier = planet ? planet->tier : 0;
    choice.planet = planet ? planet->name : nullptr;
    if (planet) {
        std::vector<const CoopRosterEntry *> fitting;
        for (int npc : planet->companions) {
            const auto *entry = coopRosterEntry(npc);
            if (!entry) {
                continue;
            }
            auto why = coopCopyExclusion(input, npc);
            if (why.empty()) {
                fitting.push_back(entry);
            } else {
                choice.skipped.push_back({entry->name, why});
            }
        }
        const CoopRosterEntry *picked = nullptr;
        for (const auto *entry : fitting) {
            if (!input.partyRoles.count(coopClassRole(entry->clazz))) {
                picked = entry;
                choice.reason = std::string("fits ") + planet->name + "; the party has no " + coopPartyRoleName(coopClassRole(entry->clazz));
                break;
            }
        }
        if (!picked && !fitting.empty()) {
            picked = fitting.front();
            choice.reason = std::string("fits ") + planet->name + " (every role is covered: the first who fits)";
        }
        if (picked) {
            choice.kind = CoopCastKind::Copy;
            choice.npc = picked->npc;
            choice.templ = picked->templ;
            choice.name = picked->name;
            choice.clazz = picked->clazz;
            choice.role = coopClassRole(picked->clazz);
            return choice;
        }
    }
    if (const auto *generic = coopGenericFor(input.module)) {
        choice.kind = CoopCastKind::Generic;
        choice.templ = kCoopShepardTemplate;
        choice.name = generic->label;
        choice.clazz = generic->clazz;
        choice.role = coopClassRole(generic->clazz);
        choice.reason = planet ? std::string("no companion fits ") + planet->name + ": " + coopShepardName() + ", a " + generic->label
                               : coopShepardName() + ", a " + generic->label;
        return choice;
    }
    choice.kind = CoopCastKind::Clone;
    choice.reason = "no stand-in table entry for " + (input.module.empty() ? std::string("this module") : boost::to_lower_copy(input.module));
    return choice;
}

const std::vector<CoopGearLine> &coopGearLines() {
    // Tier 0 Endar Spire and Taris, 1 Dantooine and the first Ebon Hawk
    // visit, 2 the mid-game planets, 3 the end game. Item templates from the
    // BIFs, one base item type per line (baseitems.2da row).
    static const std::vector<CoopGearLine> lines {
        {0, "quarterstaff", {"g_w_qtrstaff01", "g_w_qtrstaff01", "g_w_qtrstaff02", "g_w_qtrstaff02"}},
        {3, "vibrosword", {"g_w_vbroswrd01", "g_w_vbroswrd02", "g_w_vbroswrd03", "g_w_vbroswrd04"}},
        {5, "vibroblade", {"g_w_vbroshort01", "g_w_vbroshort02", "g_w_vbroshort03", "g_w_vbroshort03"}},
        {7, "vibro double-blade", {"g_w_vbrdblswd01", "g_w_vbrdblswd02", "g_w_vbrdblswd03", "g_w_vbrdblswd03"}},
        {12, "blaster pistol", {"g_w_blstrpstl001", "g_w_blstrpstl002", "g_w_blstrpstl003", "g_w_blstrpstl004"}},
        {13, "heavy blaster", {"g_w_hvyblstr01", "g_w_hvyblstr02", "g_w_hvyblstr03", "g_w_hvyblstr04"}},
        {19, "bowcaster", {"g_w_bowcstr003", "g_w_bowcstr003", "g_w_bowcstr003", "g_w_bowcstr002"}},
        {20, "blaster carbine", {"g_w_blstrcrbn001", "g_w_blstrcrbn001", "g_w_blstrcrbn002", "g_w_blstrcrbn003"}},
        {23, "repeating blaster", {"g_w_rptnblstr01", "g_w_rptnblstr01", "g_w_rptnblstr02", "g_w_rptnblstr02"}},
        {24, "heavy repeating blaster", {"g_w_hvrptbltr002", "g_w_hvrptbltr002", "g_w_hvrptbltr01", "g_w_hvrptbltr02"}},
        {38, "light armour", {"g_a_class4001", "g_a_class4002", "g_a_class4003", "g_a_class4004"}},
        {39, "medium-light armour", {"g_a_class5001", "g_a_class5002", "g_a_class5003", "g_a_class5004"}},
        {66, "droid light plating", {"g_i_drdltplat001", "g_i_drdltplat002", "g_i_drdltplat003", "g_i_drdltplat003"}},
        {77, "blaster rifle", {"g_w_blstrrfl001", "g_w_blstrrfl002", "g_w_blstrrfl003", "g_w_blstrrfl004"}},
    };
    return lines;
}

std::string coopTieredItem(int baseItem, const std::string &current, int tier) {
    auto ref = boost::to_lower_copy(current);
    for (const auto &line : coopGearLines()) {
        if (line.baseItem != baseItem || line.tiers.empty()) {
            continue;
        }
        int own = -1;
        for (size_t i = 0; i < line.tiers.size(); ++i) {
            if (ref == line.tiers[i]) {
                own = static_cast<int>(i);
                break;
            }
        }
        if (own < 0 || tier <= own) {
            return std::string(); // not ours to judge, or already as good
        }
        std::string wanted = line.tiers[std::min(static_cast<size_t>(tier), line.tiers.size() - 1)];
        return wanted == ref ? std::string() : wanted;
    }
    return std::string();
}

} // namespace game

} // namespace reone
