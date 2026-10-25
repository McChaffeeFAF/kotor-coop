/*
 * Copyright (c) 2025 The reone project contributors
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

#include <glm/vec3.hpp>

#include "reone/game/action/objectapproach.h"

namespace reone {

namespace game {

class Door;
class Creature;
class Game;
class Object;
class Party;
class Placeable;

// If the door is locked and requires a named key, look for that key in the
// actor's inventory and then the party player's, unlock the door if found, and
// consume one key when AutoRemoveKey is set. Leaves the door locked otherwise.
void tryUnlockDoorWithKey(Game &game, Door &door, Object &actor, Party &party);

// Where a creature stands to use a door: the door walkmesh's use point on the
// actor's side, stepped out onto walkable ground.
glm::vec3 doorApproachPoint(Game &game, const Door &door, const Object &actor);

// Walk a creature up to a door. A shut door stands in its doorway, often off
// the walkmesh, so when no path leads to the door itself the creature walks to
// a point just in front of it on its own side instead (useApproachPoint then
// stays set for the action). Returns true once there. When neither can be
// reached, returns false with creature.navigationFailed() set.
bool approachDoor(Game &game, Creature &creature, const Door &door, bool &useApproachPoint, float distance, float dt);

// Where a creature walks to use an object (a container, a body, a use-on
// placeable) that stands off the walkmesh, as the footprint of a placeable
// does: the nearest walkable ground around it that the creature can walk to,
// searched on rings out to a few metres. Returns the object's own position
// when it stands on the walkmesh or nothing reachable lies near it.
glm::vec3 objectUsePoint(Game &game, const Object &target, const Object &actor);

// Walk a creature up to an object. When the object itself is off the walkmesh
// the creature walks to its use point and counts as arrived within 0.5 m of
// it. Returns true once there; when it cannot be reached, returns false with
// creature.navigationFailed() set.
bool approachObject(Game &game, Creature &creature, const Object &target, bool run, ObjectApproach &approach, float distance, float dt);

// Move an actor to a door, unlock and open it. Returns true when this action is
// complete (also when the door cannot be reached).
bool unlockDoor(Game &game, Door &door, Object &actor, bool &useApproachPoint, float distance, float dt);

// Move an actor to a placeable and unlock it. Returns true when this action is
// complete.
bool unlockPlaceable(Placeable &placeable, Object &actor, float distance, float dt);

// The d20 part of a Security roll: 20 out of combat (take 20, as Demolitions
// does), else a d20.
int securityRoll(const Creature &creature, bool &tookTwenty);

// True when a roll total (the d20 or take-20 part plus the Security skill
// value) clears the lock's Open Lock DC.
bool securityCheckSucceeds(int total, int openLockDC);

// Set position and facing of an actor, and update area visibility.
void jumpToPositionFacing(Object &actor, const glm::vec3 &position,
                          float facing, Game &game);

} // namespace game

} // namespace reone
