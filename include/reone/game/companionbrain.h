/*
 * Copyright (c) 2026 The reone project contributors
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

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

#include "types.h"

namespace reone {

namespace game {

class Creature;
class Game;

/**
 * "Smarter companions" (C13, opt-in; design note c13_companions).
 *
 * An engine-side decision for one combat round of an AI-controlled party
 * member whose end-of-round script is a generic one. The decision itself is
 * a pure function of a snapshot (CompanionView), so it can be tested without
 * a game; runCompanionBrainRound builds the snapshot and queues the actions.
 */
namespace companion {

/** What a talent is for, derived from its 2DA category and flags. */
enum class TalentRole {
    Heal,         // restores hit points (Force heals, medpacs, repair kits)
    Buff,         // beneficial, not a heal (Force armour, Valor, shields, stims)
    AttackSingle, // hostile, one target (Stun, Choke, Throw Saber, droid stun ray)
    AttackArea,   // hostile, an area (Force Wave, Storm, Insanity)
    Grenade,      // hostile item, thrown; hits everything in its blast
    AttackFeat,   // a melee or ranged attack feat (Power Attack, Rapid Shot)
    Cure          // removes poison (anti-toxin kit)
};

/** What a usable item is, from its spells.2da row label. */
enum class ItemKind {
    None,         // a Force power, a feat, or an item read by category only
    Medpac,       // heals organics (medpac, advanced medpac, life support kit)
    RepairKit,    // heals droids (repair, advanced repair, construction kits)
    PartyHeal,    // heals the whole party (recovery stim)
    Antidote,     // removes poison
    Stim,         // adrenal, hyper-adrenal and combat shots
    Shield,       // energy shields (organic and droid)
    Cloak,        // stealth field: never used by the brain
    DroidUtility, // droid stun ray, flamethrower, carbonite, disruptor, gravity generator
    Grenade,
    Plot,         // a PLOT_ row or a plot-flagged item: never used
    Launcher      // K2 wrist-launcher ammunition: not a thrown item
};

/** A grenade's payload, from its spells.2da row label (k_sup_grenade). */
enum class GrenadeKind {
    None,
    Frag,
    Stun,     // stuns organics, droid-stuns droids
    Thermal,  // heavy blaster damage and knockdown (rare)
    Poison,   // organics only
    Sonic,
    Adhesive, // entangles
    Cryoban,  // cold damage and paralysis (rare)
    Plasma,   // the "fire" grenade: fire damage and horror (rare)
    Ion,      // heavy against droids and shields
    Other
};

/** Effect bits of a hostile talent, from its 2DA category digits. */
constexpr uint32_t kEffectDamage = 0x1;  // kind digit bit 1
constexpr uint32_t kEffectDisable = 0x2; // kind digit bit 2: stun, paralysis, hold
constexpr uint32_t kEffectDebuff = 0x4;  // third digit bit 1: slow, affliction, plague
constexpr uint32_t kEffectDispel = 0x8;  // third digit bit 2: Breach, shield disruptors
constexpr uint32_t kEffectCure = 0x10;   // a heal that also removes poison (category bit 0x40)

constexpr float kGrenadeRadius = 4.0f;     // grenade blast (k_sup_grenade: a 4 m sphere for every type)
constexpr float kAreaPowerRadius = 5.0f;   // radius assumed for a hostile area power
constexpr float kAreaHealRadius = 10.0f;   // party heals reach allies this close to the caster
constexpr float kAllyItemRange = 10.0f;    // an ally this close can be given a medpac, kit or anti-toxin
constexpr float kFriendlyFireMargin = 1.5f;
constexpr float kHealThreshold = 0.5f;     // heal a party member below half health
constexpr float kItemHealThreshold = 0.4f; // use a medpac or kit on a party member below this
constexpr float kCriticalHealth = 0.25f;   // below this the strongest kit is used
constexpr float kWeakEnemyThreshold = 0.25f;
constexpr float kChaseLimit = 20.0f;       // enemies farther than this from the leader are not chased
constexpr float kMeleeReach = 2.5f;
constexpr float kBackOffDistance = 4.0f;
constexpr float kLineOfFireWidth = 1.0f;
constexpr float kSideStep = 2.0f;
constexpr float kMinGrenadeValue = 2.0f;   // a throw must be worth this much (sum of grenadeValue)

struct Talent {
    TalentRole role {TalentRole::AttackSingle};
    TalentType type {TalentType::Spell};
    int id {-1};
    uint32_t item {0};        // granting item's object id; 0 for a known power or feat
    int forceCost {0};
    float radius {0.0f};      // effect radius (area powers, grenades, party heals)
    bool onCaster {false};    // the area is centred on the caster
    bool selfOnly {false};    // personal: the caster is the only target
    bool rangedFeat {false};  // AttackFeat: for ranged weapons (else melee)
    uint32_t exclusion {0};   // 2DA exclusion bits: 0x01 droids only, 0x02 organics only
    int rank {0};             // maxcr; higher is stronger
    ItemKind itemKind {ItemKind::None};
    GrenadeKind grenade {GrenadeKind::None};
    uint32_t effects {0};     // kEffect* bits of a hostile talent
    bool rare {false};        // conserved for a fight that is going badly
    bool droidItem {false};   // a droid's equipped utility or shield: droids only
    std::string family;       // buffs of one family do not stack (a power line, a stim type)
    int count {0};            // items of this kind in the pack; 0 for powers and feats
    int value {0};            // item cost: of two kits, the dearer is the stronger
};

struct Actor {
    uint32_t id {0};
    glm::vec3 position {0.0f};
    int hitPoints {0};
    int maxHitPoints {0};
    bool droid {false};
    bool ranged {false};      // wields a ranged weapon
    bool caster {false};      // an enemy that uses Force powers
    bool healer {false};      // an enemy that heals
    uint32_t target {0};      // who it attacks now; 0 for nobody
    std::vector<int> activeSpells; // spell ids of the effects on it, sorted
    std::vector<std::string> activeFamilies; // Talent::family of the effects on it, sorted
    int level {1};            // total character level
    bool boss {false};        // a plot-flagged or never-dying enemy
    bool disabled {false};    // stunned, paralysed, held, entangled, horrified or asleep
    bool poisoned {false};
    bool shielded {false};    // an energy shield or Force shield is up
    bool forceResistant {false}; // a Force resistance effect is on it
    bool immuneStun {false};  // immune to stun, paralysis or mind-affecting effects
    bool immunePoison {false};

    float healthRatio() const {
        return maxHitPoints > 0 ? static_cast<float>(hitPoints) / static_cast<float>(maxHitPoints) : 1.0f;
    }
    bool hasSpellEffect(int spell) const {
        return std::binary_search(activeSpells.begin(), activeSpells.end(), spell);
    }
    bool hasFamily(const std::string &family) const {
        return !family.empty() && std::binary_search(activeFamilies.begin(), activeFamilies.end(), family);
    }
};

struct Hazard {
    glm::vec3 position {0.0f};
    float radius {0.0f};
};

struct CompanionView {
    Actor self;
    int forcePoints {0};
    bool stealthed {false};   // never break stealth
    bool hold {false};        // hold position: no moving, no chasing
    bool passive {false};     // the party AI says do not start fights
    float attackRange {2.0f};
    float preferredDistance {0.0f}; // baseitems prefattackdist of the weapon; 0 in melee
    bool mayReposition {true}; // not repositioned in the last round
    uint32_t leaderTarget {0};
    glm::vec3 leaderPosition {0.0f};
    std::vector<Actor> allies;  // live party members other than self, sorted by id
    std::vector<Actor> enemies; // live, known enemies, sorted by id
    std::vector<Talent> talents;
    std::vector<Hazard> hazards; // known mines and area-effect clouds
    int partyDown {0};           // party members dead
    std::vector<uint32_t> claimedItems; // items other party members are about to use, sorted
    std::vector<uint32_t> tended;       // party members another is already giving a kit or cure, sorted
};

enum class DecisionKind {
    None,          // nothing worth doing this round
    Attack,        // weapon attack on target
    TalentOnObject // use talent on target
};

struct Decision {
    DecisionKind kind {DecisionKind::None};
    uint32_t target {0};
    Talent talent;
    std::optional<glm::vec3> moveFirst; // step here before acting
    std::string reason;
};

using Random = std::function<int(int, int)>;

/** Classifies a spells.2da row by category alone; nullopt when the brain does not use it. */
std::optional<TalentRole> classifySpell(uint32_t category, bool hostile, bool fromItem);

/** Everything the brain reads from one spells.2da row. */
struct SpellTraits {
    std::optional<TalentRole> role; // nullopt: the brain does not use it
    ItemKind itemKind {ItemKind::None};
    GrenadeKind grenade {GrenadeKind::None};
    uint32_t effects {0};
    bool rare {false};
    bool droidItem {false};
    float radius {0.0f}; // a known blast radius; 0 where the brain assumes one
};

/**
 * Classifies a spells.2da row by category, hostility, label and item
 * targeting. Items are told apart by label (ITEM_ABILITY_*, DROID_ITEM_*,
 * PLOT_*), which reads the K1 table and the K2 rows merged after it alike.
 */
SpellTraits describeSpell(uint32_t category, bool hostile, bool fromItem, const std::string &label,
                          uint32_t itemTargeting, int rank);

/** True for a label of an item row (ITEM_ABILITY_*, DROID_ITEM_*, PLOT_*). */
bool isItemLabel(const std::string &label);

/**
 * The stacking family of a spells.2da row: "stim:" and the stim type (the
 * hyper version shares it), "shield" for every energy shield, else "#" and
 * the root of its prerequisite line (Force Armor and Force Shield share
 * Force Aura's).
 */
std::string spellFamily(const SpellTraits &traits, const std::string &label, int rootId);

/** A fight worth stims and buffs: 3+ enemies, a boss, a much stronger enemy, or more enemy health than party health. */
bool toughFight(const CompanionView &view);

/** A fight going badly: a party member down or nearly dead, the party below half health, or far outmatched. */
bool goingBadly(const CompanionView &view);

/** What one enemy in a grenade's blast is worth (0: the grenade does nothing to it). */
float grenadeValue(const Talent &grenade, const Actor &enemy);

/** Force points kept back for heals: the cost of the cheapest heal power known. */
int forceReserve(const CompanionView &view);

/** True when a blast of radius centred at point would catch a party member. */
bool catchesAllies(const CompanionView &view, const glm::vec3 &point, float radius, bool excludeSelf);

/** The enemy to fight, or 0: leader's target, weak enemies, casters and healers first. */
uint32_t chooseTarget(const CompanionView &view);

Decision decide(const CompanionView &view, const Random &random);

/** True when the round of this party member goes to the brain instead of its script. */
bool usesBrain(Game &game, const Creature &creature);

/** Builds the view, decides and queues the actions. */
void runCompanionBrainRound(Game &game, Creature &creature);

bool isGenericCombatScript(const std::string &script);

} // namespace companion

} // namespace game

} // namespace reone
