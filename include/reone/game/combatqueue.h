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

#include <vector>

#include "reone/net/protocol.h"

#include "types.h"

namespace reone {

namespace game {

class Action;
class Object;

/**
 * What a player character's combat queue shows on the HUD, one slot per
 * action. The host fills it from the real actions (a talent the party AI
 * queued shows as the feat or power it dispatches); a co-op guest, whose
 * client runs no actions for its character, gets the same list replicated.
 */
namespace combatqueue {

/** The slot kind for an action type; param is filled by the caller. */
net::QueueKind kindForActionType(ActionType type);

/** The slot one action shows. */
net::QueueEntry entryFor(const Action &action);

/** The first kMaxQueueEntries actions of an object's queue, as HUD slots. */
std::vector<net::QueueEntry> entriesOf(const Object &object);

} // namespace combatqueue

} // namespace game

} // namespace reone
