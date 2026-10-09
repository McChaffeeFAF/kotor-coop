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

#include "reone/game/action/useskill.h"

#include "commonactions.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/trigger.h"
#include "reone/game/traps.h"
#include "reone/game/script/runner.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

void UseSkillAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    switch (_skill) {
    case SkillType::Security: {
        if (!_target) {
            warn("ActionExecutor: unsupported Security target: null");
            complete();
            return;
        }
        if (_target->type() == ObjectType::Door) {
            Door &door = static_cast<Door &>(*_target);
            if (unlockDoor(_game, door, actor, _doorApproach, kDefaultMaxObjectDistance, dt)) {
                complete();
            }
            return;
        }
        if (_target->type() == ObjectType::Placeable) {
            auto &placeable = static_cast<Placeable &>(*_target);
            if (unlockPlaceable(placeable, actor, kDefaultMaxObjectDistance, dt)) {
                if (placeable.hasInventory() && !placeable.isLocked()) {
                    _game.openContainer(_target);
                }
                complete();
            }
            return;
        }

        warn("ActionExecutor: unsupported Security target: " + std::to_string(_target->id()));
        complete();
        return;
    }
    case SkillType::Demolitions: {
        // On a trap: disarm, or the trap subskills (design note c11_data).
        auto creature = dyn_cast<Creature>(&actor);
        auto trap = dyn_cast<Trigger>(_target.get());
        if (!creature || !trap) {
            complete();
            return;
        }
        TrapSkill skill = TrapSkill::Disarm;
        if (_subSkill == static_cast<int>(SubSkill::FlagTrap)) {
            skill = TrapSkill::Flag;
        } else if (_subSkill == static_cast<int>(SubSkill::RecoverTrap)) {
            skill = TrapSkill::Recover;
        } else if (_subSkill == static_cast<int>(SubSkill::ExamineTrap)) {
            skill = TrapSkill::Examine;
        }
        if (performTrapSkill(_game, _services, *creature, *trap, skill, dt)) {
            complete();
        }
        return;
    }
    default:
        warn("ActionExecutor: unsupported UseSkillAction");
        complete();
    }
}

} // namespace game

} // namespace reone
