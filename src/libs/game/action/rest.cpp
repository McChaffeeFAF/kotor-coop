/*
 * Copyright (c) 2026 The reone project contributors
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

#include "reone/game/action/rest.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"

namespace reone {

namespace game {

// No rest with a living hostile creature this close.
static constexpr float kRestHostileRange = 30.0f;

void RestAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto creature = dyn_cast<Creature>(&actor);
    auto module = _game.module();
    if (!creature || creature->isDead() || !module || !module->area()) {
        complete();
        return;
    }
    const float range2 = kRestHostileRange * kRestHostileRange;
    for (const auto &object : module->area()->getObjectsByType(ObjectType::Creature)) {
        auto other = std::static_pointer_cast<Creature>(object);
        if (other.get() == creature || other->isDead()) {
            continue;
        }
        if (creature->getSquareDistanceTo(*other) <= range2 &&
            _services.game.reputes.getIsEnemy(*other, *creature)) {
            complete();
            return;
        }
    }
    creature->finishRest();
    if (_game.party().isMember(*creature)) {
        module->raisePlayerRest(*creature);
    }
    complete();
}

} // namespace game

} // namespace reone
