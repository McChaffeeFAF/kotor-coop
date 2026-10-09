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
    Buff,         // beneficial, not a heal (Force armour, Valor, shields)
    AttackSingle, // hostile, one target (Stun, Choke, Throw Saber)
    AttackArea,   // hostile, an area (Force Wave, Storm, Insanity)
    Grenade,      // hostile item, thrown; hits everything in its blast
    AttackFeat    // a melee or ranged attack feat (Power Attack, Rapid Shot)
};

constexpr float kGrenadeRadius = 5.0f;     // blast radius assumed for a grenade
constexpr float kAreaPowerRadius = 5.0f;   // radius assumed for a hostile area power
constexpr float kAreaHealRadius = 10.0f;   // party heals reach allies this close to the caster
constexpr float kFriendlyFireMargin = 1.0f;
constexpr float kHealThreshold = 0.5f;      // heal a party member below half health
constexpr float kItemHealThreshold = 0.4f;  // use a medpac on oneself below this
constexpr float kWeakEnemyThreshold = 0.25f;
constexpr float kChaseLimit = 20.0f;        // enemies farther than this from the leader are not chased
constexpr float kMeleeReach = 2.5f;
constexpr float kBackOffDistance = 4.0f;
constexpr float kLineOfFireWidth = 1.0f;
constexpr float kSideStep = 2.0f;

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

    float healthRatio() const {
        return maxHitPoints > 0 ? static_cast<float>(hitPoints) / static_cast<float>(maxHitPoints) : 1.0f;
    }
    bool hasSpellEffect(int spell) const {
        return std::binary_search(activeSpells.begin(), activeSpells.end(), spell);
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

/** Classifies a spells.2da row; nullopt when the brain does not use it. */
std::optional<TalentRole> classifySpell(uint32_t category, bool hostile, bool fromItem);

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
