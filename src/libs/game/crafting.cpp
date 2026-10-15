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

#include "reone/game/crafting.h"

#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/strings.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"

using namespace reone::resource;

namespace reone {

namespace game {

namespace crafting {

int materialValue(int itemCost) {
    return std::max(1, itemCost / 20);
}

int breakdownYield(int value, int skillRank) {
    float factor = std::min(1.0f, static_cast<float>(std::max(0, skillRank)) / 20.0f);
    return std::max(1, static_cast<int>(static_cast<float>(value) * factor));
}

Material materialOfBaseItem(int baseItem) {
    // K1 baseitems.2da: grenades 25-34, implants 48-50, adrenaline 53,
    // combat shots 54, medical equipment 55, squad recovery kit 91.
    if ((baseItem >= 25 && baseItem <= 34) || (baseItem >= 48 && baseItem <= 50) ||
        (baseItem >= 53 && baseItem <= 55) || baseItem == 91) {
        return Material::Chemicals;
    }
    return Material::Components;
}

SkillType breakdownSkill(Material material) {
    return material == Material::Chemicals ? SkillType::TreatInjury : SkillType::Repair;
}

bool isBreakableBaseItem(int baseItem) {
    switch (baseItem) {
    case 51: // unused
    case 52: // datapad
    case 57: // credits
    case 64: // plot usable item
    case 65: // aesthetic item
    case 81:
    case 82:
    case 83:
    case 84: // creature weapons and hides
    case 86: // Pazaak card
    case 87: // Pazaak side deck
        return false;
    default:
        return baseItem >= 0;
    }
}

std::vector<Recipe> loadRecipes(ITwoDAs &twoDas) {
    std::vector<Recipe> result;
    if (auto items = twoDas.get("itemcreate")) {
        for (int row = 0; row < items->getRowCount(); ++row) {
            int group = items->getInt(row, "group", -1);
            if (group != 4) {
                continue;
            }
            Recipe recipe;
            recipe.templateResRef = boost::to_lower_copy(items->getString(row, "label"));
            recipe.skill = static_cast<SkillType>(items->getInt(row, "base_skill", 5));
            recipe.skillRank = items->getInt(row, "skill", 0);
            recipe.material = Material::Components;
            recipe.group = group;
            if (!recipe.templateResRef.empty()) {
                result.push_back(std::move(recipe));
            }
        }
    }
    if (auto chemicals = twoDas.get("chemicalcreate")) {
        for (int row = 0; row < chemicals->getRowCount(); ++row) {
            int group = chemicals->getInt(row, "group", -1);
            if (group < 0 || group > 3) {
                continue;
            }
            Recipe recipe;
            recipe.templateResRef = boost::to_lower_copy(chemicals->getString(row, "label"));
            recipe.skill = static_cast<SkillType>(chemicals->getInt(row, "base_skill", 7));
            recipe.skillRank = chemicals->getInt(row, "skilllevel", 0);
            recipe.material = Material::Chemicals;
            recipe.group = group;
            if (!recipe.templateResRef.empty()) {
                result.push_back(std::move(recipe));
            }
        }
    }
    return result;
}

} // namespace crafting

void CraftingBench::useHolder(std::shared_ptr<Object> inventory, int *components, int *chemicals) {
    _holder = std::move(inventory);
    _components = components;
    _chemicals = chemicals;
    _hasHolder = true;
}

void CraftingBench::useParty() {
    _holder.reset();
    _components = nullptr;
    _chemicals = nullptr;
    _hasHolder = false;
}

std::shared_ptr<Object> CraftingBench::inventoryHolder() const {
    if (_hasHolder) {
        return _holder;
    }
    auto player = _game.party().player();
    if (!player) {
        return nullptr;
    }
    auto holder = _game.party().sharedInventoryReceiver(player);
    return holder ? holder : std::static_pointer_cast<Object>(player);
}

int CraftingBench::components() const {
    if (_hasHolder) {
        return _components ? *_components : 0;
    }
    return static_cast<int>(_game.party().persistedState().itemComponent);
}

int CraftingBench::chemicals() const {
    if (_hasHolder) {
        return _chemicals ? *_chemicals : 0;
    }
    return static_cast<int>(_game.party().persistedState().itemChemical);
}

void CraftingBench::addMaterial(crafting::Material material, int amount) {
    if (_hasHolder) {
        int *count = material == crafting::Material::Chemicals ? _chemicals : _components;
        if (count) {
            *count = std::max(0, *count + amount);
        }
        return;
    }
    auto &party = _game.party();
    uint32_t components = party.persistedState().itemComponent;
    uint32_t chemicals = party.persistedState().itemChemical;
    if (material == crafting::Material::Chemicals) {
        chemicals = static_cast<uint32_t>(std::max(0, static_cast<int>(chemicals) + amount));
    } else {
        components = static_cast<uint32_t>(std::max(0, static_cast<int>(components) + amount));
    }
    party.setCraftingMaterials(components, chemicals);
}

std::vector<std::shared_ptr<Item>> CraftingBench::breakdownCandidates() const {
    std::vector<std::shared_ptr<Item>> result;
    auto owner = inventoryHolder();
    if (!owner) {
        return result;
    }
    for (const auto &item : owner->items()) {
        if (!item || item->isEquipped() || item->plotFlag() ||
            !crafting::isBreakableBaseItem(item->baseItemType())) {
            continue;
        }
        result.push_back(item);
    }
    return result;
}

int CraftingBench::breakdownYieldFor(const Item &item, const Creature &crafter) const {
    auto material = crafting::materialOfBaseItem(item.baseItemType());
    int rank = crafter.getSkillValue(crafting::breakdownSkill(material));
    return crafting::breakdownYield(crafting::materialValue(static_cast<int>(item.cost())), rank);
}

bool CraftingBench::breakDown(const std::shared_ptr<Item> &item, const Creature &crafter) {
    if (!item) {
        return false;
    }
    auto candidates = breakdownCandidates();
    if (std::find(candidates.begin(), candidates.end(), item) == candidates.end()) {
        return false;
    }
    int yield = breakdownYieldFor(*item, crafter);
    auto material = crafting::materialOfBaseItem(item->baseItemType());
    auto owner = inventoryHolder();
    if (!owner) {
        return false;
    }
    bool last = false;
    if (!owner->removeItem(item, last)) {
        return false;
    }
    addMaterial(material, yield);
    return true;
}

std::string CraftingBench::resolveTemplate(const std::string &resRef) const {
    if (_services.resource.gffs.get(resRef, ResType::Uti)) {
        return resRef;
    }
    return "k2:" + resRef;
}

std::vector<CraftingBench::RecipeEntry> CraftingBench::recipes(const Creature &crafter) const {
    std::vector<RecipeEntry> result;
    for (auto &recipe : crafting::loadRecipes(_services.resource.twoDas)) {
        auto uti = _services.resource.gffs.get(resolveTemplate(recipe.templateResRef), ResType::Uti);
        if (!uti) {
            continue;
        }
        RecipeEntry entry;
        entry.recipe = recipe;
        LocString name;
        if (uti->readLocString(name, "LocalizedName", _services.resource.strings)) {
            entry.name = name.str();
        }
        if (entry.name.empty()) {
            entry.name = recipe.templateResRef;
        }
        entry.cost = crafting::materialValue(static_cast<int>(uti->getUint("Cost", 0)));
        entry.skilled = crafter.getSkillValue(recipe.skill) >= recipe.skillRank;
        int available = recipe.material == crafting::Material::Chemicals ? chemicals() : components();
        entry.affordable = available >= entry.cost;
        result.push_back(std::move(entry));
    }
    return result;
}

bool CraftingBench::create(const crafting::Recipe &recipe, const Creature &crafter) {
    auto entries = recipes(crafter);
    auto entry = std::find_if(entries.begin(), entries.end(), [&recipe](const RecipeEntry &candidate) {
        return candidate.recipe.templateResRef == recipe.templateResRef &&
               candidate.recipe.material == recipe.material;
    });
    if (entry == entries.end() || !entry->skilled || !entry->affordable) {
        return false;
    }
    auto owner = inventoryHolder();
    if (!owner) {
        return false;
    }
    auto item = owner->addItem(resolveTemplate(recipe.templateResRef), 1, true);
    if (!item) {
        return false;
    }
    addMaterial(recipe.material, -entry->cost);
    return true;
}

} // namespace game

} // namespace reone
