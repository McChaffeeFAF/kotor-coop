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

#pragma once

#include <functional>

#include "../action.h"
#include "../object/door.h"

namespace reone {

namespace game {

class Door;

class OpenDoorAction : public Action {
public:
    OpenDoorAction(Game &game,
                   ServicesView &services,
                   std::shared_ptr<Door> door) :
        Action(game, services, ActionType::OpenDoor),
        _door(std::move(door)) {
        requireRuntimeObject(_door);
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::OpenDoor;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;

    const std::shared_ptr<Door> &door() const { return _door; }

    /**
     * A click on an area exit: on arrival the creature uses the door's
     * transition (or its fail-to-open) instead of just opening it.
     */
    void setUseTransition(bool val) { _useTransition = val; }
    bool usesTransition() const { return _useTransition; }

    /**
     * Co-op host: a client's creature reached the exit. Replaces the local
     * transition use, so the session can run the client's ready check.
     */
    using ArrivalHandler = std::function<void(const std::shared_ptr<Door> &door, Creature &creature)>;
    void setOnArrival(ArrivalHandler handler) { _onArrival = std::move(handler); }

private:
    bool _doorApproach {false};
    bool _useTransition {false};
    ArrivalHandler _onArrival;
    std::shared_ptr<Door> _door;
};

} // namespace game

} // namespace reone
