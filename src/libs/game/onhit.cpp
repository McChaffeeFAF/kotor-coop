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

#include "reone/game/onhit.h"

#include <boost/algorithm/string/case_conv.hpp>

#include "reone/game/di/services.h"
#include "reone/game/effect/abilitydecrease.h"
#include "reone/game/effect/confused.h"
#include "reone/game/effect/death.h"
#include "reone/game/effect/frightened.h"
#include "reone/game/effect/movementspeeddecrease.h"
#include "reone/game/effect/paralyze.h"
#include "reone/game/effect/poison.h"
#include "reone/game/effect/sleep.h"
#include "reone/game/effect/stunned.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include "reone/system/randomutil.h"

namespace reone {

namespace game {

static constexpr int kFortitude = 1;
static constexpr int kReflex = 2;
static constexpr int kWill = 3;
static constexpr int kOnHitDCCostTable = 25;
static constexpr int kDefaultOnHitDC = 20;
static constexpr int kOnHitDurationParamTable = 1;
static constexpr float kRoundSeconds = 3.0f;
static constexpr float kAbilityDrainSeconds = 30.0f;
/** Slow: the movement rate is halved (reone choice; the K2 effect's scale is not known). */
static constexpr int kSlowPercent = 50;

std::optional<OnHitRule> onHitRule(int subtype) {
    switch (static_cast<OnHitKind>(subtype)) {
    case OnHitKind::Sleep:
    case OnHitKind::Stun:
    case OnHitKind::Confusion:
        return OnHitRule {kWill, SavingThrowType::MindAffecting, true, 0};
    case OnHitKind::Paralyze:
        return OnHitRule {kFortitude, SavingThrowType::Paralysis, true, 0};
    case OnHitKind::Fear:
        return OnHitRule {kWill, SavingThrowType::Fear, true, 0};
    case OnHitKind::Slow:
        return OnHitRule {kWill, SavingThrowType::None, true, 0};
    case OnHitKind::AbilityDrain:
        return OnHitRule {kReflex, SavingThrowType::None, false, 100};
    case OnHitKind::ItemPoison:
        // The poison makes its own save.
        return OnHitRule {0, SavingThrowType::None, false, 100};
    case OnHitKind::SlayRacialGroup:
    case OnHitKind::SlayAlignmentGroup:
        // Their chance is never set, so they never fire.
        return OnHitRule {kFortitude, SavingThrowType::Death, false, 0};
    case OnHitKind::InstantDeath:
        return OnHitRule {0, SavingThrowType::None, false, 100};
    default:
        return std::nullopt;
    }
}

OnHitRoll resolveOnHitRoll(resource::ITwoDAs &twoDas, int subtype, int param1Value, int costValue, const OnHitRule &rule) {
    OnHitRoll roll;
    roll.chance = rule.chance;
    if (rule.timed) {
        auto onHit = twoDas.get("iprp_onhit");
        auto durations = twoDas.get("iprp_onhitdur");
        if (onHit && durations && subtype < onHit->getRowCount() &&
            onHit->getInt(subtype, "param1resref", -1) == kOnHitDurationParamTable &&
            param1Value < durations->getRowCount()) {
            roll.chance = durations->getInt(param1Value, "effectchance", 0);
            roll.durationRounds = durations->getInt(param1Value, "durationrounds", 0);
        }
    }
    std::string dcTableName = "iprp_onhitdc";
    if (auto costTables = twoDas.get("iprp_costtable")) {
        if (kOnHitDCCostTable < costTables->getRowCount()) {
            dcTableName = boost::to_lower_copy(costTables->getString(kOnHitDCCostTable, "name", dcTableName));
        }
    }
    auto dcTable = twoDas.get(dcTableName);
    if (dcTable && costValue < dcTable->getRowCount()) {
        roll.saveDC = dcTable->getInt(costValue, "value", kDefaultOnHitDC);
    }
    return roll;
}

void applyOnHitProperties(Game &game, ServicesView &services, Creature &attacker, const Item &weapon, Creature &target) {
    auto &twoDas = services.resource.twoDas;
    for (const auto &property : weapon.properties()) {
        if (property.propertyName != static_cast<uint16_t>(ItemProperty::OnHitProperties) ||
            !weapon.isPropertyActive(property)) {
            continue;
        }
        auto rule = onHitRule(property.subtype);
        if (!rule) {
            continue;
        }
        OnHitRoll roll = resolveOnHitRoll(twoDas, property.subtype, property.paramValue, property.costValue, *rule);
        if (rule->timed && (roll.chance <= 0 || roll.durationRounds <= 0)) {
            continue;
        }
        if (roll.chance < 100 && randomInt(0, 99) >= roll.chance) {
            continue;
        }
        if (rule->save != 0 && target.rollSavingThrow(rule->save, roll.saveDC, rule->saveType)) {
            continue;
        }
        float duration = static_cast<float>(roll.durationRounds) * kRoundSeconds;
        switch (static_cast<OnHitKind>(property.subtype)) {
        case OnHitKind::Sleep:
            target.applyEffect(game.newEffect<SleepEffect>(), DurationType::Temporary, duration);
            break;
        case OnHitKind::Stun:
            target.applyEffect(game.newEffect<StunnedEffect>(), DurationType::Temporary, duration);
            break;
        case OnHitKind::Paralyze:
            target.applyEffect(game.newEffect<ParalyzeEffect>(), DurationType::Temporary, duration);
            break;
        case OnHitKind::Confusion:
            target.applyEffect(game.newEffect<ConfusedEffect>(), DurationType::Temporary, duration);
            break;
        case OnHitKind::Fear:
            target.applyEffect(game.newEffect<FrightenedEffect>(), DurationType::Temporary, duration);
            break;
        case OnHitKind::Slow:
            target.applyEffect(game.newEffect<MovementSpeedDecreaseEffect>(kSlowPercent), DurationType::Temporary, duration);
            break;
        case OnHitKind::AbilityDrain:
            target.applyEffect(
                game.newEffect<AbilityDecreaseEffect>(static_cast<Ability>(property.paramValue), 1),
                DurationType::Temporary, kAbilityDrainSeconds);
            break;
        case OnHitKind::ItemPoison: {
            std::optional<PoisonProfile> profile;
            if (auto poison = twoDas.get("poison")) {
                profile = loadPoisonProfile(*poison, property.paramValue);
            }
            target.applyEffect(
                game.newEffect<PoisonEffect>(static_cast<Poison>(property.paramValue), std::move(profile)),
                DurationType::Permanent);
            break;
        }
        case OnHitKind::InstantDeath:
            target.applyEffect(game.newEffect<DeathEffect>(false, true, false), DurationType::Instant);
            break;
        default:
            break;
        }
    }
}

} // namespace game

} // namespace reone
