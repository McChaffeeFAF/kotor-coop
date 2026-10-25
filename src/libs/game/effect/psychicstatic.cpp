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

#include "reone/game/effect/psychicstatic.h"
#include "reone/game/combatfeedback.h"
#include "reone/game/object/creature.h"

namespace reone {

namespace game {

// A confusion effect (Creature::isConfused). The per-round choice it makes
// (act normally, attack the nearest ally, or do nothing) belongs to the
// creature's combat round and is not wired yet.
bool PsychicStaticEffect::onApply(Object &object, const EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature || creature->isDead()) {
        return false;
    }
    auto creator = instance.boundCreator();
    if (creature->hasEffectImmunity(
            ImmunityType::Confused,
            creator ? dyn_cast<Creature>(creator.get()) : nullptr)) {
        addEffectImmunityFeedback(creature->game(), creator.get(), *creature, ImmunityType::Confused);
        return false;
    }
    return true;
}

} // namespace game

} // namespace reone
