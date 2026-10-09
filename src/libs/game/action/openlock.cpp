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

#include "reone/game/action/openlock.h"

#include "reone/system/logutil.h"

#include "commonactions.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/door.h"
#include "reone/game/script/runner.h"

namespace reone {

namespace game {

void OpenLockAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (_target->type() == ObjectType::Door) {
        if (unlockDoor(_game, static_cast<Door &>(*_target), actor, _doorApproach, kDefaultMaxObjectDistance, dt)) {
            complete();
        }
        return;
    }

    warn("ActionExecutor: unsupported OpenLockAction target");
    complete();
}

} // namespace game

} // namespace reone
