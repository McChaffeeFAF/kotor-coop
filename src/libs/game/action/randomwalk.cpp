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
#include "reone/game/action/randomwalk.h"

#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/system/randomutil.h"

namespace reone {

namespace game {

// Tries per leg to find a destination on the walkmesh, and legs in a row
// that may fail before the walk gives up.
static constexpr int kMaxPicks = 8;
static constexpr int kMaxFailedLegs = 8;
static constexpr float kMinLeg = 1.0f;
static constexpr float kArrivalDistance = 0.5f;

std::optional<glm::vec3> RandomWalkAction::chooseDestination() {
    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    for (int attempt = 0; attempt < kMaxPicks; ++attempt) {
        // Gameplay stream: this runs in the simulation tick.
        float angle = randomFloat(0.0f, 6.28318531f);
        float distance = randomFloat(kMinLeg, kRadius);
        glm::vec3 candidate(*_anchor + glm::vec3(portable::cosf(angle) * distance, portable::sinf(angle) * distance, 0.0f));
        if (!area) {
            return candidate;
        }
        if (auto landed = area->walkablePointAt(candidate)) {
            return landed;
        }
    }
    return std::nullopt;
}

void RandomWalkAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // Walks from random point to random point near where it began, until
    // another command removes it from the queue.
    auto creature = _game.getObjectById<Creature>(actor.id());
    if (!creature) {
        complete();
        return;
    }
    if (!_anchor) {
        _anchor = creature->position();
    }
    if (!_destination) {
        _destination = chooseDestination();
        if (!_destination) {
            complete();
            return;
        }
    }
    bool reached = creature->navigateTo(*_destination, /*run=*/false, kArrivalDistance, dt);
    if (reached) {
        _destination.reset();
        _failedLegs = 0;
    } else if (creature->navigationFailed()) {
        _destination.reset();
        if (++_failedLegs >= kMaxFailedLegs) {
            creature->setMovementType(Creature::MovementType::None);
            complete();
        }
    }
}

void RandomWalkAction::cancel(std::shared_ptr<Action> self, Object &actor) {
    if (auto *creature = dyn_cast<Creature>(&actor)) {
        creature->clearPath();
        creature->setMovementType(Creature::MovementType::None);
    }
}

} // namespace game

} // namespace reone
