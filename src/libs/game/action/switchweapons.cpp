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

#include "reone/game/action/switchweapons.h"

#include "reone/game/action/attackobject.h"
#include "reone/game/coopsession.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"

namespace reone {

namespace game {

void SwitchWeaponsAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto *creature = dyn_cast<Creature>(&actor);
    if (!creature || !creature->isRuntimeLive() || creature->isDead() ||
        _game.isConversationActive()) {
        complete();
        return;
    }
    creature->swapWeaponSets();
    complete();

    if (!creature->isInCombat()) {
        return;
    }
    auto target = creature->getAttackTarget();
    if (!target || !target->isRuntimeLive() || target->isDead() || target->id() == creature->id()) {
        return;
    }
    for (const auto &queued : creature->actions()) {
        if (queued.get() != this && queued->type() == ActionType::AttackObject) {
            return;
        }
    }
    auto attack = _game.newAction<AttackObjectAction>(target);
    attack->setUserAction(isUserAction());
    creature->addAction(std::move(attack));
}

void requestWeaponSwitch(Game &game, Creature &creature) {
    if (auto *coop = game.coop(); coop && coop->isClient()) {
        // Co-op: the host swaps the player's creature's weapons.
        coop->submitRequest(net::CommandType::SwitchWeapons, 0, 0);
        return;
    }
    if (!creature.isRuntimeLive() || creature.isDead() || game.isConversationActive()) {
        return;
    }
    auto action = game.newAction<SwitchWeaponsAction>();
    action->setUserAction(true);
    if (!creature.isInCombat()) {
        creature.clearAllActions();
    }
    creature.addAction(std::move(action));
}

} // namespace game

} // namespace reone
