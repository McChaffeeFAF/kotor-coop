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

#include "reone/game/action/opencontainer.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/placeable.h"

namespace reone {

namespace game {

void OpenContainerAction::openAt(Object &actor) {
    if (_onReached) {
        _onReached(_object);
        return;
    }
    auto container = _object;
    _game.openContainer(container);
    // Retail: the container window opens, then the container's OnUsed fires
    // with the opener as the user (design note engine_spec, body bags). K1 containers
    // carry OnUsed scripts that must run on the click: the Tusken bins'
    // k_ptat_tuskcont, manm27aa's BroknDrd journal entry.
    if (auto placeable = std::dynamic_pointer_cast<Placeable>(container)) {
        placeable->runOnUsed(_game.getObjectById(actor.id()));
    }
}

void OpenContainerAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // A body that decayed into a body bag is gone; the click is void.
    if (!_object || !_object->isRuntimeLive() || !isa<Creature>(actor)) {
        complete();
        return;
    }
    if (auto *placeable = dyn_cast<Placeable>(_object.get())) {
        if (placeable->isLocked()) {
            complete();
            return;
        }
    }

    auto &creatureActor = cast<Creature>(actor);
    bool reached = creatureActor.navigateTo(_object->position(), true, kDefaultMaxObjectDistance, dt);
    if (reached) {
        openAt(actor);
        complete();
        return;
    }
    if (creatureActor.navigationFailed()) {
        // No path to the container (e.g. remains that came to rest off the
        // walkmesh). Loot it if it is within reach; otherwise give up rather
        // than blocking the action queue forever.
        constexpr float kUnreachableLootDistance = 4.0f;
        if (creatureActor.getSquareDistanceTo(glm::vec2(_object->position())) <=
            kUnreachableLootDistance * kUnreachableLootDistance) {
            openAt(actor);
        }
        creatureActor.setMovementType(Creature::MovementType::None);
        complete();
    }
}

} // namespace game

} // namespace reone
