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

class RandomWalkAction : public Action {
public:
    RandomWalkAction(Game &game, ServicesView &services) :
        Action(game, services, ActionType::RandomWalk) {
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::RandomWalk;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    void cancel(std::shared_ptr<Action> self, Object &actor) override;

    // Heartbeat and spawn scripts re-issue random walks, so saves skip them.
    // Without this a module transition failed while a creature was wandering.
    bool isEphemeral() const override { return true; }

    /** Destinations stay within this distance of where the walk began. */
    static constexpr float kRadius = 5.0f;

    const std::optional<glm::vec3> &anchor() const { return _anchor; }
    const std::optional<glm::vec3> &destination() const { return _destination; }

private:
    std::optional<glm::vec3> _anchor;
    std::optional<glm::vec3> _destination;
    int _failedLegs {0};

    std::optional<glm::vec3> chooseDestination();
};

} // namespace game

} // namespace reone
