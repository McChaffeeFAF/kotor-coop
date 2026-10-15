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

#include <functional>
#include <string>
#include <vector>

#include "runtimeref.h"

namespace reone {

namespace resource {
class TwoDA;
}

namespace game {

class Creature;
class Game;
struct ServicesView;
class Item;
class Object;

/** The four upgrade bench categories (upgradesel.gui). */
enum class UpgradeCategory {
    None,
    Lightsaber,
    Ranged,
    Melee,
    Armor
};

/** One row of upgrade.2da: an upgrade kind. */
struct UpgradeKind {
    int row {-1};
    std::string templateResRef; // lower case
    int type {-1};              // upgradetypes.2da row
};

/** upgrade.2da as the bench reads it. */
class UpgradeTable {
public:
    UpgradeTable() = default;
    explicit UpgradeTable(const resource::TwoDA &upgrades);

    const std::vector<UpgradeKind> &kinds() const { return _kinds; }
    const UpgradeKind *kind(int row) const;
    const UpgradeKind *kindForTemplate(const std::string &resRef) const;

private:
    std::vector<UpgradeKind> _kinds;
};

/** A slot on an upgradeable item. */
struct UpgradeSlot {
    int type {-1};         // upgradetypes.2da row the slot takes
    bool color {false};    // the lightsaber colour crystal slot
    int installedRow {-1}; // upgrade.2da row in it, or -1 (a power/part slot)
};

UpgradeCategory upgradeCategoryOf(const Item &item);

/** Lightsaber model variation a colour crystal gives, or -1 if it is none. */
int colorCrystalModelVariation(const std::string &templateResRef);
/** The colour crystal template for a lightsaber model variation, or "". */
std::string colorCrystalForModelVariation(int modelVariation);

/**
 * upcrystals.2da (design note c11_data): each colour crystal names the saber template
 * whose blade it gives, per saber kind (shortmdlvar, longmdlvar,
 * doublemdlvar); the blade is that template's ModelVariation. Without the
 * table, the five K1 colour crystals above apply to every saber kind.
 */
class ColorCrystalTable {
public:
    struct Crystal {
        std::string templateResRef;
        // model variation for the short, long and double saber, or -1
        int shortVariation {-1};
        int longVariation {-1};
        int doubleVariation {-1};
    };

    ColorCrystalTable() = default;
    ColorCrystalTable(
        const resource::TwoDA &upcrystals,
        const std::function<int(const std::string &)> &templateModelVariation);

    bool empty() const { return _crystals.empty(); }
    const std::vector<Crystal> &crystals() const { return _crystals; }

    /** Model variation the crystal gives a saber of that base item, or -1. */
    int modelVariation(const std::string &crystalResRef, int saberBaseItem) const;
    /** The crystal that gives a saber of that base item this variation, or "". */
    std::string crystalFor(int modelVariation, int saberBaseItem) const;

private:
    std::vector<Crystal> _crystals;

    static int variationFor(const Crystal &crystal, int saberBaseItem);
};

/** The slots of an item holding the given installed rows (design note gui_screens). */
std::vector<UpgradeSlot> upgradeSlotsFor(const Item &item, uint32_t installed, const UpgradeTable &table);

/**
 * The upgrade bench's working copy of one item: slot edits are staged here
 * and applied together on Assemble. The screen and the harness `upgrade`
 * command both drive it.
 */
class UpgradeBench {
public:
    UpgradeBench(Game &game, ServicesView &services) :
        _game(game),
        _services(services) {
    }

    /**
     * Work on one holder instead of the party: its inventory supplies the parts, takes the
     * returned ones, and the equipped items of `wearers` can be edited too. Co-op guests'
     * benches (a guest's own bag in Free Mode, the shared inventory in Story Mode).
     */
    void useHolder(std::shared_ptr<Object> inventory, std::vector<uint32_t> wearers);
    /** Back to the party's inventory and members (undoes useHolder). */
    void useParty();

    /** Start editing an item (loads upgrade.2da). */
    bool begin(const std::shared_ptr<Item> &item);
    void cancel();
    bool isEditing() const;
    std::shared_ptr<Item> item() const;

    const UpgradeTable &table() const { return _table; }
    const ColorCrystalTable &crystals() const { return _crystals; }
    /** Colour crystal for a model variation of the item being edited, or "". */
    std::string colorCrystalFor(int modelVariation) const;
    std::vector<UpgradeSlot> slots() const;
    uint32_t stagedUpgrades() const { return _staged; }
    /** The inventory item staged into a slot now (not what was installed before), or null. */
    std::shared_ptr<Item> stagedPart(int slot) const;
    /**
     * The template of what is in a slot as staged: the colour crystal of the staged blade colour
     * for a saber's colour slot, else the installed upgrade row's template; "" when empty. The
     * slot's icon and the list's installed row both show this, so they always agree.
     */
    std::string installedTemplate(int slot) const;
    int stagedModelVariation() const { return _stagedModelVariation; }

    /** Party inventory items that could go into a slot now. */
    std::vector<std::shared_ptr<Item>> candidates(int slot) const;
    /** Stage an upgrade item into a slot. False when it does not fit. */
    bool install(int slot, const std::shared_ptr<Item> &upgrade);
    /** Stage emptying a slot. False when there is nothing to take out. */
    bool remove(int slot);
    bool isDirty() const;

    /**
     * Apply the staged slots to the item (the Assemble button): installed
     * upgrade items leave the party inventory, removed ones come back, and an
     * equipped item's effects are rebuilt. Ends the edit.
     */
    bool assemble();

    /** Every upgradeable item the party holds in a category (or all). */
    std::vector<std::shared_ptr<Item>> partyItems(UpgradeCategory category) const;

private:
    Game &_game;
    ServicesView &_services;
    UpgradeTable _table;
    ColorCrystalTable _crystals;
    RuntimeObjectRef<Item> _item;
    std::shared_ptr<Object> _holder;
    std::vector<uint32_t> _wearers;
    bool _hasHolder {false};
    uint32_t _staged {0};
    int _stagedModelVariation {-1};
    // Upgrade items staged for consumption, keyed by upgrade row (or -2 for
    // the colour crystal).
    std::vector<std::pair<int, RuntimeObjectRef<Item>>> _consumed;

    std::shared_ptr<Object> partyInventory() const;
    int stagedCount(const std::shared_ptr<Item> &upgrade) const;
};

} // namespace game

} // namespace reone
