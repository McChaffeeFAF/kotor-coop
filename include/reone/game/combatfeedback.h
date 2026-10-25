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

#include <initializer_list>
#include <optional>
#include <string>
#include <utility>

#include "types.h"

namespace reone {

namespace game {

class Creature;
class Game;
class Object;

/**
 * Feedback-log lines about effects (design note vrifftech_rules_triage,
 * "Combat feedback"). Each line is written to this machine's log, and only
 * when one of its recipients is the creature this machine's player controls
 * (the party leader). A co-op guest's own lines are not sent to it.
 */

/** The square of the distance within which combat lines about a creature reach the leader: 30 metres. */
constexpr float kCombatFeedbackRangeSquared = 900.0f;

/** Combat lines about \p subject reach \p controlled: same faction, within 30 metres. */
bool canReceiveCombatFeedback(const Creature &controlled, const Creature &subject);

/** The text of \p strRef with its <CUSTOMn> tokens filled from \p tokens. */
std::string feedbackText(Game &game, int strRef, std::initializer_list<std::pair<int, std::string>> tokens = {});

/**
 * The talk-table line naming a creature immune to an effect, by the immunity
 * that refused it ("<name> is immune to Stun"), or none for an immunity type
 * that has no line of its own.
 */
std::optional<int> effectImmunityStrRef(ImmunityType immunity);

/**
 * An effect was refused by the target's immunity: the line naming the target
 * goes to the target and to the effect's creator when that is a creature.
 * A creature that is both is told once.
 */
void addEffectImmunityFeedback(Game &game, const Object *creator, const Creature &target, ImmunityType immunity);

/**
 * Hit points healed: "<name> : Healed <n> hit points.", told to the healed
 * creature and to a different creator.
 */
void addHitPointHealingFeedback(Game &game, const Object *creator, const Creature &target, int amount);

/**
 * Force points restored: "<name> regains <n> Force Points.", told only to the
 * player controlling the healed creature.
 */
void addForceHealingFeedback(Game &game, const Creature &target, int amount);

} // namespace game

} // namespace reone
