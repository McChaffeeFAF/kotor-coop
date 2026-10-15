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

#include "../gui.h"

#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"

#include "../crafting.h"
#include "../upgradebench.h"

namespace reone {

namespace game {

class Creature;
class GuestBenchScreen;
class Item;
class UpgradeGUI;

/**
 * Where a list goes when its authored rectangle runs under header labels and over controls
 * below it (K1's upgrade.gui authors LB_ITEMS over the whole left panel): below every header
 * that overlaps the list's top half, above the first footer that starts inside it. Headers and
 * footers that do not overlap the list horizontally are ignored; the list is returned as authored
 * when nothing overlaps or too little room would be left.
 */
gui::Control::Extent fitListBetween(const gui::Control::Extent &list,
                                    const std::vector<gui::Control::Extent> &headers,
                                    const std::vector<gui::Control::Extent> &footers,
                                    int gap = 3);

/** upgradeitems.gui: the upgradeable items of one category. */
class UpgradeItemsPanel : public GameGUI {
public:
    UpgradeItemsPanel(UpgradeGUI &host, Game &game, ServicesView &services) :
        GameGUI(game, services),
        _host(host) {
        _resRef = guiResRef("upgradeitems");
    }

    void show(UpgradeCategory category);
    void refresh();
    const std::vector<std::shared_ptr<Item>> &items() const { return _items; }
    std::shared_ptr<Item> selectedItem() const;

private:
    UpgradeGUI &_host;
    UpgradeCategory _category {UpgradeCategory::None};
    std::vector<std::shared_ptr<Item>> _items;
    // With the K2 systems merged, two rows after the items open crafting.
    bool _craftingRows {false};
    std::shared_ptr<gui::ListBox> _list;
    std::shared_ptr<gui::ListBox> _description;
    std::shared_ptr<gui::Button> _upgradeButton;
    std::shared_ptr<gui::Button> _backButton;

    void onGUILoaded() override;
    void describeSelection();
    /// The crafting row selected (0 create, 1 break down), or -1.
    int selectedCraftingRow() const;
    void activateSelection();
};

/**
 * K2 crafting and breakdown at the bench (the K2 systems merge,
 * design note k2_merge "Crafting"), in the items page's layout
 * (upgradeitems / upgradeitems_p): the list shows the recipes, or the items
 * that can be broken down; its first row switches between the two; the
 * action button makes or breaks down the selection.
 */
class CraftingPanel : public GameGUI {
public:
    enum class Mode {
        Create,
        BreakDown
    };

    CraftingPanel(UpgradeGUI &host, Game &game, ServicesView &services) :
        GameGUI(game, services),
        _host(host),
        _crafting(game, services) {
        _resRef = guiResRef("upgradeitems");
    }

    /// A private copy of the layout: the items page shares the cached one,
    /// and this page rewires its list and buttons.
    void init() override;

    void show(Mode mode);
    void refresh();
    Mode mode() const { return _mode; }
    CraftingBench &crafting() { return _crafting; }
    /// Act on list row `row` (0 is the mode switch). False when nothing happened.
    /// A co-op guest's row sends the host a request instead (true when one went out).
    bool activate(int row);
    /// The list row of a recipe (Create) or an item (Break down) by template, or -1.
    int rowOf(const std::string &resRef) const;
    int selectedRow() const;
    void selectRow(int row);

private:
    UpgradeGUI &_host;
    CraftingBench _crafting;
    Mode _mode {Mode::Create};
    std::vector<CraftingBench::RecipeEntry> _recipes;
    std::vector<std::shared_ptr<Item>> _breakable;
    // A co-op guest's breakdown yields, from the host's view (by row).
    std::vector<int> _guestYields;
    std::shared_ptr<gui::ListBox> _list;
    std::shared_ptr<gui::ListBox> _description;
    std::shared_ptr<gui::Button> _actionButton;
    std::shared_ptr<gui::Button> _backButton;

    void onGUILoaded() override;
    void describeSelection();
    std::shared_ptr<Creature> crafter() const;
    /** A co-op guest: the recipes and breakable items from the host's view. */
    void loadGuestRows(const GuestBenchScreen &screen);
    int yieldOf(size_t index, const Item &item, const Creature *leader) const;
};

/** upgrade.gui: one item's slots and the upgrades that fit them. */
class UpgradeSlotsPanel : public GameGUI {
public:
    UpgradeSlotsPanel(UpgradeGUI &host, Game &game, ServicesView &services) :
        GameGUI(game, services),
        _host(host) {
        _resRef = guiResRef("upgrade");
    }

    /** Lay out the bench's item. */
    void show();
    void selectSlot(int slot);
    int selectedSlot() const { return _selectedSlot; }
    void refresh();

private:
    struct SlotControls {
        std::shared_ptr<gui::Button> button;
        std::shared_ptr<gui::Label> label;
    };

    UpgradeGUI &_host;
    std::vector<SlotControls> _slots3;
    std::vector<SlotControls> _slots4;
    std::vector<SlotControls> *_active {nullptr};
    int _selectedSlot {0};
    // LB_ITEMS rows: the installed upgrade (to take out) or a candidate.
    struct Row {
        bool installed {false};
        std::shared_ptr<Item> candidate;
    };
    std::vector<Row> _rows;

    std::shared_ptr<gui::ListBox> _list;
    std::shared_ptr<gui::ListBox> _description;
    std::shared_ptr<gui::ListBox> _descriptionSaber;
    std::shared_ptr<gui::Label> _descBackground;
    std::shared_ptr<gui::Label> _descBackgroundSaber;
    std::shared_ptr<gui::Label> _slotName;
    std::shared_ptr<gui::Label> _saberSlotName;
    std::shared_ptr<gui::Label> _property;
    std::shared_ptr<gui::Label> _count;
    std::shared_ptr<gui::Label> _title;
    std::shared_ptr<gui::Button> _assemble;
    std::shared_ptr<gui::Button> _back;
    // LB_ITEMS fitted between the labels and the slot buttons, for a saber and for other items.
    gui::Control::Extent _listExtent;
    gui::Control::Extent _listExtentSaber;

    void onGUILoaded() override;
    void fitLayout();
    void activateRow(int row);
    std::string slotName(const UpgradeSlot &slot) const;
};

/**
 * The upgrade bench (ShowUpgradeScreen): upgradesel.gui to pick a category,
 * then upgradeitems.gui, then upgrade.gui for one item. Assemble applies the
 * staged slots through UpgradeBench, which the harness `upgrade` command uses
 * too.
 */
class UpgradeGUI : public GameGUI {
public:
    enum class Page {
        Select,
        Items,
        Slots,
        Crafting
    };

    UpgradeGUI(Game &game, ServicesView &services);

    void init() override;
    bool handle(const input::Event &event) override;
    void update(float dt) override;
    void render() override;
    PresentationGUI *inputPage() override {
        auto page = pageGUI();
        return page ? static_cast<PresentationGUI *>(page) : this;
    }

    /** Open on the category page, or straight on an item. */
    void open(const std::shared_ptr<Item> &item);
    void openCategory(UpgradeCategory category);
    void openItem(const std::shared_ptr<Item> &item);
    /** K2 crafting (only with the K2 systems merged). */
    void openCrafting(CraftingPanel::Mode mode = CraftingPanel::Mode::Create);
    bool craftingAvailable() const;
    CraftingPanel &craftingPanel() { return *_craftingPanel; }
    /** Back: from the slots page discards staged changes. */
    void back();
    /** Assemble: apply the staged slots and return. */
    bool assemble();
    void close();

    /**
     * A co-op guest's bench (CoopSession::benchScreen): the screen works on local stand-ins of
     * the host's items; Assemble, Create and Break Down send requests, and a new view from the
     * host redraws it. A lab opens straight on crafting.
     */
    void openGuest();
    void refreshGuest();
    void leaveGuest();
    bool guest() const { return _guest; }
    GuestBenchScreen *guestScreen() const;
    /** The host's reason for the last refused request, for the description lists, or "". */
    std::string guestNote() const;

    Page page() const { return _page; }
    UpgradeBench &bench() { return _bench; }
    UpgradeSlotsPanel &slotsPanel() { return *_slotsPanel; }
    /** The icon and name of an upgrade.2da row's template item. */
    std::pair<std::shared_ptr<graphics::Texture>, std::string> templateLook(const std::string &resRef) const;

private:
    UpgradeBench _bench;
    Page _page {Page::Select};
    UpgradeCategory _category {UpgradeCategory::None};
    // Opened on an item by a script: Back and Assemble leave the bench.
    bool _directItem {false};
    // A co-op guest's bench: the item on the slots page by host id, the material counts the
    // crafting page reads (the host's), and whether it is a lab (crafting only).
    bool _guest {false};
    bool _guestLab {false};
    uint32_t _guestItem {0};
    int _guestComponents {0};
    int _guestChemicals {0};
    std::unique_ptr<UpgradeItemsPanel> _itemsPanel;
    std::unique_ptr<UpgradeSlotsPanel> _slotsPanel;
    std::unique_ptr<CraftingPanel> _craftingPanel;
    // K2's workbench page lists the items itself, filtered by the category
    // buttons (and All); K1's picks a category for the items page.
    std::shared_ptr<gui::ListBox> _selectList;
    std::shared_ptr<gui::ListBox> _selectDescription;
    std::vector<std::shared_ptr<Item>> _selectItems;

    void onGUILoaded() override;
    GameGUI *pageGUI();
    /** Mark the chosen category's button; the others plain. */
    void showCategoryChoice();
    void refreshSelectList();
    void describeSelectItem();
    std::shared_ptr<Item> selectedSelectItem() const;
    /** Back to the page that lists items: K2's workbench page, K1's items page. */
    void returnToItemList();
    void useGuestHolder();
    bool assembleGuest();
};

} // namespace game

} // namespace reone
