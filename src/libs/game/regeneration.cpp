/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#include "reone/game/regeneration.h"

#include "reone/game/d20/forms.h"

#include <algorithm>
#include <cmath>

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"

using namespace reone::resource;

namespace reone {

namespace game {

// Smallest seconds-per-point the rule accepts.
static constexpr float kMinRegenTime = 0.0001f;
// Feat bonuses to the combat base (K2 feats; K1 has no such feat rows).
static constexpr float kVitalityFeatBonus = 0.5f;
static constexpr float kForceFeatBonus = 0.25f;
// Absorbs binary rounding when a gain sums to a whole point exactly, so
// 180 ticks of 1/180 give one point on the 180th tick, not the 181st.
static constexpr double kWholeEpsilon = 1e-9;

RegenerationRules RegenerationRules::k2Defaults() {
    return RegenerationRules();
}

RegenerationRules RegenerationRules::k1Defaults() {
    RegenerationRules rules;
    // K1 regeneration.2da: forceregen InCombat 0.0, OutOfCombat 1.0.
    rules.outOfCombatForceBase = 1.0f;
    rules.inCombatForceBase = 0.0f;
    // Everything else is K2 behaviour (owner decision, 2026-09-24).
    return rules;
}

static void readValue(const TwoDA &table, const char *label, float &out) {
    int row = table.indexByLabel(label);
    if (row < 0) {
        return;
    }
    if (auto value = table.getFloatOpt(row, "value")) {
        out = *value;
    }
}

RegenerationRules RegenerationRules::fromTable(const TwoDA *table, bool tsl) {
    RegenerationRules rules = tsl ? k2Defaults() : k1Defaults();
    if (!table) {
        return rules;
    }
    if (tsl) {
        readValue(*table, "timeperhp", rules.timePerHitPoint);
        readValue(*table, "outofcombathpbase", rules.outOfCombatHitPointBase);
        readValue(*table, "incombathpbase", rules.inCombatHitPointBase);
        readValue(*table, "conmodbonus", rules.constitutionBonus);
        readValue(*table, "skillrankbonus", rules.skillBonus);
        readValue(*table, "fpregentime", rules.forceRegenTime);
        readValue(*table, "outofcombatfpbase", rules.outOfCombatForceBase);
        readValue(*table, "incombatfpbase", rules.inCombatForceBase);
        readValue(*table, "wismodbonus", rules.wisdomBonus);
        return rules;
    }
    int inCombat = table->indexByCellValue("label", "InCombat");
    int outOfCombat = table->indexByCellValue("label", "OutOfCombat");
    if (inCombat >= 0) {
        if (auto value = table->getFloatOpt(inCombat, "forceregen")) {
            rules.inCombatForceBase = *value;
        }
        // A zero healthregen is "not in the table": keep the default.
        if (auto value = table->getFloatOpt(inCombat, "healthregen"); value && *value > 0.0f) {
            rules.inCombatHitPointBase = *value;
        }
    }
    if (outOfCombat >= 0) {
        if (auto value = table->getFloatOpt(outOfCombat, "forceregen")) {
            rules.outOfCombatForceBase = *value;
        }
        if (auto value = table->getFloatOpt(outOfCombat, "healthregen"); value && *value > 0.0f) {
            rules.outOfCombatHitPointBase = *value;
        }
    }
    return rules;
}

float naturalHitPointsPerSecond(const RegenerationRules &rules, const RegenerationSubject &subject) {
    float base = subject.inCombat ? rules.inCombatHitPointBase : rules.outOfCombatHitPointBase;
    base += rules.constitutionBonus * static_cast<float>(subject.constitutionModifier);
    base += rules.skillBonus * static_cast<float>(subject.healingSkill);
    if (subject.vitalityFeat) {
        base += kVitalityFeatBonus;
    }
    for (int i = 0; i < subject.vitalityModifierEffects; ++i) {
        base += base;
    }
    // A low Constitution can push the base below zero; natural regeneration
    // never drains hit points.
    return std::max(0.0f, base) / std::max(kMinRegenTime, rules.timePerHitPoint);
}

float naturalForcePointsPerSecond(const RegenerationRules &rules, const RegenerationSubject &subject) {
    if (subject.maxForcePoints <= 0) {
        return 0.0f;
    }
    float base = subject.inCombat
                     ? forms::inCombatForceRegenBase(subject.activeForm, subject.wieldsLightsaber,
                                                     rules.inCombatForceBase, rules.outOfCombatForceBase)
                     : rules.outOfCombatForceBase;
    base += forms::forceRegenBonus(subject.activeForm);
    base += rules.wisdomBonus * static_cast<float>(subject.wisdomModifier);
    if (subject.forceFeat) {
        base += kForceFeatBonus;
    }
    base += static_cast<float>(subject.forceModifierPercent) / 100.0f;
    return static_cast<float>(subject.maxForcePoints) * base / std::max(kMinRegenTime, rules.forceRegenTime);
}

int RegenerationAccumulator::take() {
    double whole = _value >= 0.0 ? std::floor(_value + kWholeEpsilon) : std::ceil(_value - kWholeEpsilon);
    if (whole == 0.0) {
        return 0;
    }
    _value -= whole;
    return static_cast<int>(whole);
}

int regenerateTowards(RegenerationAccumulator &accumulator, double perSecond, float dt, int current, int cap) {
    // Nothing accrues while there is no room: full pools bank nothing.
    const bool room = perSecond > 0.0 ? current < cap : current > 0;
    if (!room || perSecond == 0.0 || dt <= 0.0f) {
        return current;
    }
    accumulator.add(perSecond * dt);
    int gain = accumulator.take();
    return gain == 0 ? current : std::clamp(current + gain, 0, std::max(current, cap));
}

Regeneration::Regeneration(Game &game, ServicesView &services) :
    _game(game),
    _services(services) {
}

Regeneration::~Regeneration() = default;

const RegenerationRules &Regeneration::rules() {
    if (!_rulesLoaded) {
        _rulesLoaded = true;
        std::shared_ptr<TwoDA> table;
        try {
            table = _services.resource.twoDas.get("regeneration");
        } catch (const std::exception &) {
            table.reset();
        }
        _rules = RegenerationRules::fromTable(table.get(), _game.isTSL());
    }
    return _rules;
}

void Regeneration::setRules(RegenerationRules rules) {
    _rules = rules;
    _rulesLoaded = true;
}

void Regeneration::reset() {
    _states.clear();
}

void Regeneration::update(float dt) {
    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) {
        return;
    }
    std::map<uint32_t, bool> seen;
    for (const auto &object : area->getObjectsByType(ObjectType::Creature)) {
        auto creature = std::static_pointer_cast<Creature>(object);
        updateCreature(*creature, dt, _game.party().isMember(*creature));
        seen[creature->id()] = true;
    }
    // Creatures that left the area lose their fractions.
    for (auto it = _states.begin(); it != _states.end();) {
        it = seen.count(it->first) ? std::next(it) : _states.erase(it);
    }
}

void Regeneration::updateCreature(Creature &creature, float dt, bool partyMember) {
    if (dt <= 0.0f) {
        return;
    }
    if (creature.isDead() || creature.currentHitPoints() < 1) {
        _states.erase(creature.id());
        return;
    }
    const bool disabled = _game.party().persistedState().regenerationDisabled;
    const int itemHitPoints = disabled ? 0 : creature.getItemRegeneration(false);
    const int itemForcePoints = creature.getItemRegeneration(true);
    if (!partyMember && itemHitPoints == 0 && itemForcePoints == 0) {
        _states.erase(creature.id());
        return;
    }
    const auto &rules = this->rules();
    const float speed = std::max(0.0f, _game.options().game.regenRate);

    RegenerationSubject subject;
    subject.inCombat = creature.isInCombat();
    const float natural = subject.inCombat ? 1.0f : speed;
    double hitPointsPerSecond = static_cast<double>(itemHitPoints) / kItemRegenerationInterval;
    double forcePointsPerSecond = static_cast<double>(itemForcePoints) / kItemRegenerationInterval;
    if (partyMember) {
        subject.constitutionModifier = creature.getEffectiveAbilityModifier(Ability::Constitution);
        subject.wisdomModifier = creature.getEffectiveAbilityModifier(Ability::Wisdom);
        subject.healingSkill = creature.getSkillValue(
            creature.racialType() == RacialType::Droid ? SkillType::Repair : SkillType::TreatInjury);
        if (_game.isTSL() || _services.resource.k2Merge) {
            // K2 feats: in K1 only with the K2 systems merged.
            subject.vitalityFeat = creature.hasEffectiveFeat(FeatType::RegenerateVitalityPoints);
            subject.forceFeat = creature.hasEffectiveFeat(FeatType::RegenerateForcePoints);
        }
        subject.activeForm = creature.activeForm();
        subject.wieldsLightsaber = creature.wieldsLightsaber();
        subject.vitalityModifierEffects = creature.getVitalityRegenModifierCount();
        subject.forceModifierPercent = creature.getForcePointRegenPercent();
        subject.maxForcePoints = creature.maxForcePoints();
        if (!disabled) {
            hitPointsPerSecond += natural * naturalHitPointsPerSecond(rules, subject);
        }
        forcePointsPerSecond += natural * naturalForcePointsPerSecond(rules, subject);
    }

    auto &state = _states[creature.id()];

    // Current values include the temporary pools; the maxima do not.
    const int hitPointCap = creature.maxHitPoints() + creature.temporaryHitPoints();
    int hitPoints = regenerateTowards(state.hitPoints, hitPointsPerSecond, dt, creature.currentHitPoints(), hitPointCap);
    if (hitPoints > creature.currentHitPoints()) {
        creature.setCurrentHitPoints(hitPoints);
    }
    const int forceCap = creature.maxForcePoints() + creature.temporaryForcePoints();
    int force = regenerateTowards(state.forcePoints, forcePointsPerSecond, dt, creature.currentForce(), forceCap);
    if (force != creature.currentForce()) {
        creature.setCurrentForcePoints(force);
    }
}

} // namespace game

} // namespace reone
