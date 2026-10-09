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

#include "reone/game/action/interactobject.h"

#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/placeable.h"

namespace reone {

namespace game {

void InteractObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // Walk up to the placeable, then use it: its OnUsed runs with the actor
    // as the user, as a click by the party leader does.
    // K2 behaviour: the user stands within its own body radius + 0.75 m.
    // Ours: measured to the placeable's origin, not to its use points.
    static constexpr float kUseReach = 0.75f;
    if (!_placeable || !_placeable->isRuntimeLive()) {
        complete();
        return;
    }
    if (auto *creature = dyn_cast<Creature>(&actor)) {
        float useDistance = creature->creaturePersonalSpace() + kUseReach;
        if (!creature->navigateTo(_placeable->position(), false, useDistance, dt)) {
            if (!creature->navigationFailed()) {
                return;
            }
            // The walk can get no closer. Wall-mounted placeables (tar_m09aa
            // wall1-5) have their origin beyond the walkmesh, where retail's
            // use points are not: use it from here when close, else drop the
            // order instead of holding the queue forever.
            constexpr float kUnreachableUseDistance = 4.0f;
            creature->setMovementType(Creature::MovementType::None);
            if (creature->getSquareDistanceTo(glm::vec2(_placeable->position())) >
                kUnreachableUseDistance * kUnreachableUseDistance) {
                complete();
                return;
            }
        }
        creature->face(*_placeable);
    }
    complete();
    _placeable->runOnUsed(_game.getObjectById(actor.id()));
}

} // namespace game

} // namespace reone
