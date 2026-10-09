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

#include "../storesession.h"

namespace reone {

namespace game {

class Item;

/**
 * The merchant screen (store.gui): Buy and Sell lists, prices and credits.
 * Every trade goes through the game's StoreSession, the path the harness
 * `store` command takes too.
 */
class StoreGUI : public GameGUI {
public:
    enum class Mode {
        Buy,
        Sell
    };

    StoreGUI(Game &game, ServicesView &services) :
        GameGUI(game, services) {
        _resRef = guiResRef("store");
    }

    bool handle(const input::Event &event) override;
    void update(float dt) override;

    /** Show the session's store, in Buy mode. */
    void open();
    void close();

    Mode mode() const { return _mode; }
    void setMode(Mode mode);

    /** The items the current list shows, in list order. */
    const std::vector<std::shared_ptr<Item>> &shownItems() const { return _shownItems; }
    std::shared_ptr<Item> selectedItem() const;
    bool selectItem(const std::shared_ptr<Item> &item);

    /** The Accept button: buy or sell the selected item. */
    StoreSession::Result accept();

private:
    struct Controls {
        std::shared_ptr<gui::Button> BTN_Accept;
        std::shared_ptr<gui::Button> BTN_Cancel;
        std::shared_ptr<gui::Button> BTN_Examine;
        std::shared_ptr<gui::Label> LBL_BUYSELL;
        std::shared_ptr<gui::Label> LBL_COST;
        std::shared_ptr<gui::Label> LBL_COST_VALUE;
        std::shared_ptr<gui::Label> LBL_CREDITS;
        std::shared_ptr<gui::Label> LBL_CREDITS_VALUE;
        std::shared_ptr<gui::Label> LBL_STOCK;
        std::shared_ptr<gui::Label> LBL_STOCK_VALUE;
        std::shared_ptr<gui::Label> LBL_ONHAND_VALUE; // K2 layout: "In Inventory"
        std::shared_ptr<gui::ListBox> LB_DESCRIPTION;
        std::shared_ptr<gui::ListBox> LB_INVITEMS;
        std::shared_ptr<gui::ListBox> LB_SHOPITEMS;
    };

    Controls _controls;
    Mode _mode {Mode::Buy};
    std::vector<std::shared_ptr<Item>> _shownItems;
    // What the lists were built from, to rebuild when the world changes them.
    std::vector<std::pair<uint32_t, int>> _signature;
    int _shownGold {-1};
    int _describedIndex {-2};

    std::string _buyText;
    std::string _sellText;
    std::string _showBuyText;
    std::string _showSellText;

    void onGUILoaded() override;
    void bindControls();
    void configureList(gui::ListBox &list);

    gui::ListBox *activeList() const;
    std::vector<std::shared_ptr<Item>> currentItems() const;
    std::vector<std::pair<uint32_t, int>> signatureOf(const std::vector<std::shared_ptr<Item>> &items) const;
    void refresh();
    void refreshSelection();
};

} // namespace game

} // namespace reone
