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

#include "reone/game/effect/factionmodifier.h"
#include "reone/game/object/creature.h"

namespace reone {

namespace game {

// K2. The creature belongs to another faction while the effect lasts
// (Creature::refreshFactionModifier).
bool FactionModifierEffect::onApply(Object &object, const EffectInstance &) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature || _newFaction <= static_cast<int>(Faction::Invalid) ||
        _newFaction >= static_cast<int>(Faction::Last)) {
        return false;
    }
    creature->refreshFactionModifier();
    return true;
}

void FactionModifierEffect::onRemove(Object &object, const EffectInstance &) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->refreshFactionModifier();
    }
}

} // namespace game

} // namespace reone
