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

#include "reone/game/action/followleader.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"

namespace reone {

namespace game {

void FollowLeaderAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // The party has no leader while it is empty: before it is first populated,
    // after a module transition resets it, and once the last member is removed
    // by RemovePartyMember. A FollowLeader action queued on a creature can still
    // be executed in those windows, so there is nothing to follow and the action
    // is dropped instead of dereferencing a null leader.
    auto leader = _game.party().getLeader();
    if (!leader) {
        complete();
        return;
    }

    auto creatureActor = _game.getObjectById<Creature>(actor.id());
    if (!creatureActor) {
        complete();
        return;
    }
    if (creatureActor == leader) {
        // The follower was made leader (party switch): nothing to follow.
        creatureActor->clearPath();
        complete();
        return;
    }

    glm::vec3 destination(leader->position());
    float distance2 = creatureActor->getSquareDistanceTo(glm::vec2(destination));
    bool run = distance2 > kDistanceWalk * kDistanceWalk;
    if (creatureActor->navigateTo(destination, run, kPartyFollowDistance, dt)) {
        complete();
        return;
    }
    if (!creatureActor->navigationFailed()) {
        return;
    }

    // The leader cannot be reached (no route, a door this member cannot
    // open, or a blockage that outlasted the give-up time). The henchman
    // heartbeat re-issues the follow, so the action ends here. As a last
    // resort, a member far behind and out of the leader's sight rejoins at
    // its formation slot, as on area entry; never during combat or a
    // conversation.
    complete();
    if (distance2 < kRegroupDistance * kRegroupDistance) {
        return;
    }
    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    if (!area || _game.isConversationActive() || leader->isInCombat() || creatureActor->isInCombat() ||
        area->isObjectSeen(*leader, *creatureActor)) {
        return;
    }
    creatureActor->clearPath();
    creatureActor->setMovementType(Creature::MovementType::None);
    area->regroupWithLeader(creatureActor);
}

std::optional<SavedActionRecord> FollowLeaderAction::saveFacingState() const {
    // Retail AI action 61 is the abstract party-follow command. It deliberately
    // carries no target or formation parameters: execution resolves the current
    // party leader, while Party/FollowInfo owns formation state.
    SavedActionRecord result = originalSavedAction().value_or(SavedActionRecord {});
    result.actionId = 61;
    result.declaredParameterCount = 0;
    result.parameters.clear();
    return result;
}

} // namespace game

} // namespace reone
