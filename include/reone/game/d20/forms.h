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

#include "../types.h"

#include <functional>
#include <vector>

namespace reone {

namespace game {

/**
 * K2's lightsaber and Force forms in K1 (design note k2_merge, "Force
 * forms"). A form is a spells.2da row of user type 6 (rows 258-268 of the
 * merged table); a creature knows forms like powers and has at most one
 * active form (saved as "CurrentForm"). K2 behaviour; numbers from K2's form
 * descriptions, checked against the K2 engine.
 */
namespace forms {

constexpr int kShiiCho = 258;  // I, Determination
constexpr int kMakashi = 259;  // II, Contention
constexpr int kSoresu = 260;   // III, Resilience
constexpr int kAtaru = 261;    // IV, Aggression
constexpr int kShien = 262;    // V, Perseverance
constexpr int kNiman = 263;    // VI, Moderation
constexpr int kJuyo = 264;     // VII, Ferocity
constexpr int kChannel = 265;  // Force Channel
constexpr int kPotency = 266;  // Force Potency
constexpr int kAffinity = 267; // Force Affinity
constexpr int kMastery = 268;  // Force Mastery

/// The spells.2da user type of a form.
constexpr int kFormUserType = 6;

/// No active form.
constexpr int kNoForm = 0;

bool isForm(int spell);
bool isSaberForm(int spell);
bool isForceForm(int spell);

/**
 * Whether a form is in effect: it is the creature's active form and, for a
 * lightsaber form, the creature wields a lightsaber (IsFormActive).
 */
bool isInEffect(int activeForm, int form, bool wieldsLightsaber);

/// The lightsaber form in effect, or kNoForm.
int saberFormInEffect(int activeForm, bool wieldsLightsaber);

/// Attack modifier of the active lightsaber form (I +1, II +3 against a
/// lightsaber wielder, V +2, VI +1).
int attackModifier(int saberForm, bool targetWieldsLightsaber);

/// Damage modifier of the active lightsaber form (II +3).
int damageModifier(int saberForm);

/**
 * Defense modifier of the active lightsaber form against an attacker. The
 * "current target" is the creature the defender is attacking. Net values:
 * I +3 (+0 against the current target), III +2 against the current target,
 * IV -2 (+3 against the current target), V -5 against the current target,
 * VI +1, VII -4 (-2 against the current target).
 */
int defenseModifier(int saberForm, bool attackerIsCurrentTarget);

/// Weapon threat range change: III -1, IV +1.
int threatRangeModifier(int saberForm);

/// Critical multiplier change: V +1.
int criticalMultiplierModifier(int saberForm);

/// Critical confirmation roll bonus: VII +4.
int criticalConfirmationBonus(int saberForm);

/// Extra attacks per round: VII +1.
int extraAttacks(int saberForm);

/**
 * Save modifier against Force powers: II +2, VI +1, VII -4 (lightsaber forms
 * need a lightsaber), Force Channel +2, Force Mastery -4.
 */
int forceSaveModifier(int activeForm, bool wieldsLightsaber);

/// Modifier to the saves of the targets of a caster's Force powers: Force
/// Mastery -2.
int opponentForceSaveModifier(int activeForm);

/// Force point cost multiplier: Potency and Mastery +20%.
float forcePointCostMultiplier(int activeForm);

/**
 * FP regeneration base in combat, given the table's in- and out-of-combat
 * bases: Niman (with a lightsaber) regenerates at 0.3 of the out-of-combat
 * base, Force Affinity at 0.6 of it; anything else uses the in-combat base.
 */
float inCombatForceRegenBase(int activeForm, bool wieldsLightsaber, float inCombatBase, float outOfCombatBase);

/// Force Channel adds +0.5 to the FP regeneration base, in and out of combat.
float forceRegenBonus(int activeForm);

/**
 * Forms a Force user learns (ours; K1 has no form teachers): at Force-class
 * level 1 Shii-Cho and Force Channel; 2 Soresu; 6 Makashi; 10 Niman; and K2's
 * own story progression (the order K2's teachers follow) at 4, 8 and 12:
 * the Guardian line (Guardian, Weapon Master, Marauder) Ataru, Shien, Juyo;
 * the Consular line (Consular, Jedi Master, Sith Lord) Potency or Affinity
 * by alignment (dark first below 50), the other, then Mastery; the Sentinel
 * line (Sentinel, Watchman, Assassin) Shien, Ataru, Niman.
 *
 * @param lineClass the prestige class if any, else the Jedi class
 * @param forceLevel the sum of the creature's Force-class levels
 * @return every form due at or below forceLevel, in grant order
 */
std::vector<int> formsDue(ClassType lineClass, int forceLevel, int goodEvil);

} // namespace forms

} // namespace game

} // namespace reone
