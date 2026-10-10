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

#include "reone/game/guestbench.h"

#include <boost/algorithm/string.hpp>

#include "reone/game/crafting.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/game/upgradebench.h"

using namespace reone::net;

namespace reone {

namespace game {

namespace {

std::string resRefOf(const Item &item) {
    auto resref = boost::to_lower_copy(item.blueprintResRef());
    return resref.size() <= 16 ? resref : std::string();
}

uint16_t stackOf(const Item &item) {
    return static_cast<uint16_t>(std::clamp(item.stackSize(), 1, 65535));
}

} // namespace

// The benches below are built per call: they hold no state between a guest's commands.

namespace {

struct Benches {
    UpgradeBench upgrades;
    CraftingBench crafting;

    Benches(Game &game, ServicesView &services, const GuestBench::Context &context) :
        upgrades(game, services),
        crafting(game, services) {
        if (context.own) {
            upgrades.useHolder(context.inventory, context.wearers);
            crafting.useHolder(context.inventory, context.components, context.chemicals);
        }
    }
};

std::shared_ptr<Item> findItem(const std::vector<std::shared_ptr<Item>> &items, uint32_t id) {
    for (const auto &item : items) {
        if (item && item->id() == id) {
            return item;
        }
    }
    return nullptr;
}

} // namespace

BenchView GuestBench::view(uint32_t benchId) const {
    Benches benches(_game, _services, _context);
    BenchView view;
    view.bench = benchId;
    view.kind = _kind;
    view.open = true;
    view.components = std::max(0, benches.crafting.components());
    view.chemicals = std::max(0, benches.crafting.chemicals());

    std::vector<uint32_t> partIds;
    if (_kind == BenchKind::Workbench) {
        for (const auto &item : benches.upgrades.partyItems(UpgradeCategory::None)) {
            auto resref = resRefOf(*item);
            if (resref.empty() || view.items.size() >= kMaxBenchItems || !benches.upgrades.begin(item)) {
                continue;
            }
            BenchItem entry;
            entry.id = item->id();
            entry.resref = resref;
            entry.category = static_cast<uint8_t>(upgradeCategoryOf(*item));
            entry.equipped = item->isEquipped();
            entry.upgrades = item->upgrades();
            entry.modelVariation = static_cast<int16_t>(std::clamp(item->modelVariation(), -1, 1000));
            auto slots = benches.upgrades.slots();
            for (int i = 0; i < static_cast<int>(slots.size()) && entry.slots.size() < kMaxBenchSlots; ++i) {
                BenchSlot slot;
                slot.type = static_cast<uint8_t>(std::clamp(slots[i].type, 0, 255));
                slot.color = slots[i].color;
                slot.installedRow = static_cast<int16_t>(slots[i].installedRow);
                for (const auto &candidate : benches.upgrades.candidates(i)) {
                    if (slot.candidates.size() < kMaxBenchCandidates) {
                        slot.candidates.push_back(candidate->id());
                        if (std::find(partIds.begin(), partIds.end(), candidate->id()) == partIds.end()) {
                            partIds.push_back(candidate->id());
                            auto partRef = resRefOf(*candidate);
                            if (!partRef.empty() && view.parts.size() < kMaxBenchParts) {
                                view.parts.push_back({candidate->id(), partRef, stackOf(*candidate), 0});
                            }
                        }
                    }
                }
                entry.slots.push_back(std::move(slot));
            }
            benches.upgrades.cancel();
            view.items.push_back(std::move(entry));
        }
    }
    if (_context.crafter) {
        for (const auto &item : benches.crafting.breakdownCandidates()) {
            auto resref = resRefOf(*item);
            auto material = crafting::materialOfBaseItem(item->baseItemType());
            if (resref.empty() || view.breakdown.size() >= kMaxBenchBreakdown ||
                (_kind == BenchKind::Lab && material != crafting::Material::Chemicals)) {
                continue;
            }
            view.breakdown.push_back({item->id(), resref, stackOf(*item),
                                      static_cast<uint16_t>(std::clamp(benches.crafting.breakdownYieldFor(*item, *_context.crafter), 0, 65535)),
                                      static_cast<uint8_t>(material == crafting::Material::Chemicals ? 1 : 0)});
        }
        for (const auto &entry : benches.crafting.recipes(*_context.crafter)) {
            if ((_kind == BenchKind::Lab && entry.recipe.material != crafting::Material::Chemicals) ||
                entry.recipe.templateResRef.size() > 16 || view.recipes.size() >= kMaxBenchRecipes) {
                continue;
            }
            view.recipes.push_back({entry.recipe.templateResRef,
                                    static_cast<uint8_t>(entry.recipe.material == crafting::Material::Chemicals ? 1 : 0),
                                    entry.cost, entry.skilled, entry.affordable});
        }
    }
    return view;
}

CommandReject GuestBench::upgrade(uint32_t itemId, uint32_t partId) {
    if (_kind != BenchKind::Workbench) {
        return CommandReject::BenchClosed;
    }
    Benches benches(_game, _services, _context);
    auto item = findItem(benches.upgrades.partyItems(UpgradeCategory::None), itemId);
    if (!item || !benches.upgrades.begin(item)) {
        return CommandReject::ItemUnavailable;
    }
    int slotCount = static_cast<int>(benches.upgrades.slots().size());
    std::shared_ptr<Item> part;
    int slot = -1;
    for (int i = 0; i < slotCount && slot < 0; ++i) {
        auto candidates = benches.upgrades.candidates(i);
        if (auto found = findItem(candidates, partId)) {
            part = found;
            slot = i;
        }
    }
    if (slot < 0) {
        benches.upgrades.cancel();
        // A part the guest does not hold is not a part at all; one it holds that does not fit is incompatible.
        auto inventory = _context.own ? _context.inventory
                                      : (_context.crafter ? _game.party().sharedInventoryReceiver(_context.crafter) : nullptr);
        bool held = inventory && findItem(inventory->items(), partId) != nullptr;
        return held ? CommandReject::UpgradeIncompatible : CommandReject::ItemUnavailable;
    }
    if (!benches.upgrades.install(slot, part) || !benches.upgrades.assemble()) {
        benches.upgrades.cancel();
        return CommandReject::UpgradeIncompatible;
    }
    return CommandReject::None;
}

CommandReject GuestBench::remove(uint32_t itemId, int slotIndex) {
    if (_kind != BenchKind::Workbench) {
        return CommandReject::BenchClosed;
    }
    Benches benches(_game, _services, _context);
    auto item = findItem(benches.upgrades.partyItems(UpgradeCategory::None), itemId);
    if (!item || !benches.upgrades.begin(item)) {
        return CommandReject::ItemUnavailable;
    }
    if (!benches.upgrades.remove(slotIndex) || !benches.upgrades.assemble()) {
        benches.upgrades.cancel();
        return CommandReject::UpgradeIncompatible;
    }
    return CommandReject::None;
}

CommandReject GuestBench::craft(const std::string &resref, int material) {
    if ((material != 0 && material != 1) || !_context.crafter || (_kind == BenchKind::Lab && material != 1)) {
        return CommandReject::BenchClosed;
    }
    Benches benches(_game, _services, _context);
    auto wanted = material == 1 ? crafting::Material::Chemicals : crafting::Material::Components;
    auto lower = boost::to_lower_copy(resref);
    for (const auto &entry : benches.crafting.recipes(*_context.crafter)) {
        if (entry.recipe.templateResRef != lower || entry.recipe.material != wanted) {
            continue;
        }
        if (!entry.skilled) {
            return CommandReject::SkillTooLow;
        }
        if (!entry.affordable) {
            return CommandReject::NotEnoughMaterial;
        }
        return benches.crafting.create(entry.recipe, *_context.crafter) ? CommandReject::None : CommandReject::ItemUnavailable;
    }
    return CommandReject::ItemUnavailable;
}

CommandReject GuestBench::breakDown(uint32_t itemId) {
    if (!_context.crafter) {
        return CommandReject::BenchClosed;
    }
    Benches benches(_game, _services, _context);
    auto item = findItem(benches.crafting.breakdownCandidates(), itemId);
    if (!item) {
        return CommandReject::NotBreakable;
    }
    if (_kind == BenchKind::Lab && crafting::materialOfBaseItem(item->baseItemType()) != crafting::Material::Chemicals) {
        return CommandReject::NotBreakable;
    }
    return benches.crafting.breakDown(item, *_context.crafter) ? CommandReject::None : CommandReject::NotBreakable;
}

} // namespace game

} // namespace reone
