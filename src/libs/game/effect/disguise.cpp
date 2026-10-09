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

#include "reone/game/effect/disguise.h"

#include "reone/game/object/creature.h"

namespace reone {

namespace game {

bool DisguiseEffect::onApply(Object &object, const EffectInstance &) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature || _appearance < 0) {
        return false;
    }
    creature->refreshDisguise();
    return true;
}

void DisguiseEffect::onRemove(Object &object, const EffectInstance &) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->refreshDisguise();
    }
}

} // namespace game

} // namespace reone
