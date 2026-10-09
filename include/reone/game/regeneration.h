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

#pragma once

#include <cstdint>
#include <map>

namespace reone {

namespace resource {

class TwoDA;

}

namespace game {

class Creature;
class Game;
struct ServicesView;

/**
 * Natural hit point and Force point regeneration (design note regeneration).
 * All rates are per second of simulation time.
 */
struct RegenerationRules {
    float timePerHitPoint {6.0f};
    float outOfCombatHitPointBase {1.0f};
    float inCombatHitPointBase {0.0f};
    float constitutionBonus {0.3f};
    float skillBonus {0.1f};
    float forceRegenTime {90.0f};
    float outOfCombatForceBase {1.0f};
    float inCombatForceBase {0.1f};
    float wisdomBonus {0.0f};

    /** K2 regeneration.2da values; also the engine defaults. */
    static RegenerationRules k2Defaults();

    /**
     * K1 follows K2 behaviour (owner decision): the FP combat bases come from
     * K1's regeneration.2da, everything else from K2's.
     */
    static RegenerationRules k1Defaults();

    /** Rules read from a K1 (label/healthregen/forceregen) or K2 (value) table. */
    static RegenerationRules fromTable(const resource::TwoDA *table, bool tsl);
};

struct RegenerationSubject {
    bool inCombat {false};
    int constitutionModifier {0};
    int wisdomModifier {0};
    /** Treat Injury, or Repair for a droid: total skill value. */
    int healingSkill {0};
    bool vitalityFeat {false};
    bool forceFeat {false};
    /** Active EffectVPRegenModifier count: each doubles the hit point base. */
    int vitalityModifierEffects {0};
    /** Sum of active EffectFPRegenModifier percentages. */
    int forceModifierPercent {0};
    int maxForcePoints {0};
    /** K2 Force forms (the K2 systems merge): the active form, and whether a
        lightsaber is wielded (Niman's in-combat regeneration needs one). */
    int activeForm {0};
    bool wieldsLightsaber {false};
};

/** Natural hit points per second, before the regeneration speed option. */
float naturalHitPointsPerSecond(const RegenerationRules &rules, const RegenerationSubject &subject);

/** Natural Force points per second, before the regeneration speed option. */
float naturalForcePointsPerSecond(const RegenerationRules &rules, const RegenerationSubject &subject);

/** Item Regeneration / RegenerationForcePoints: N points per this many seconds. */
constexpr float kItemRegenerationInterval = 6.0f;

/**
 * A fractional pool. Gains accumulate; take() moves out the whole part (toward
 * zero) and keeps the fraction. Deterministic, no randomness.
 */
class RegenerationAccumulator {
public:
    void add(double amount) { _value += amount; }
    int take();
    double value() const { return _value; }
    void reset() { _value = 0.0; }

private:
    double _value {0.0};
};

/**
 * Advance one pool by perSecond * dt through its accumulator: the new current
 * value, within 0..cap (a value already above cap is never lowered).
 */
int regenerateTowards(RegenerationAccumulator &accumulator, double perSecond, float dt, int current, int cap);

class Regeneration {
public:
    Regeneration(Game &game, ServicesView &services);
    ~Regeneration();

    /** One simulation tick: every living creature of the current area. */
    void update(float dt);

    /**
     * One creature for dt seconds. partyMember gates natural regeneration;
     * item properties apply to anyone.
     */
    void updateCreature(Creature &creature, float dt, bool partyMember);

    /** Drop all fractional state (module change, load). */
    void reset();

    const RegenerationRules &rules();

    void setRules(RegenerationRules rules);

private:
    struct State {
        RegenerationAccumulator hitPoints;
        RegenerationAccumulator forcePoints;
    };

    Game &_game;
    ServicesView &_services;
    bool _rulesLoaded {false};
    RegenerationRules _rules;
    // Keyed by object id; ordered so iteration is deterministic.
    std::map<uint32_t, State> _states;
};

} // namespace game

} // namespace reone
