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

#include "reone/game/action/follow.h"

#include <algorithm>

#include "reone/game/di/services.h"
#include "reone/game/game.h"

namespace reone {

namespace game {

void FollowAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto creatureActor = _game.getObjectById<Creature>(actor.id());
    if (!creatureActor || !_follow || !_follow->isRuntimeLive() || _follow.get() == &actor) {
        complete();
        return;
    }

    // ActionForceFollowObject follows until the queue is cleared (nss): the
    // action never completes on arrival. At rest the follower waits, and sets
    // off again once the followee has moved away. If the followee cannot be
    // reached, it keeps trying; each attempt gives up after the navigation
    // give-up time and the next one plans afresh.
    float followDistance = std::max(_followDistance, kMinFollowDistance);
    auto dest = _follow->position();
    float distance2 = creatureActor->getSquareDistanceTo(glm::vec2(dest));
    if (!_moving) {
        float resume = followDistance + kResumeSlack;
        if (distance2 <= resume * resume) {
            return;
        }
        _moving = true;
    }
    bool run = distance2 > kDistanceWalk * kDistanceWalk;
    if (creatureActor->navigateTo(dest, run, followDistance, dt)) {
        _moving = false;
    }
}

} // namespace game

} // namespace reone
