/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/stealth.h"

#include <algorithm>
#include <cmath>

#include <boost/algorithm/string.hpp>

namespace reone {

namespace game {

namespace stealth {

bool isStealthUnitBaseItem(const std::string &label, int itemType) {
    if (!label.empty()) {
        return boost::iequals(label, kStealthUnitLabel);
    }
    return itemType == kStealthUnitItemType;
}

Refusal entryRefusal(const EntryInputs &inputs) {
    if (!inputs.alive) {
        return Refusal::Unavailable;
    }
    if (inputs.inCombat) {
        return Refusal::InCombat;
    }
    if (!inputs.stealthUnitEquipped || inputs.stealthRanks <= 0) {
        return Refusal::Requirements;
    }
    if (inputs.leaderWithFollowers && !inputs.soloMode) {
        return Refusal::NeedSolo;
    }
    return Refusal::None;
}

int refusalStrRef(Refusal refusal) {
    switch (refusal) {
    case Refusal::InCombat:
        return kStrRefInCombat;
    case Refusal::Requirements:
        return kStrRefRequirements;
    case Refusal::NeedSolo:
        return kStrRefNeedSolo;
    default:
        return -1;
    }
}

bool mayRun(bool stealthed, bool hasStealthRunFeat) {
    return !stealthed || hasStealthRunFeat;
}

int awarenessModifier(const AwarenessInputs &inputs) {
    int modifier = inputs.awarenessSkill;
    if (inputs.distance > 0.0f) {
        modifier -= static_cast<int>(std::floor(inputs.distance / kDistancePenaltyStep));
    }
    if (inputs.observerRunning) {
        modifier -= kObserverRunningPenalty;
    }
    if (inputs.targetBehind) {
        modifier -= kRearArcPenalty;
    }
    if (inputs.observerInCombat) {
        modifier -= kObserverInCombatPenalty;
    }
    return modifier;
}

bool isBehind(float observerFacing, float dx, float dy) {
    // Facing 0 looks along +Y; the engine's forward is (-sin f, cos f).
    float fx = -std::sin(observerFacing);
    float fy = std::cos(observerFacing);
    return fx * dx + fy * dy < 0.0f;
}

CheckResult check(int awarenessRoll, int awarenessModifierTotal, int stealthRoll, int stealthSkill) {
    CheckResult result;
    result.awarenessRoll = awarenessRoll;
    result.awarenessTotal = awarenessRoll + awarenessModifierTotal;
    result.stealthRoll = stealthRoll;
    result.stealthTotal = stealthRoll + stealthSkill;
    result.detected = result.awarenessTotal > result.stealthTotal;
    return result;
}

int poolAfterDetection(int current, int decrement, bool enabled) {
    if (!enabled) {
        return current;
    }
    return std::max(0, current - std::max(0, decrement));
}

} // namespace stealth

} // namespace game

} // namespace reone
