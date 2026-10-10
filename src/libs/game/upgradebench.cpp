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

#include "reone/game/upgradebench.h"

#include <boost/algorithm/string.hpp>

#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/di/services.h"
#include "reone/resource/gff.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"

namespace reone {

namespace game {

// Power crystal slots on a lightsaber, after its colour crystal slot.
static constexpr int kLightsaberPowerSlots = 2;
// Upgrade type of lightsaber crystals (upgradetypes.2da row 0).
static constexpr int kCrystalUpgradeType = 0;
// _consumed key of a staged colour crystal.
static constexpr int kColorCrystalKey = -2;

namespace {

// The K1 colour crystals and the lightsaber model variation (blade colour)
// each gives; the saber templates are made in these colours (01 blue,
// 02 red, 03 green, 04 yellow, 05 violet).
struct ColorCrystal {
    const char *templateResRef;
    int modelVariation;
};

constexpr ColorCrystal kColorCrystals[] {
    {"g_w_sbrcrstl14", 1}, // blue
    {"g_w_sbrcrstl18", 2}, // red
    {"g_w_sbrcrstl16", 3}, // green
    {"g_w_sbrcrstl15", 4}, // yellow
    {"g_w_sbrcrstl17", 5}, // violet
};

uint32_t rowBit(int row) {
    return (row >= 0 && row < Item::kMaxUpgradeRows) ? (1u << row) : 0u;
}

} // namespace

UpgradeTable::UpgradeTable(const resource::TwoDA &upgrades) {
    for (int row = 0; row < upgrades.getRowCount() && row < Item::kMaxUpgradeRows; ++row) {
        UpgradeKind kind;
        kind.row = row;
        kind.templateResRef = boost::to_lower_copy(upgrades.getString(row, "template"));
        kind.type = upgrades.getInt(row, "upgradetype", -1);
        _kinds.push_back(std::move(kind));
    }
}

const UpgradeKind *UpgradeTable::kind(int row) const {
    for (const auto &kind : _kinds) {
        if (kind.row == row) {
            return &kind;
        }
    }
    return nullptr;
}

const UpgradeKind *UpgradeTable::kindForTemplate(const std::string &resRef) const {
    auto lower = boost::to_lower_copy(resRef);
    for (const auto &kind : _kinds) {
        if (!kind.templateResRef.empty() && kind.templateResRef == lower) {
            return &kind;
        }
    }
    return nullptr;
}

UpgradeCategory upgradeCategoryOf(const Item &item) {
    if (!item.isUpgradeable()) {
        return UpgradeCategory::None;
    }
    if (item.isLightsaber()) {
        return UpgradeCategory::Lightsaber;
    }
    if (item.weaponType() == WeaponType::Ranged) {
        return UpgradeCategory::Ranged;
    }
    if (item.weaponType() == WeaponType::Melee) {
        return UpgradeCategory::Melee;
    }
    if (item.isEquippable(InventorySlots::body)) {
        return UpgradeCategory::Armor;
    }
    return UpgradeCategory::None;
}

int colorCrystalModelVariation(const std::string &templateResRef) {
    auto lower = boost::to_lower_copy(templateResRef);
    for (const auto &crystal : kColorCrystals) {
        if (lower == crystal.templateResRef) {
            return crystal.modelVariation;
        }
    }
    return -1;
}

std::string colorCrystalForModelVariation(int modelVariation) {
    for (const auto &crystal : kColorCrystals) {
        if (crystal.modelVariation == modelVariation) {
            return crystal.templateResRef;
        }
    }
    return "";
}

ColorCrystalTable::ColorCrystalTable(
    const resource::TwoDA &upcrystals,
    const std::function<int(const std::string &)> &templateModelVariation) {
    auto variationOf = [&](int row, const char *column) {
        auto resRef = boost::to_lower_copy(upcrystals.getString(row, column));
        return resRef.empty() ? -1 : templateModelVariation(resRef);
    };
    for (int row = 0; row < upcrystals.getRowCount(); ++row) {
        Crystal crystal;
        crystal.templateResRef = boost::to_lower_copy(upcrystals.getString(row, "template"));
        if (crystal.templateResRef.empty()) {
            continue;
        }
        crystal.shortVariation = variationOf(row, "shortmdlvar");
        crystal.longVariation = variationOf(row, "longmdlvar");
        crystal.doubleVariation = variationOf(row, "doublemdlvar");
        _crystals.push_back(std::move(crystal));
    }
}

// Base items 8, 9 and 10 are the long, double and short lightsaber.
int ColorCrystalTable::variationFor(const Crystal &crystal, int saberBaseItem) {
    switch (saberBaseItem) {
    case 9:
        return crystal.doubleVariation;
    case 10:
        return crystal.shortVariation;
    default:
        return crystal.longVariation;
    }
}

int ColorCrystalTable::modelVariation(const std::string &crystalResRef, int saberBaseItem) const {
    if (_crystals.empty()) {
        return colorCrystalModelVariation(crystalResRef);
    }
    auto lower = boost::to_lower_copy(crystalResRef);
    for (const auto &crystal : _crystals) {
        if (crystal.templateResRef == lower) {
            return variationFor(crystal, saberBaseItem);
        }
    }
    return -1;
}

std::string ColorCrystalTable::crystalFor(int modelVariation, int saberBaseItem) const {
    if (_crystals.empty()) {
        return colorCrystalForModelVariation(modelVariation);
    }
    if (modelVariation < 0) {
        return "";
    }
    for (const auto &crystal : _crystals) {
        if (variationFor(crystal, saberBaseItem) == modelVariation) {
            return crystal.templateResRef;
        }
    }
    return "";
}

std::vector<UpgradeSlot> upgradeSlotsFor(const Item &item, uint32_t installed, const UpgradeTable &table) {
    std::vector<UpgradeSlot> slots;
    auto rows = item.upgradeRows();
    auto installedOfType = [&](int type) {
        std::vector<int> result;
        for (int row : rows) {
            const UpgradeKind *kind = table.kind(row);
            if (kind && kind->type == type && (installed & rowBit(row)) != 0) {
                result.push_back(row);
            }
        }
        return result;
    };

    if (item.isLightsaber()) {
        UpgradeSlot color;
        color.type = kCrystalUpgradeType;
        color.color = true;
        slots.push_back(color);
        auto crystals = installedOfType(kCrystalUpgradeType);
        for (int i = 0; i < kLightsaberPowerSlots; ++i) {
            UpgradeSlot power;
            power.type = kCrystalUpgradeType;
            power.installedRow = i < static_cast<int>(crystals.size()) ? crystals[i] : -1;
            slots.push_back(power);
        }
        return slots;
    }

    std::vector<int> types;
    for (int row : rows) {
        const UpgradeKind *kind = table.kind(row);
        if (kind && kind->type >= 0 && std::find(types.begin(), types.end(), kind->type) == types.end()) {
            types.push_back(kind->type);
        }
    }
    std::sort(types.begin(), types.end());
    for (int type : types) {
        UpgradeSlot slot;
        slot.type = type;
        auto rowsInstalled = installedOfType(type);
        slot.installedRow = rowsInstalled.empty() ? -1 : rowsInstalled.front();
        slots.push_back(slot);
    }
    return slots;
}

bool UpgradeBench::begin(const std::shared_ptr<Item> &item) {
    cancel();
    if (!item) {
        return false;
    }
    if (auto upgrades = _services.resource.twoDas.get("upgrade")) {
        _table = UpgradeTable(*upgrades);
    } else {
        _table = UpgradeTable();
    }
    if (auto upcrystals = _services.resource.twoDas.get("upcrystals")) {
        _crystals = ColorCrystalTable(*upcrystals, [this](const std::string &resRef) {
            auto uti = _services.resource.gffs.get(resRef, resource::ResType::Uti);
            return uti ? uti->getInt("ModelVariation", -1) : -1;
        });
    } else {
        _crystals = ColorCrystalTable();
    }
    _item = item;
    _staged = item->upgrades();
    _stagedModelVariation = item->modelVariation();
    return true;
}

void UpgradeBench::cancel() {
    _item.reset();
    _staged = 0;
    _stagedModelVariation = -1;
    _consumed.clear();
}

std::string UpgradeBench::colorCrystalFor(int modelVariation) const {
    auto target = _item.resolve();
    return _crystals.crystalFor(modelVariation, target ? target->baseItemType() : 8);
}

bool UpgradeBench::isEditing() const {
    return static_cast<bool>(_item.resolve());
}

std::shared_ptr<Item> UpgradeBench::item() const {
    return _item.resolve();
}

std::vector<UpgradeSlot> UpgradeBench::slots() const {
    auto target = _item.resolve();
    if (!target) {
        return {};
    }
    return upgradeSlotsFor(*target, _staged, _table);
}

void UpgradeBench::useHolder(std::shared_ptr<Object> inventory, std::vector<uint32_t> wearers) {
    _holder = std::move(inventory);
    _wearers = std::move(wearers);
    _hasHolder = true;
}

std::shared_ptr<Object> UpgradeBench::partyInventory() const {
    if (_hasHolder) {
        return _holder;
    }
    auto player = _game.party().actualPlayer();
    if (!player) {
        return nullptr;
    }
    return _game.party().sharedInventoryReceiver(player);
}

int UpgradeBench::stagedCount(const std::shared_ptr<Item> &upgrade) const {
    return static_cast<int>(std::count_if(_consumed.begin(), _consumed.end(), [&upgrade](const auto &entry) {
        return entry.second.resolve() == upgrade;
    }));
}

std::vector<std::shared_ptr<Item>> UpgradeBench::candidates(int slotIndex) const {
    std::vector<std::shared_ptr<Item>> result;
    auto target = _item.resolve();
    auto inventory = partyInventory();
    auto allSlots = slots();
    if (!target || !inventory || slotIndex < 0 || slotIndex >= static_cast<int>(allSlots.size())) {
        return result;
    }
    const UpgradeSlot &slot = allSlots[slotIndex];
    auto supported = target->upgradeRows();
    for (const auto &candidate : inventory->items()) {
        if (!candidate || candidate == target || candidate->isEquipped() ||
            candidate->stackSize() - stagedCount(candidate) <= 0) {
            continue;
        }
        if (slot.color) {
            int variation = _crystals.modelVariation(candidate->blueprintResRef(), target->baseItemType());
            if (variation >= 0 && variation != _stagedModelVariation) {
                result.push_back(candidate);
            }
            continue;
        }
        if (slot.installedRow >= 0) {
            continue;
        }
        const UpgradeKind *kind = _table.kindForTemplate(candidate->blueprintResRef());
        if (!kind || kind->type != slot.type || (_staged & rowBit(kind->row)) != 0 ||
            std::find(supported.begin(), supported.end(), kind->row) == supported.end()) {
            continue;
        }
        result.push_back(candidate);
    }
    return result;
}

bool UpgradeBench::install(int slotIndex, const std::shared_ptr<Item> &upgrade) {
    auto options = candidates(slotIndex);
    if (!upgrade || std::find(options.begin(), options.end(), upgrade) == options.end()) {
        return false;
    }
    auto allSlots = slots();
    if (allSlots[slotIndex].color) {
        // A staged colour replaces an earlier staged one.
        _consumed.erase(std::remove_if(_consumed.begin(), _consumed.end(), [](const auto &entry) {
                            return entry.first == kColorCrystalKey;
                        }),
                        _consumed.end());
        auto target = _item.resolve();
        _stagedModelVariation = _crystals.modelVariation(
            upgrade->blueprintResRef(), target ? target->baseItemType() : 8);
        if (target && _stagedModelVariation != target->modelVariation()) {
            _consumed.emplace_back(kColorCrystalKey, RuntimeObjectRef<Item>(upgrade));
        }
        return true;
    }
    const UpgradeKind *kind = _table.kindForTemplate(upgrade->blueprintResRef());
    _staged |= rowBit(kind->row);
    _consumed.emplace_back(kind->row, RuntimeObjectRef<Item>(upgrade));
    return true;
}

bool UpgradeBench::remove(int slotIndex) {
    auto allSlots = slots();
    if (slotIndex < 0 || slotIndex >= static_cast<int>(allSlots.size())) {
        return false;
    }
    const UpgradeSlot &slot = allSlots[slotIndex];
    // A lightsaber always keeps a colour crystal; it is swapped, not removed.
    if (slot.color || slot.installedRow < 0) {
        return false;
    }
    _staged &= ~rowBit(slot.installedRow);
    _consumed.erase(std::remove_if(_consumed.begin(), _consumed.end(), [&slot](const auto &entry) {
                        return entry.first == slot.installedRow;
                    }),
                    _consumed.end());
    return true;
}

bool UpgradeBench::isDirty() const {
    auto target = _item.resolve();
    return target && (_staged != target->upgrades() || _stagedModelVariation != target->modelVariation());
}

bool UpgradeBench::assemble() {
    auto target = _item.resolve();
    auto inventory = partyInventory();
    if (!target || !inventory) {
        cancel();
        return false;
    }
    uint32_t original = target->upgrades();
    uint32_t removed = original & ~_staged;
    int originalVariation = target->modelVariation();

    // Installed upgrade items leave the party inventory.
    for (const auto &[row, ref] : _consumed) {
        auto upgrade = ref.resolve();
        if (!upgrade) {
            continue;
        }
        bool last = false;
        if (inventory->removeItem(upgrade, last) && last) {
            _game.destroyRuntimeObjectGraph(upgrade);
        }
    }
    // Removed upgrades come back as items.
    for (int row = 0; row < Item::kMaxUpgradeRows; ++row) {
        if ((removed & rowBit(row)) == 0) {
            continue;
        }
        const UpgradeKind *kind = _table.kind(row);
        if (kind && !kind->templateResRef.empty()) {
            inventory->addItem(kind->templateResRef, 1, true);
        }
    }
    if (_stagedModelVariation >= 0 && _stagedModelVariation != originalVariation) {
        auto oldCrystal = _crystals.crystalFor(originalVariation, target->baseItemType());
        if (!oldCrystal.empty()) {
            inventory->addItem(oldCrystal, 1, true);
        }
        target->setModelVariation(_stagedModelVariation);
    }
    target->setUpgrades(_staged);

    if (target->isEquipped()) {
        if (auto owner = _game.getObjectById<Creature>(target->owner())) {
            owner->refreshEquippedItems();
        }
    }
    cancel();
    return true;
}

std::vector<std::shared_ptr<Item>> UpgradeBench::partyItems(UpgradeCategory category) const {
    std::vector<std::shared_ptr<Item>> result;
    auto add = [&](const std::shared_ptr<Item> &item) {
        if (!item || std::find(result.begin(), result.end(), item) != result.end()) {
            return;
        }
        UpgradeCategory itemCategory = upgradeCategoryOf(*item);
        if (itemCategory == UpgradeCategory::None ||
            (category != UpgradeCategory::None && itemCategory != category)) {
            return;
        }
        result.push_back(item);
    };
    if (auto inventory = partyInventory()) {
        for (const auto &item : inventory->items()) {
            add(item);
        }
    }
    if (_hasHolder) {
        for (uint32_t id : _wearers) {
            if (auto wearer = _game.getObjectById<Creature>(id)) {
                for (const auto &[slot, item] : wearer->equipment()) {
                    add(item);
                }
            }
        }
        return result;
    }
    for (int i = 0; i < static_cast<int>(_game.party().getSize()); ++i) {
        auto member = _game.party().getMember(i);
        if (!member) {
            continue;
        }
        for (const auto &[slot, item] : member->equipment()) {
            add(item);
        }
    }
    return result;
}

} // namespace game

} // namespace reone
