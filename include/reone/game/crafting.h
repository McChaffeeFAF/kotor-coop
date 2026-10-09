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

#include "types.h"

#include <memory>
#include <string>
#include <vector>

namespace reone {

namespace resource {

class ITwoDAs;

}

namespace game {

class Creature;
class Game;
class Item;
struct ServicesView;

/**
 * K2 crafting and breakdown at the upgrade bench (design note k2_merge,
 * "Crafting"). K2 behaviour: K2's workbench makes items from components and
 * breaks items down into them; K2's lab station does the same with
 * chemicals. K1 has no lab station, so its workbench does both.
 */
namespace crafting {

enum class Material {
    Components,
    Chemicals
};

/// An item's material value: its cost / 20, at least 1 (K2).
int materialValue(int itemCost);

/**
 * Material from breaking an item down: its value times Repair (components)
 * or Treat Injury (chemicals) rank / 20, the factor capped at 1, truncated,
 * at least 1 (K2).
 */
int breakdownYield(int value, int skillRank);

/// Medical items, stimulants, implants and grenades are chemical items; the
/// rest are made of components.
Material materialOfBaseItem(int baseItem);

/// The skill that sets a breakdown's yield.
SkillType breakdownSkill(Material material);

/// Base items that are never broken down (credits, datapads, plot and
/// Pazaak items, creature weapons).
bool isBreakableBaseItem(int baseItem);

struct Recipe {
    std::string templateResRef;
    SkillType skill {SkillType::Invalid};
    int skillRank {0};
    Material material {Material::Components};
    int group {0};
};

/**
 * The recipes K1 can use: K2's itemcreate group 4 (weapons, armor, shields
 * and gear) and chemicalcreate groups 0-3 (medical, stimulants, implants,
 * grenades). K2's upgrade parts (itemcreate groups 0-3) need K2's upgrade
 * slots, and chemicalcreate group 5 are K2 quest items; neither is offered.
 */
std::vector<Recipe> loadRecipes(resource::ITwoDAs &twoDas);

} // namespace crafting

/**
 * The bench's crafting operations for one crafter. Components and chemicals
 * are party-wide counts (saved as K2's party table fields).
 */
class CraftingBench {
public:
    struct RecipeEntry {
        crafting::Recipe recipe;
        std::string name;
        int cost {0};
        bool skilled {false};
        bool affordable {false};
    };

    CraftingBench(Game &game, ServicesView &services) :
        _game(game),
        _services(services) {
    }

    int components() const;
    int chemicals() const;

    /// Items in the party's inventory that can be broken down.
    std::vector<std::shared_ptr<Item>> breakdownCandidates() const;
    int breakdownYieldFor(const Item &item, const Creature &crafter) const;
    /// Break one of the item (one of its stack) down. False if it cannot.
    bool breakDown(const std::shared_ptr<Item> &item, const Creature &crafter);

    std::vector<RecipeEntry> recipes(const Creature &crafter) const;
    /// Make one item of a recipe. False without the skill or the material.
    bool create(const crafting::Recipe &recipe, const Creature &crafter);

private:
    Game &_game;
    ServicesView &_services;

    /// The template of a recipe: K1's when K1 has one of that name, else
    /// K2's (the "k2:" prefix).
    std::string resolveTemplate(const std::string &resRef) const;
    std::shared_ptr<Item> previewItem(const std::string &resRef) const;
    void addMaterial(crafting::Material material, int amount);
};

} // namespace game

} // namespace reone
