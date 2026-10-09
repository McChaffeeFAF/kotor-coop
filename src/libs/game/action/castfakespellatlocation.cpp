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

#include "reone/game/action/castfakespellatlocation.h"

#include "reone/game/d20/spells.h"
#include "reone/game/di/services.h"
#include "reone/game/object/creature.h"

namespace reone {

namespace game {

void CastFakeSpellAtLocationAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // Conjure and cast presentation only (nwscript: "nothing else").
    auto *caster = dyn_cast<Creature>(&actor);
    if (!caster || !_target) {
        complete();
        return;
    }
    auto spell = _services.game.spells.get(_spell);
    if (_cast.update(*caster, spell.get(), _target->position(), _services, dt)) {
        complete();
    }
}

void CastFakeSpellAtLocationAction::cancel(std::shared_ptr<Action> self, Object &actor) {
    if (auto *caster = dyn_cast<Creature>(&actor)) {
        _cast.cancel(*caster);
    }
}

} // namespace game

} // namespace reone
