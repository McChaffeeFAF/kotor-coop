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

#include "reone/game/action/opendoor.h"

#include "commonactions.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/door.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/script/runner.h"

namespace reone {

namespace game {

void OpenDoorAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (actor.type() == ObjectType::Creature) {
        auto &creature = static_cast<Creature &>(actor);
        bool reached = approachDoor(_game, creature, *_door, _doorApproach, kDefaultMaxObjectDistance, dt);
        if (!reached) {
            if (creature.navigationFailed()) {
                // The door cannot be reached: drop the action.
                complete();
            }
            return;
        }
        creature.face(*_door);
    }

    // Allow a door to open itself, bypassing all requirements. This is used by
    // scripts that assign actions to doors.
    bool isObjectSelf = _door->id() == actor.id();
    if (isObjectSelf) {
        _door->open();
        _door->onOpen(actor.id());
        complete();
        return;
    }

    // A party member who set out to open the door has reached it: the door's
    // OnClick runs first; the usual open / fail-to-open handling follows.
    if (_door->hasClickScript() && _game.party().isMember(actor)) {
        auto door = _door;
        door->runClickScript(actor.id());
        if (!door->isRuntimeLive()) {
            complete();
            return;
        }
    }

    if (_useTransition && actor.type() == ObjectType::Creature) {
        // The walk to an area exit is over: go through it, as the retail
        // click does once the leader stands at the door.
        auto door = _door;
        auto &creature = static_cast<Creature &>(actor);
        complete();
        if (_onArrival) {
            _onArrival(door, creature);
        } else if (auto module = _game.module()) {
            if (auto user = _game.getObjectById<Creature>(creature.id())) {
                module->useTransitionDoorOnArrival(door, user);
            }
        }
        return;
    }

    tryUnlockDoorWithKey(_game, *_door, actor, _game.party());

    if (!_door->isLocked()) {
        _door->open();
        _door->onOpen(actor.id());
    } else {
        _door->onFailToOpen(actor);
    }

    complete();
}

} // namespace game

} // namespace reone
