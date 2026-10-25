/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <cstdint>
#include <string>

namespace reone {

namespace game {

/**
 * Stealth Mode rules (design note stealth). Pure functions, so every rule
 * is unit-tested apart from the game. The numbers the game data states are
 * marked with their source; the rest are engine choices the spec lists.
 */
namespace stealth {

// dialog.tlk lines both games ship (K1 and K2 share these row numbers).
constexpr int kStrRefInCombat = 1452;          // "You cannot enter stealth mode while in combat."
constexpr int kStrRefActivated = 1464;         // "Stealth Mode Activated (Run Disabled)"
constexpr int kStrRefDeactivated = 1465;       // "Stealth Mode Deactivated (Run Enabled)"
constexpr int kStrRefActive = 37253;           // "Stealth mode active." (K2 Stealth Run: running stays on)
constexpr int kStrRefNeedSolo = 37890;         // "To use Stealth, you must turn Solo Mode on. ..."
constexpr int kStrRefSoloOffEndsStealth = 37892; // "Turning off Solo Mode will take you out of Stealth Mode. ..."
constexpr int kStrRefDetected = 38023;         // "<CUSTOM0> successfully detects <CUSTOM1>: awareness <CUSTOM2> vs. stealth <CUSTOM3>"
constexpr int kStrRefRequirements = 38062;     // "Stealth Mode requires points in the Stealth skill and an equipped stealth field generator."

/// The visual a stealthed creature wears: visualeffects.2da VFX_DUR_STEALTH_FIELD (progfx 1426).
constexpr int kStealthFieldVisual = 8002;

/// baseitems.2da: the stealth units' row label and item type (row 88 in both games).
constexpr const char *kStealthUnitLabel = "Stealth_Unit";
constexpr int kStealthUnitItemType = 44;

/// An item is a stealth unit by its base item row: the label, or the item type when the label is gone.
bool isStealthUnitBaseItem(const std::string &label, int itemType);

/// Why a creature may not enter Stealth Mode now.
enum class Refusal {
    None,
    Unavailable,  // dead, or not a creature that can act
    InCombat,     // TLK 1452
    Requirements, // TLK 38062: no stealth unit equipped, or no paid points in Stealth
    NeedSolo      // TLK 37890: the leader with followers, solo mode off
};

struct EntryInputs {
    bool alive {true};
    bool inCombat {false};
    bool stealthUnitEquipped {false};
    int stealthRanks {0}; // paid points (base skill rank, without ability or item bonuses)
    bool leaderWithFollowers {false}; // the host's leader, with AI party members who would follow
    bool soloMode {false};
};

/**
 * Entry check, in the order the player meets the reasons: combat first,
 * then the unit and the skill (both are named by one TLK line), then solo
 * mode, which the UI offers to turn on.
 */
Refusal entryRefusal(const EntryInputs &inputs);

/// TLK row that explains a refusal, -1 for none.
int refusalStrRef(Refusal refusal);

/// Running while stealthed: only with K2's Stealth Run feat (FEAT_STEALTH_RUN 197).
bool mayRun(bool stealthed, bool hasStealthRunFeat);

// Detection: an opposed check, observer d20 + Awareness against target d20 + Stealth.

/// An observer re-checks a stealthed creature it has not detected this often (seconds).
constexpr float kRecheckInterval = 20.0f;
/// The stealthed creature's own roll stands for this long against every observer.
constexpr float kStealthRollLifetime = 20.0f;

/// TLK 250 (Awareness): "Running imparts a -5 penalty to any checks of the skill."
constexpr int kObserverRunningPenalty = 5;
/// d20 Spot/Listen distance rule: -1 per 3 m (10 ft).
constexpr float kDistancePenaltyStep = 3.0f;
/// The target is in the observer's rear half-circle.
constexpr int kRearArcPenalty = 5;
/// The observer is fighting.
constexpr int kObserverInCombatPenalty = 5;

struct AwarenessInputs {
    int awarenessSkill {0}; // rank with ability and item bonuses (GetSkillRank)
    float distance {0.0f};  // metres between the two
    bool observerRunning {false};
    bool targetBehind {false};
    bool observerInCombat {false};
};

/// Total modifier the observer adds to its d20 (skill plus the situation).
int awarenessModifier(const AwarenessInputs &inputs);

/// The target is behind the observer: more than 90 degrees off its facing.
bool isBehind(float observerFacing, float dx, float dy);

struct CheckResult {
    int awarenessRoll {0};  // the d20
    int awarenessTotal {0}; // roll + modifier
    int stealthRoll {0};
    int stealthTotal {0};
    bool detected {false};
};

/**
 * One check. Detected only when awareness beats stealth: a tie leaves the
 * creature hidden (engine choice; the TLK names no tie rule).
 */
CheckResult check(int awarenessRoll, int awarenessModifierTotal, int stealthRoll, int stealthSkill);

/**
 * The area's stealth XP after a detection: lowered by the decrement
 * (ARE StealthXPLoss), never below 0. Only while stealth XP is enabled.
 */
int poolAfterDetection(int current, int decrement, bool enabled);

} // namespace stealth

} // namespace game

} // namespace reone
