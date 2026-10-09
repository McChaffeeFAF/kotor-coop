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

#include "reone/game/action/equipmostdamagingmelee.h"

#include "reone/game/action/equipitem.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/weaponchoice.h"

namespace reone {

namespace game {

void EquipMostDamagingMeleeAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // Pick the most damaging melee weapon the creature owns (else the most
    // damaging of the other kind) and hand it to the ordinary equip path.
    // _versus is accepted but the ranking is not per-target.
    auto *creature = dyn_cast<Creature>(&actor);
    if (!creature) {
        complete();
        return;
    }
    auto choice = chooseMostDamagingWeapon(*creature, WeaponKind::Melee, _offHand);
    if (!choice || creature->getEquippedItem(choice->slot) == choice->item) {
        complete();
        return;
    }
    auto equip = _game.newAction<EquipItemAction>(choice->item, choice->slot, false);
    equip->execute(equip, actor, dt);
    complete();
}

} // namespace game

} // namespace reone
