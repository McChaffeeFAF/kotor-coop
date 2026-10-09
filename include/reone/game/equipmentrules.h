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

#pragma once

#include <cstdint>
#include <memory>

namespace reone {

namespace game {

class Creature;
class Game;
class Item;
class Object;

enum class EquipmentCandidateAction {
    None,
    Equip,
    EquipMainHandFromOffHand,
    ClearSlot,
    ClearMainHandAndOffHand,
    EquipAndClearOffHand,
    Reject
};

enum class EquipmentCandidateReason {
    None,
    NotEquippableInRequestedSlot,
    OffHandRequiresMainHand,
    TwoHandedInOffHand,
    WeaponRequiresEmptyPairedSlot,
    IncompatibleWithMainHand,
    IncompatibleWithOffHand,
    MainHandWeaponClearsOffHand,
    MissingRequiredFeat,
    WrongBodyType,
    SubraceDenied,
    // SetItemNonEquippable
    NonEquippable
};

struct EquipmentCandidateDecision {
    bool visible {false};
    bool valid {false};
    int requestedSlot {-1};
    int actualSlot {-1};
    int pairedSlot {-1};
    EquipmentCandidateAction action {EquipmentCandidateAction::None};
    EquipmentCandidateReason reason {EquipmentCandidateReason::None};
};

enum class EquipmentSlotActivationReason {
    None,
    OffHandBlockedByMainHandWeapon
};

struct EquipmentSlotActivationDecision {
    bool available {true};
    int requestedSlot {-1};
    int pairedSlot {-1};
    EquipmentSlotActivationReason reason {EquipmentSlotActivationReason::None};
};

/**
 * baseitems.2da use limits: every reqfeat the item lists, droidorhuman
 * (1 non-droids only, 2 droids only) and the denysubrace mask.
 */
EquipmentCandidateReason getItemUseRestriction(const Creature &creature, const Item &item);

bool isMainHandWeaponSlot(int slot);
bool isOffHandWeaponSlot(int slot);
int getPairedMainHandSlot(int offHandSlot);
int getPairedOffHandSlot(int mainHandSlot);

bool isOneHandedWeapon(const Item &item);
bool isTwoHandedWeapon(const Item &item);
bool weaponRequiresEmptyPairedSlot(const Item &item);
bool areWeaponsCompatible(const Item &mainHand, const Item &offHand);

EquipmentCandidateDecision evaluateEquipmentCandidate(
    const Creature &creature,
    int requestedSlot,
    const Item *item);

EquipmentSlotActivationDecision evaluateEquipmentSlotActivation(
    const Creature &creature,
    int requestedSlot);

std::shared_ptr<Item> takeEquipmentCandidate(
    Game &game,
    Object &inventory,
    const std::shared_ptr<Item> &item);

/** True only when the exact Item is owned by the active Area graph. */
bool isActiveAreaOwnedItem(
    Game &game,
    const std::shared_ptr<Item> &item);

/** End exact active-Area ownership without retiring the runtime Item. */
bool releaseAreaOwnedItem(
    Game &game,
    const std::shared_ptr<Item> &item);

/**
 * Move one complete runtime Item from its current nested or Area ownership
 * edge to receiver. An ownerless nonresident Item is not transferable.
 */
bool transferItemTo(
    Game &game,
    const std::shared_ptr<Item> &item,
    Object &receiver);

/**
 * transferItemTo on behalf of actorId, raising the inventory-disturbed event
 * on each party to the move that is not the actor: REMOVED on the object the
 * item left, ADDED on the receiver. Returns what transferItemTo returns.
 */
bool transferItemAndSignalDisturbed(
    Game &game,
    const std::shared_ptr<Item> &item,
    Object &receiver,
    uint32_t actorId);

/**
 * The retail container "give": one item (one from a stack) leaves giver's
 * inventory for the container's, merging into a stack there with the same tag
 * or becoming a one-item clone. Raises the module's item-lost event and the
 * container's inventory-disturbed event (ADDED, disturber giver). Returns the
 * id of the item now in the container, or kObjectInvalid when giver does not
 * hold the item.
 */
uint32_t giveItemToContainer(
    Game &game,
    Creature &giver,
    const std::shared_ptr<Item> &item,
    Object &container);

/**
 * The module's item events for an item that moved during play from `from`
 * to `to` (either may be null): item-acquired when it came into a party
 * member's hands from outside the party, item-lost when it left the party.
 * tag finds the stack the item merged into when it no longer exists.
 */
void raisePartyItemEvents(
    Game &game,
    const std::string &tag,
    const std::shared_ptr<Item> &item,
    Object *from,
    Object *to);

} // namespace game

} // namespace reone
