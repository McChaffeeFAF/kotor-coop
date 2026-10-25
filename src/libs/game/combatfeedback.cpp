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

#include "reone/game/combatfeedback.h"

#include <map>

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/messagelog.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/resource/di/services.h"
#include "reone/resource/strings.h"

namespace reone {

namespace game {

static constexpr int kStrRefHitPointHealing = 1495;
static constexpr int kStrRefForceHealing = 1031;

bool canReceiveCombatFeedback(const Creature &controlled, const Creature &subject) {
    return controlled.faction() == subject.faction() &&
           controlled.getSquareDistanceTo(subject) <= kCombatFeedbackRangeSquared;
}

std::string feedbackText(Game &game, int strRef, std::initializer_list<std::pair<int, std::string>> tokens) {
    // All the tokens at once: resolving one at a time would turn the tokens
    // not yet filled into <UNRECOGNIZED TOKEN>.
    std::map<int, std::string> values(tokens.begin(), tokens.end());
    return game.substituteCustomTokens(game.services().resource.strings.getText(strRef), values);
}

std::optional<int> effectImmunityStrRef(ImmunityType immunity) {
    // dialog.tlk 1479-1492, the same rows in both games.
    switch (immunity) {
    case ImmunityType::Disease:
        return 1479;
    case ImmunityType::CriticalHit:
        return 1480;
    case ImmunityType::Fear:
        return 1481;
    case ImmunityType::Knockdown:
        return 1482;
    case ImmunityType::Paralysis:
        return 1483;
    case ImmunityType::NegativeLevel:
        return 1484;
    case ImmunityType::MindSpells:
        return 1485;
    case ImmunityType::Poison:
        return 1486;
    case ImmunityType::SneakAttack:
        return 1487;
    case ImmunityType::Sleep:
        return 1488;
    case ImmunityType::Confused:
    case ImmunityType::DroidConfused:
        return 1489;
    case ImmunityType::Stun:
        return 1490;
    case ImmunityType::Entangle:
        return 1491;
    case ImmunityType::Slow:
        return 1492;
    default:
        // Blindness, deafness, silence and the decreases have no line.
        return std::nullopt;
    }
}

// Whether the leader is one of the recipients; a recipient named twice is
// told once.
static bool leaderAmong(Game &game, std::initializer_list<const Object *> recipients) {
    auto leader = game.party().getLeader();
    if (!leader) {
        return false;
    }
    for (const Object *recipient : recipients) {
        if (recipient == leader.get()) {
            return true;
        }
    }
    return false;
}

static void addLine(Game &game, std::string text) {
    game.messageLog().add(MessageLog::kFeedbackMessageType, MessageLog::Style::Combat, std::move(text));
}

void addEffectImmunityFeedback(Game &game, const Object *creator, const Creature &target, ImmunityType immunity) {
    auto strRef = effectImmunityStrRef(immunity);
    if (!strRef) {
        return;
    }
    const Object *creatorCreature = creator && creator->type() == ObjectType::Creature ? creator : nullptr;
    if (!leaderAmong(game, {&target, creatorCreature})) {
        return;
    }
    addLine(game, feedbackText(game, *strRef, {{0, target.name()}}));
}

void addHitPointHealingFeedback(Game &game, const Object *creator, const Creature &target, int amount) {
    if (amount <= 0 || !leaderAmong(game, {&target, creator})) {
        return;
    }
    game.messageLog().add(
        MessageLog::kFeedbackMessageType,
        MessageLog::Style::Normal,
        feedbackText(game, kStrRefHitPointHealing, {{0, target.name()}, {1, std::to_string(amount)}}));
}

void addForceHealingFeedback(Game &game, const Creature &target, int amount) {
    if (amount <= 0 || !leaderAmong(game, {&target})) {
        return;
    }
    addLine(game, feedbackText(game, kStrRefForceHealing, {{0, target.name()}, {1, std::to_string(amount)}}));
}

} // namespace game

} // namespace reone
