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

#include "reone/system/portablemath.h"
#include "reone/game/action/moveawayfromobject.h"

#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"

namespace reone {

namespace game {

// The flee point is fMoveAwayRange from the object, along the line from the
// object through the actor. When that point is off the walkmesh, directions
// rotated away from it are tried, widest last.
static constexpr float kFleeAngles[] = {0.0f, 30.0f, -30.0f, 60.0f, -60.0f, 90.0f, -90.0f, 120.0f, -120.0f, 180.0f};
// Re-plan when the object has moved this far since the point was chosen.
static constexpr float kReplanDistance = 2.0f;
static constexpr float kArrivalDistance = 1.0f;

std::optional<glm::vec3> MoveAwayFromObject::chooseDestination(const Object &actor) const {
    glm::vec3 from(_fleeFrom->position());
    glm::vec2 away(actor.position().x - from.x, actor.position().y - from.y);
    if (glm::length(away) < 1e-3f) {
        float facing = actor.getFacing();
        away = glm::vec2(portable::sinf(facing), -portable::cosf(facing));
    }
    away = glm::normalize(away);

    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    for (float degrees : kFleeAngles) {
        float angle = glm::radians(degrees);
        glm::vec2 dir(away.x * portable::cosf(angle) - away.y * portable::sinf(angle),
                      away.x * portable::sinf(angle) + away.y * portable::cosf(angle));
        glm::vec3 candidate(from.x + dir.x * _moveAwayRange, from.y + dir.y * _moveAwayRange, actor.position().z);
        if (!area) {
            return candidate;
        }
        if (auto landed = area->walkablePointAt(candidate)) {
            return landed;
        }
    }
    return std::nullopt;
}

void MoveAwayFromObject::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto creature = _game.getObjectById<Creature>(actor.id());
    if (!creature || !_fleeFrom) {
        complete();
        return;
    }
    if (!_started) {
        _started = true;
        // An object outside the actor's area: nothing happens (nwscript.nss).
        auto module = _game.module();
        auto area = module ? module->area() : nullptr;
        if (area) {
            const auto &objects = area->objects();
            bool present = std::any_of(objects.begin(), objects.end(),
                                       [this](const std::shared_ptr<Object> &object) { return object == _fleeFrom; });
            if (!present) {
                complete();
                return;
            }
        }
    }

    float distance = glm::distance(glm::vec2(creature->position()), glm::vec2(_fleeFrom->position()));
    if (distance >= _moveAwayRange) {
        creature->setMovementType(Creature::MovementType::None);
        creature->clearPath();
        complete();
        return;
    }
    if (!_destination ||
        glm::distance(glm::vec2(_fleeFrom->position()), glm::vec2(_fleeOrigin)) > kReplanDistance) {
        _destination = chooseDestination(*creature);
        _fleeOrigin = _fleeFrom->position();
        if (!_destination) {
            complete();
            return;
        }
    }
    bool reached = creature->navigateTo(*_destination, _run, kArrivalDistance, dt);
    if (reached || creature->navigationFailed()) {
        complete();
    }
}

void MoveAwayFromObject::cancel(std::shared_ptr<Action> self, Object &actor) {
    if (auto *creature = dyn_cast<Creature>(&actor)) {
        creature->clearPath();
    }
}

} // namespace game

} // namespace reone
