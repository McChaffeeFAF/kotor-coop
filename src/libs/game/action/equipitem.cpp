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

#include "reone/game/action/equipitem.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"

namespace reone {

namespace game {

void EquipItemAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto *creature = dyn_cast<Creature>(&actor);
    if (!creature) {
        complete();
        return;
    }
    auto candidate = _item;
    int equipabilitySlot = _inventorySlot;
    if (_inventorySlot == InventorySlots::rightWeapon2) {
        equipabilitySlot = InventorySlots::rightWeapon;
    } else if (_inventorySlot == InventorySlots::leftWeapon2) {
        equipabilitySlot = InventorySlots::leftWeapon;
    }
    if (!creature->isRuntimeLive() || !candidate ||
        !candidate->isRuntimeLive() ||
        !candidate->isEquippable(equipabilitySlot) ||
        (candidate->isNonEquippable() &&
         creature->getEquippedItem(_inventorySlot) != candidate)) {
        complete();
        return;
    }
    for (const auto &[slot, equipped] : creature->equipment()) {
        if (equipped == candidate && slot != _inventorySlot) {
            complete();
            return;
        }
    }

    auto actorObject = _game.getObjectById(actor.id());
    auto displacedReceiver =
        _game.party().sharedInventoryReceiver(actorObject);
    auto previous = creature->getEquippedItem(_inventorySlot);
    if (previous && previous != candidate && !displacedReceiver) {
        complete();
        return;
    }

    // A two-handed weapon (or a stun baton) put in a main hand takes the
    // off hand's weapon off, as the equipment screen does
    // (EquipAndClearOffHand). The harness's equipslot and a script's
    // ActionEquipItem otherwise left a rifle with a pistol in the off hand,
    // and the creature fought with both.
    // Nothing goes in the off hand beside a two-handed main weapon (the
    // equipment screen refuses it: IncompatibleWithMainHand).
    if (isOffHandWeaponSlot(_inventorySlot)) {
        auto mainHand = creature->getEquippedItem(getPairedMainHandSlot(_inventorySlot));
        if (mainHand && mainHand != candidate && weaponRequiresEmptyPairedSlot(*mainHand)) {
            complete();
            return;
        }
    }
    std::shared_ptr<Item> offHandToClear;
    if (isMainHandWeaponSlot(_inventorySlot) && weaponRequiresEmptyPairedSlot(*candidate)) {
        auto offHand = creature->getEquippedItem(getPairedOffHandSlot(_inventorySlot));
        if (offHand && offHand != candidate) {
            if (!displacedReceiver) {
                complete();
                return;
            }
            offHandToClear = offHand;
        }
    }

    std::shared_ptr<Object> sourceOwner;
    uint32_t ownerId = candidate->owner();
    bool areaOwned = isActiveAreaOwnedItem(_game, candidate);
    if (areaOwned && ownerId != 0 && ownerId != script::kObjectInvalid) {
        complete();
        return;
    }
    if (ownerId != 0 && ownerId != script::kObjectInvalid) {
        sourceOwner = _game.getObjectById(ownerId);
        if (!sourceOwner) {
            complete();
            return;
        }
        if (auto equippedOwner =
                std::dynamic_pointer_cast<Creature>(sourceOwner);
            equippedOwner && candidate->isEquipped()) {
            candidate = equippedOwner->takeEquippedItem(candidate);
        } else {
            candidate = takeEquipmentCandidate(_game, *sourceOwner, candidate);
        }
    } else {
        bool ready = previous && previous != candidate
                         ? displacedReceiver &&
                               creature->canReplaceEquipment(
                                   _inventorySlot, candidate, *displacedReceiver)
                         : creature->canEquip(_inventorySlot, candidate);
        if (!ready || !areaOwned ||
            !releaseAreaOwnedItem(_game, candidate)) {
            complete();
            return;
        }
    }
    bool equipped = candidate &&
                    (previous && previous != candidate
                         ? creature->replaceEquipment(
                               _inventorySlot, candidate, *displacedReceiver)
                         : creature->equip(_inventorySlot, candidate));
    if (!equipped) {
        if (candidate && candidate->owner() == 0 && sourceOwner) {
            sourceOwner->addItem(candidate);
        }
        complete();
        return;
    }
    if (offHandToClear && creature->getEquippedItem(getPairedOffHandSlot(_inventorySlot)) == offHandToClear) {
        creature->moveEquippedItemTo(offHandToClear, *displacedReceiver);
    }
    creature->playAnimation(CombatAnimation::Draw, creature->getWieldType());
    complete();
}

} // namespace game

} // namespace reone
