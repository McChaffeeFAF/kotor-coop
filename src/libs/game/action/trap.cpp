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

#include "reone/game/action/trap.h"

#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/trigger.h"

namespace reone {

namespace game {

TrapSkill TrapSkillAction::skillFor(ActionType type) {
    switch (type) {
    case ActionType::RecoverTrap:
        return TrapSkill::Recover;
    case ActionType::FlagTrap:
        return TrapSkill::Flag;
    case ActionType::ExamineTrap:
        return TrapSkill::Examine;
    default:
        return TrapSkill::Disarm;
    }
}

void TrapSkillAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto creature = dyn_cast<Creature>(&actor);
    auto trap = dyn_cast<Trigger>(_trap.get());
    if (!creature || !trap) {
        complete();
        return;
    }
    if (performTrapSkill(_game, _services, *creature, *trap, skillFor(type()), dt)) {
        complete();
    }
}

void SetTrapAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto creature = dyn_cast<Creature>(&actor);
    if (!creature || !_mine) {
        complete();
        return;
    }
    if (!_reached) {
        if (!creature->navigateTo(_position, true, kTrapApproachDistance, dt)) {
            return;
        }
        _reached = true;
        creature->face(_position);
    }
    // The set-mine animation takes 2 s before the mine is laid.
    _setTime += dt;
    if (_setTime < kSetMineDuration) {
        return;
    }
    setMine(_game, _services, *creature, *_mine, _position);
    complete();
}

} // namespace game

} // namespace reone
