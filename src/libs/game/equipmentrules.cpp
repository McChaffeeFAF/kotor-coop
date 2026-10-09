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

#include "reone/game/equipmentrules.h"

#include "reone/game/game.h"
#include "reone/game/minigame.h"
#include "reone/game/object.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/game/types.h"

namespace reone {

namespace game {

EquipmentCandidateReason getItemUseRestriction(const Creature &creature, const Item &item) {
    if (item.isNonEquippable()) {
        return EquipmentCandidateReason::NonEquippable;
    }
    for (auto feat : item.requiredFeats()) {
        if (!creature.hasEffectiveFeat(feat)) {
            return EquipmentCandidateReason::MissingRequiredFeat;
        }
    }
    bool droid = creature.racialType() == RacialType::Droid;
    if ((item.droidOrHuman() == 1 && droid) || (item.droidOrHuman() == 2 && !droid)) {
        return EquipmentCandidateReason::WrongBodyType;
    }
    int subrace = static_cast<int>(creature.subrace());
    if (subrace > 0 && subrace < 32 && (item.denySubraceMask() & (1u << subrace)) != 0) {
        return EquipmentCandidateReason::SubraceDenied;
    }
    return EquipmentCandidateReason::None;
}

bool isMainHandWeaponSlot(int slot) {
    return slot == InventorySlots::rightWeapon || slot == InventorySlots::rightWeapon2;
}

bool isOffHandWeaponSlot(int slot) {
    return slot == InventorySlots::leftWeapon || slot == InventorySlots::leftWeapon2;
}

int getPairedMainHandSlot(int offHandSlot) {
    return offHandSlot == InventorySlots::leftWeapon2 ? InventorySlots::rightWeapon2 : InventorySlots::rightWeapon;
}

int getPairedOffHandSlot(int mainHandSlot) {
    return mainHandSlot == InventorySlots::rightWeapon2 ? InventorySlots::leftWeapon2 : InventorySlots::leftWeapon;
}

bool isOneHandedWeapon(const Item &item) {
    switch (item.weaponWield()) {
    case WeaponWield::StunBaton:
    case WeaponWield::SingleSword:
    case WeaponWield::BlasterPistol:
        return true;
    default:
        return false;
    }
}

bool isTwoHandedWeapon(const Item &item) {
    switch (item.weaponWield()) {
    case WeaponWield::DoubleBladedSword:
    case WeaponWield::BlasterRifle:
    case WeaponWield::HeavyWeapon:
        return true;
    default:
        return false;
    }
}

bool weaponRequiresEmptyPairedSlot(const Item &item) {
    return isTwoHandedWeapon(item) || item.weaponWield() == WeaponWield::StunBaton;
}

bool areWeaponsCompatible(const Item &mainHand, const Item &offHand) {
    return isOneHandedWeapon(mainHand) &&
           isOneHandedWeapon(offHand) &&
           !weaponRequiresEmptyPairedSlot(mainHand) &&
           !weaponRequiresEmptyPairedSlot(offHand) &&
           mainHand.weaponType() == offHand.weaponType() &&
           mainHand.weaponType() != WeaponType::None;
}

static int getCandidateEquipSlot(int requestedSlot) {
    switch (requestedSlot) {
    case InventorySlots::rightWeapon2:
        return InventorySlots::rightWeapon;
    case InventorySlots::leftWeapon2:
        return InventorySlots::leftWeapon;
    default:
        return requestedSlot;
    }
}

static int resolveActualEquipSlot(int requestedSlot, const Item &item, const Creature &creature) {
    if (!isOffHandWeaponSlot(requestedSlot))
        return requestedSlot;

    int mainHandSlot = getPairedMainHandSlot(requestedSlot);
    if (creature.getEquippedItem(mainHandSlot) || creature.getEquippedItem(requestedSlot))
        return requestedSlot;

    return isOneHandedWeapon(item) && item.isEquippable(getCandidateEquipSlot(mainHandSlot)) ? mainHandSlot : requestedSlot;
}

EquipmentSlotActivationDecision evaluateEquipmentSlotActivation(
    const Creature &creature,
    int requestedSlot) {

    EquipmentSlotActivationDecision result;
    result.requestedSlot = requestedSlot;

    if (!isOffHandWeaponSlot(requestedSlot))
        return result;

    result.pairedSlot = getPairedMainHandSlot(requestedSlot);
    auto mainHand = creature.getEquippedItem(result.pairedSlot);
    if (mainHand && weaponRequiresEmptyPairedSlot(*mainHand)) {
        result.available = false;
        result.reason = EquipmentSlotActivationReason::OffHandBlockedByMainHandWeapon;
    }

    return result;
}

EquipmentCandidateDecision evaluateEquipmentCandidate(
    const Creature &creature,
    int requestedSlot,
    const Item *item) {

    EquipmentCandidateDecision result;
    result.requestedSlot = requestedSlot;
    result.actualSlot = requestedSlot;

    if (!item) {
        result.visible = true;
        result.valid = true;
        if (isMainHandWeaponSlot(requestedSlot)) {
            result.pairedSlot = getPairedOffHandSlot(requestedSlot);
            result.action = EquipmentCandidateAction::ClearMainHandAndOffHand;
        } else {
            result.action = EquipmentCandidateAction::ClearSlot;
        }
        return result;
    }

    if (!item->isEquippable(getCandidateEquipSlot(requestedSlot))) {
        result.action = EquipmentCandidateAction::Reject;
        result.reason = EquipmentCandidateReason::NotEquippableInRequestedSlot;
        return result;
    }

    result.visible = true;
    result.actualSlot = resolveActualEquipSlot(requestedSlot, *item, creature);
    // The equipment screen lists an item the creature cannot use, but will
    // not equip it.
    auto restriction = getItemUseRestriction(creature, *item);
    if (restriction != EquipmentCandidateReason::None) {
        result.action = EquipmentCandidateAction::Reject;
        result.reason = restriction;
        return result;
    }
    result.valid = true;
    result.action = result.actualSlot != requestedSlot ? EquipmentCandidateAction::EquipMainHandFromOffHand : EquipmentCandidateAction::Equip;

    if (isOffHandWeaponSlot(result.actualSlot)) {
        if (isTwoHandedWeapon(*item)) {
            result.valid = false;
            result.action = EquipmentCandidateAction::Reject;
            result.reason = EquipmentCandidateReason::TwoHandedInOffHand;
            return result;
        }
        if (weaponRequiresEmptyPairedSlot(*item)) {
            result.valid = false;
            result.action = EquipmentCandidateAction::Reject;
            result.reason = EquipmentCandidateReason::WeaponRequiresEmptyPairedSlot;
            return result;
        }

        result.pairedSlot = getPairedMainHandSlot(result.actualSlot);
        auto mainHand = creature.getEquippedItem(result.pairedSlot);
        if (!mainHand) {
            result.valid = false;
            result.action = EquipmentCandidateAction::Reject;
            result.reason = EquipmentCandidateReason::OffHandRequiresMainHand;
            return result;
        }

        if (weaponRequiresEmptyPairedSlot(*mainHand)) {
            result.valid = false;
            result.action = EquipmentCandidateAction::Reject;
            result.reason = EquipmentCandidateReason::IncompatibleWithMainHand;
            return result;
        }

        if (!areWeaponsCompatible(*mainHand, *item)) {
            result.valid = false;
            result.action = EquipmentCandidateAction::Reject;
            result.reason = EquipmentCandidateReason::IncompatibleWithMainHand;
        }
        return result;
    }

    if (isMainHandWeaponSlot(result.actualSlot)) {
        result.pairedSlot = getPairedOffHandSlot(result.actualSlot);
        auto offHand = creature.getEquippedItem(result.pairedSlot);
        if (!offHand)
            return result;

        if (weaponRequiresEmptyPairedSlot(*item)) {
            result.action = EquipmentCandidateAction::EquipAndClearOffHand;
            result.reason = EquipmentCandidateReason::MainHandWeaponClearsOffHand;
            return result;
        }

        if (!areWeaponsCompatible(*item, *offHand)) {
            result.valid = false;
            result.action = EquipmentCandidateAction::Reject;
            result.reason = EquipmentCandidateReason::IncompatibleWithOffHand;
        }
    }

    return result;
}

std::shared_ptr<Item> takeEquipmentCandidate(
    Game &game,
    Object &inventory,
    const std::shared_ptr<Item> &item) {

    bool last = false;
    if (!inventory.removeItem(item, last)) {
        return nullptr;
    }
    if (last) {
        return item;
    }

    auto split = game.newItemClone(*item);
    split->setStackSize(1);
    return split;
}

static std::shared_ptr<Area> activeAreaOwningItem(
    Game &game,
    const std::shared_ptr<Item> &item) {
    if (!item) return nullptr;
    auto module = game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) return nullptr;
    auto owned = std::find_if(
        area->objects().begin(), area->objects().end(),
        [&item](const auto &candidate) {
            return candidate.get() == item.get();
        });
    return owned == area->objects().end() ? nullptr : area;
}

bool isActiveAreaOwnedItem(
    Game &game,
    const std::shared_ptr<Item> &item) {
    return static_cast<bool>(activeAreaOwningItem(game, item));
}

bool releaseAreaOwnedItem(
    Game &game,
    const std::shared_ptr<Item> &item) {
    if (!item || !item->isRuntimeLive() || item->isPresentationOnly() ||
        item->isEquipped() ||
        (item->owner() != 0 && item->owner() != script::kObjectInvalid)) {
        return false;
    }
    auto area = activeAreaOwningItem(game, item);
    return area && area->releaseObject(item);
}

bool transferItemTo(
    Game &game,
    const std::shared_ptr<Item> &item,
    Object &receiver) {
    if (!item || (!item->isRuntimeLive() && !item->isPresentationOnly()) ||
        (!receiver.isRuntimeLive() && !receiver.isPresentationOnly()) ||
        (item->isPresentationOnly() != receiver.isPresentationOnly()) ||
        item.get() == &receiver) {
        return false;
    }

    uint32_t ownerId = item->owner();
    if (ownerId == receiver.id() && !item->isEquipped()) return true;

    if (ownerId == 0 || ownerId == script::kObjectInvalid) {
        // owner() describes nested inventory/equipment ownership; it does not
        // encode Area residency. Only an exact active-Area ownership edge makes
        // an ownerless-numbered Item transferable. Everything else fails
        // closed rather than treating owner()==0 as freely claimable.
        if (item->isPresentationOnly() || item->isEquipped() ||
            std::find(receiver.items().begin(), receiver.items().end(), item) !=
                receiver.items().end()) {
            return false;
        }
        if (!releaseAreaOwnedItem(game, item)) {
            return false;
        }
        receiver.addItem(item);
        return true;
    }

    auto owner = game.getObjectById(ownerId);
    if (!owner) return false;

    if (auto creature = std::dynamic_pointer_cast<Creature>(owner);
        creature && item->isEquipped()) {
        if (!creature->takeEquippedItem(item)) return false;
    } else if (!owner->removeItemStack(item)) {
        return false;
    }

    receiver.addItem(item);
    return true;
}

static void signalDisturbed(Object &object, InventoryDisturbType type, uint32_t actorId, uint32_t itemId) {
    if (object.id() == actorId) {
        return;
    }
    if (auto creature = dyn_cast<Creature>(&object)) {
        creature->signalInventoryDisturbed(type, actorId, itemId);
    } else if (auto placeable = dyn_cast<Placeable>(&object)) {
        placeable->runOnInvDisturbed(actorId, type, itemId);
    }
}

bool transferItemAndSignalDisturbed(
    Game &game,
    const std::shared_ptr<Item> &item,
    Object &receiver,
    uint32_t actorId) {
    if (!item) {
        return false;
    }
    uint32_t previousOwnerId = item->owner();
    bool alreadyThere = previousOwnerId == receiver.id() && !item->isEquipped();
    std::string tag = item->tag();
    if (!transferItemTo(game, item, receiver)) {
        return false;
    }
    if (alreadyThere) {
        return true;
    }
    {
        std::shared_ptr<Object> previousOwner;
        if (previousOwnerId != 0 && previousOwnerId != script::kObjectInvalid) {
            previousOwner = game.getObjectById(previousOwnerId);
        }
        raisePartyItemEvents(game, tag, item, previousOwner.get(), &receiver);
    }
    if (previousOwnerId != 0 && previousOwnerId != script::kObjectInvalid &&
        previousOwnerId != receiver.id()) {
        if (auto previousOwner = game.getObjectById(previousOwnerId)) {
            signalDisturbed(*previousOwner, InventoryDisturbType::Removed, actorId, item->id());
        }
    }
    signalDisturbed(receiver, InventoryDisturbType::Added, actorId, item->id());
    return true;
}

uint32_t giveItemToContainer(
    Game &game,
    Creature &giver,
    const std::shared_ptr<Item> &item,
    Object &container) {
    bool last = false;
    if (!item || !giver.removeItem(item, last)) {
        return script::kObjectInvalid;
    }

    // Add item to the container if it does not exist.
    uint32_t itemId = script::kObjectInvalid;
    for (const std::shared_ptr<Item> &containerItem : container.items()) {
        if (containerItem->tag() == item->tag()) {
            container.addItem(containerItem);
            itemId = containerItem->id();
            break;
        }
    }
    if (itemId == script::kObjectInvalid) {
        // No existing item in the container. Clone the item instead of
        // adding it directly.
        std::shared_ptr<Item> newItem = game.newItemClone(*item);
        newItem->setStackSize(1);
        container.addItem(newItem);
        itemId = newItem->id();
    }
    // The item left the party for the container.
    if (auto module = game.module()) {
        if (auto given = game.getObjectById(itemId)) {
            module->raiseItemLost(*given, giver.id());
        }
    }
    if (last) {
        game.destroyRuntimeObjectGraph(item);
    }

    // Execute a script for every item individually, because it only
    // supports a single InventoryDisturbItem.
    if (auto placeable = dyn_cast<Placeable>(&container)) {
        placeable->runOnInvDisturbed(giver.id(), InventoryDisturbType::Added, itemId);
    }
    if (auto creature = dyn_cast<Creature>(&container)) {
        creature->signalInventoryDisturbed(InventoryDisturbType::Added, giver.id(), itemId);
    }
    return itemId;
}

namespace {

// Base item 86 (baseitems.2da "Pazaak_Card"): a side-deck card. Content
// awards cards as items; acquiring one adds it to the party's Pazaak
// collection, and the item does not stay in the inventory.
constexpr int kPazaakCardBaseItem = 86;

} // namespace

bool absorbPazaakCard(Game &game, Object &receiver, const std::shared_ptr<Item> &item) {
    if (!item || item->baseItemType() != kPazaakCardBaseItem) {
        return false;
    }
    int card = pazaakCardIndexForModelVariation(item->modelVariation(), game.isTSL());
    Party &party = game.party();
    size_t cardCount = game.isTSL() ? Party::kK2PazaakCardCount : Party::kK1PazaakCardCount;
    if (card < 0 || static_cast<size_t>(card) >= cardCount) {
        return false;
    }
    Party::PazaakCardCounts counts = party.pazaakCardCounts();
    Party::PazaakSideDeck sideDeck = party.pazaakSideDeck();
    if (!party.hasValidPazaakData()) {
        counts.fill(0);
        sideDeck.fill(-1);
    }
    counts[card] += glm::max(1, item->stackSize());
    party.setPazaakData(counts, sideDeck, cardCount);
    receiver.removeItemStack(item);
    game.destroyRuntimeObjectGraph(item);
    return true;
}

void raisePartyItemEvents(
    Game &game,
    const std::string &tag,
    const std::shared_ptr<Item> &item,
    Object *from,
    Object *to) {
    auto module = game.module();
    if (!module || !item) {
        return;
    }
    const Party &party = game.party();
    bool fromParty = from && party.isMember(*from);
    bool toParty = to && party.isMember(*to);
    if (fromParty == toParty) {
        // Moved within the party, or between objects outside it.
        return;
    }
    // An item that stacked onto another no longer exists; report the stack.
    std::shared_ptr<Item> held = item;
    if (!item->isRuntimeLive() && to) {
        held = to->getItemByTag(tag);
    }
    if (!held) {
        return;
    }
    if (toParty) {
        if (absorbPazaakCard(game, *to, held)) {
            return;
        }
        module->raiseItemAcquired(*held, from ? from->id() : script::kObjectInvalid);
    } else {
        module->raiseItemLost(*held, from->id());
    }
}

} // namespace game

} // namespace reone
