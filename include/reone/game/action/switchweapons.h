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

class Creature;

/**
 * Switch between the two weapon sets (ActionSwitchWeapons).
 *
 * The items in the main- and off-hand slots trade places with the second
 * set's. Afterwards, a creature that is in combat with a live attack target
 * goes back to attacking it. Queued from script, it waits its turn like any
 * other action; it does nothing while a conversation runs.
 */
class SwitchWeaponsAction : public Action {
public:
    SwitchWeaponsAction(Game &game, ServicesView &services) :
        Action(game, services, ActionType::SwitchWeapons) {
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::SwitchWeapons;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
};

/**
 * The player's swap request (the HUD's swap-weapons button).
 *
 * Out of combat the creature drops what it was doing and switches at once. In
 * combat the switch is queued behind the actions already queued, so the round
 * in progress finishes first. Ignored during a conversation.
 */
void requestWeaponSwitch(Game &game, Creature &creature);

} // namespace game

} // namespace reone
