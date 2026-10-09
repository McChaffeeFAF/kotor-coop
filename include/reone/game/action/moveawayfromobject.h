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

#include "../action.h"

namespace reone {

namespace game {

class MoveAwayFromObject : public Action {
public:
    MoveAwayFromObject(Game &game,
                       ServicesView &services,
                       std::shared_ptr<Object> fleeFrom,
                       bool run,
                       float moveAwayRange) :
        Action(game, services, ActionType::MoveAwayFromObject),
        _fleeFrom(std::move(fleeFrom)),
        _run(run),
        _moveAwayRange(moveAwayRange) {
        requireRuntimeObject(_fleeFrom);
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::MoveAwayFromObject;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    void cancel(std::shared_ptr<Action> self, Object &actor) override;

    /** The flee point, once chosen. */
    const std::optional<glm::vec3> &destination() const { return _destination; }

private:
    std::shared_ptr<Object> _fleeFrom;
    bool _run;
    float _moveAwayRange;
    bool _started {false};
    std::optional<glm::vec3> _destination;
    glm::vec3 _fleeOrigin {0.0f};

    std::optional<glm::vec3> chooseDestination(const Object &actor) const;
};

} // namespace game

} // namespace reone
