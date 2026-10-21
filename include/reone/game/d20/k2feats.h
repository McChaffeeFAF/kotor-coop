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

namespace reone {

namespace game {

/**
 * Passive effects of the K2 feats the K2 systems merge appends to K1's
 * feat.2da (design note k2_merge, "Feat effects"). K2 behaviour; the
 * numbers follow the K2 engine, which the feats' own K2 descriptions state.
 *
 * Every rule takes a feat test, so a creature without these feats (every
 * creature in a K1 session without K2) gets exactly 0 from each.
 */
namespace k2feats {

using HasFeat = std::function<bool(FeatType)>;

/// Targeting I-X: the highest tier owned, +1 per tier. Ranged attacks only.
int targetingAttackBonus(const HasFeat &hasFeat);

/// Close Combat / Improved Close Combat: +1 each, for a ranged attack at a
/// target within kCloseCombatRange metres.
int closeCombatAttackBonus(const HasFeat &hasFeat);
constexpr float kCloseCombatRange = 5.0f;

/// Attack bonus for a melee attacker against a target holding a ranged
/// weapon: +6, +4 with Close Combat, +2 with Improved Close Combat (the
/// highest owned wins). Applies with no feats at all.
int meleeOnRangedAttackBonus(const HasFeat &hasFeat);

/// Superior Weapon Focus: Lightsaber I-III: the highest tier, +1 per tier.
/// Lightsaber attacks only.
int superiorLightsaberFocusBonus(const HasFeat &hasFeat);

/// Superior Two-Weapon Fighting I-III: the main hand gains +1 / +2 / +2,
/// the off hand +1 at tier III only. Only while fighting with two weapons
/// (or a double-bladed weapon).
int superiorTwoWeaponBonus(const HasFeat &hasFeat, bool offHand);

/// Dual Strike, Improved and Master: +2 for each owned, when another party
/// member is attacking the same target.
int dualStrikeAttackBonus(const HasFeat &hasFeat);

/// Increase Combat Damage I-III (any attack) or Increase Melee Damage I-III
/// (melee and unarmed): +2 / +4 / +6, the highest tier of either chain for
/// melee and unarmed; ranged attacks count Increase Combat Damage only.
int combatDamageBonus(const HasFeat &hasFeat, bool ranged);

/// Precise Shot I-V: +1 / +2 / +4 / +6 / +8 damage with ranged weapons.
int preciseShotDamageBonus(const HasFeat &hasFeat);

/// Unarmed Specialist I-VIII: the highest tier N adds Nd4 to unarmed damage.
int unarmedSpecialistDice(const HasFeat &hasFeat);

/**
 * Damage a creature takes after its K2 damage-reduction feats: Ignore Pain
 * or Inner Strength I-III take 5% / 10% / 15% off (truncated), then Wookiee
 * Toughness II takes 5 plus 5% of the rest, III takes 8 plus 10% of the rest.
 * Never below 0.
 */
int reduceDamage(const HasFeat &hasFeat, int damage);

/// Evasion: a successful Reflex save against a "half damage" effect avoids
/// all of it.
bool hasEvasion(const HasFeat &hasFeat);

/// Damage after a Reflex save (GetReflexAdjustedDamage): half on a success,
/// nothing with Evasion, all of it on a failure.
int reflexAdjustedDamage(int damage, bool saved, bool evasion);

/// The Class Skill: <skill> feat that makes a skill a class skill, or Invalid.
FeatType classSkillFeat(SkillType skill);

/// Stealth Run: the creature may run in stealth mode.
bool hasStealthRun(const HasFeat &hasFeat);

} // namespace k2feats

} // namespace game

} // namespace reone
