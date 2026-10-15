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

#include "commonactions.h"
#include "reone/system/portablemath.h"
#include "reone/game/pathfinder.h"
#include "reone/game/object/area.h"
#include "reone/game/object/module.h"

#include <algorithm>

#include <glm/gtx/norm.hpp>

#include "reone/game/action.h"
#include "reone/game/game.h"
#include "reone/game/messagelog.h"
#include "reone/game/object.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/system/randomutil.h"

namespace reone {

namespace game {

void tryUnlockDoorWithKey(Game &game, Door &door, Object &actor, Party &party) {
    if (!door.isLocked() || !door.isKeyRequired() || door.keyName().empty()) {
        return;
    }
    Object *keyOwner = &actor;
    auto key = actor.getItemByTag(door.keyName());
    if (!key) {
        auto player = party.player();
        if (player && player->id() != actor.id()) {
            key = player->getItemByTag(door.keyName());
            keyOwner = player.get();
        }
    }
    if (!key) {
        return;
    }
    door.setLocked(false);
    if (door.isAutoRemoveKey()) {
        bool last = false;
        keyOwner->removeItem(key, last);
        if (last) {
            game.destroyRuntimeObjectGraph(key);
        }
    }
}

glm::vec3 doorApproachPoint(Game &game, const Door &door, const Object &actor) {
    // Where retail walks to: the door walkmesh's use point on the actor's
    // side. That point can fall in the gap the shut door fills in the area
    // walkmesh, so step out from the door along the same line until the
    // ground is walkable.
    glm::vec2 from(actor.position());
    glm::vec3 center(door.position());
    glm::vec3 use(center);
    auto uses = door.useWorldPositions();
    if (!uses.empty()) {
        use = *std::min_element(uses.begin(), uses.end(), [&from](const glm::vec3 &a, const glm::vec3 &b) {
            return glm::distance2(glm::vec2(a), from) < glm::distance2(glm::vec2(b), from);
        });
    }
    glm::vec2 out(glm::vec2(use) - glm::vec2(center));
    if (glm::length(out) < 0.01f) {
        float facing = door.getFacing();
        out = glm::vec2(-portable::sinf(facing), portable::cosf(facing));
        if (glm::dot(out, from - glm::vec2(center)) < 0.0f) {
            out = -out;
        }
    }
    out = glm::normalize(out);
    auto module = game.module();
    auto area = module ? module->area() : nullptr;
    float start = uses.empty() ? 1.0f : glm::length(glm::vec2(use) - glm::vec2(center));
    for (float along = start; along <= start + 3.0f; along += 0.5f) {
        glm::vec3 point(center.x + out.x * along, center.y + out.y * along, use.z);
        if (!area || isOnWalkmesh(area->pathfinder().uni, point)) {
            return point;
        }
    }
    return glm::vec3(center.x + out.x * start, center.y + out.y * start, use.z);
}

bool approachDoor(Game &game, Creature &creature, const Door &door, bool &useApproachPoint, float distance, float dt) {
    if (!useApproachPoint) {
        if (creature.navigateTo(door.position(), true, distance, dt)) {
            return true;
        }
        if (!creature.navigationFailed()) {
            return false;
        }
        useApproachPoint = true;
    }
    return creature.navigateTo(doorApproachPoint(game, door, creature), true, std::min(distance, 0.5f), dt);
}

int securityRoll(const Creature &creature, bool &tookTwenty) {
    tookTwenty = !creature.isInCombat();
    return tookTwenty ? 20 : randomInt(1, 20);
}

bool securityCheckSucceeds(int total, int openLockDC) {
    return total >= openLockDC;
}

/**
 * Rolls Security (the d20/take-20 part plus the creature's Security skill
 * value) against a lock's Open Lock DC. On failure, shows feedback so the
 * player understands the attempt did not open it; the caller leaves the lock
 * untouched, so issuing the same order again is the retry.
 */
static bool attemptSecurityCheck(Game &game, Creature &creature, int openLockDC) {
    bool tookTwenty = false;
    int total = securityRoll(creature, tookTwenty) + creature.getSkillValue(SkillType::Security);
    if (securityCheckSucceeds(total, openLockDC)) {
        return true;
    }
    game.messageLog().add(
        MessageLog::kFeedbackMessageType,
        MessageLog::Style::Normal,
        "You have failed to unlock it.");
    return false;
}

bool unlockDoor(Game &game, Door &door, Object &actor, bool &useApproachPoint, float distance, float dt) {
    if (actor.type() == ObjectType::Creature) {
        auto &creature = static_cast<Creature &>(actor);
        bool reached = approachDoor(game, creature, door, useApproachPoint, distance, dt);
        if (!reached) {
            // The door cannot be reached: drop the action.
            return creature.navigationFailed();
        }
        creature.face(door);
        creature.playAnimation(AnimationType::LoopingUnlockDoor);
    }

    // FIXME: wait for animation to play

    if (door.isKeyRequired()) {
        // FIXME: run onFailedToOpen?
        return true;
    }

    if (door.isLocked()) {
        if (auto *creature = dyn_cast<Creature>(&actor)) {
            if (!attemptSecurityCheck(game, *creature, door.openLockDC())) {
                door.onFailToOpen(actor);
                return true;
            }
        }
        door.setLocked(false);
    }

    door.open(&actor);
    door.onOpen(actor.id());

    return true;
}

bool unlockPlaceable(Placeable &placeable, Object &actor, float distance, float dt) {
    if (auto *creature = dyn_cast<Creature>(&actor)) {
        bool reached = creature->navigateTo(placeable.position(), true, distance, dt);
        if (!reached) {
            return false;
        }
        creature->face(placeable);
        creature->playAnimation(AnimationType::LoopingUnlockDoor);
    }

    // FIXME: wait for animation to play

    if (placeable.isKeyRequired()) {
        return true;
    }

    if (placeable.isLocked()) {
        if (auto *creature = dyn_cast<Creature>(&actor)) {
            if (!attemptSecurityCheck(actor.game(), *creature, placeable.openLockDC())) {
                return true;
            }
        }
        placeable.setLocked(false);
    }

    return true;
}

void jumpToPositionFacing(Object &actor, const glm::vec3 &position,
                          float facing, Game &game) {
    actor.setPosition(position);
    actor.setFacing(facing);
    actor.snapPresentation();

    auto module = game.module();
    if (!module) {
        return;
    }

    auto area = game.module()->area();
    if (!area) {
        return;
    }

    Room *roomBefore = actor.room();
    area->determineObjectRoom(actor);
    Room *roomAfter = actor.room();

    if (auto leader = game.party().getLeader()) {
        if (leader->id() == actor.id()) {
            area->onPartyLeaderMoved(roomBefore != roomAfter);
        }
    }
}

} // namespace game

} // namespace reone
