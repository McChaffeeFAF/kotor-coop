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

#include "reone/game/gui/store.h"

#include "reone/gui/control/imagebutton.h"
#include "reone/resource/strings.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/itemdescription.h"
#include "reone/game/object/item.h"
#include "reone/game/object/store.h"
#include "reone/game/party.h"

#include <boost/algorithm/string.hpp>

using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

// store.gui / TLK labels.
static constexpr int kBuyStrRef = 32132;
static constexpr int kSellStrRef = 32130;
static constexpr int kShowSellListStrRef = 41938;
static constexpr int kShowBuyListStrRef = 41937;

void StoreGUI::bindControls() {
    _controls.BTN_Accept = findControl<Button>("BTN_Accept");
    _controls.BTN_Cancel = findControl<Button>("BTN_Cancel");
    _controls.BTN_Examine = findControl<Button>("BTN_Examine");
    _controls.LBL_BUYSELL = findControl<Label>("LBL_BUYSELL");
    _controls.LBL_COST = findControl<Label>("LBL_COST");
    _controls.LBL_COST_VALUE = findControl<Label>("LBL_COST_VALUE");
    _controls.LBL_CREDITS = findControl<Label>("LBL_CREDITS");
    _controls.LBL_CREDITS_VALUE = findControl<Label>("LBL_CREDITS_VALUE");
    _controls.LBL_STOCK = findControl<Label>("LBL_STOCK");
    _controls.LBL_STOCK_VALUE = findControl<Label>("LBL_STOCK_VALUE");
    _controls.LBL_ONHAND_VALUE = findControl<Label>("LBL_ONHAND_VALUE");
    _controls.LB_DESCRIPTION = findControl<ListBox>("LB_DESCRIPTION");
    _controls.LB_INVITEMS = findControl<ListBox>("LB_INVITEMS");
    _controls.LB_SHOPITEMS = findControl<ListBox>("LB_SHOPITEMS");
}

void StoreGUI::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();
    centerRootInCanvas(isTSL() ? 800 : 640, isTSL() ? 600 : 480);

    auto &strings = _services.resource.strings;
    _buyText = strings.getText(kBuyStrRef);
    _sellText = strings.getText(kSellStrRef);
    _showSellText = strings.getText(kShowSellListStrRef);
    _showBuyText = strings.getText(kShowBuyListStrRef);
    if (_sellText.empty()) {
        _sellText = "Sell";
    }
    if (_showBuyText.empty()) {
        _showBuyText = "Show Buy List";
    }

    if (_controls.BTN_Accept) {
        _controls.BTN_Accept->setOnClick([this]() {
            accept();
        });
    }
    if (_controls.BTN_Cancel) {
        _controls.BTN_Cancel->setOnClick([this]() {
            close();
        });
    }
    if (_controls.BTN_Examine) {
        _controls.BTN_Examine->setOnClick([this]() {
            setMode(_mode == Mode::Buy ? Mode::Sell : Mode::Buy);
        });
    }
    for (auto &list : {_controls.LB_SHOPITEMS, _controls.LB_INVITEMS}) {
        if (list) {
            configureList(*list);
        }
    }
    if (_controls.LB_DESCRIPTION) {
        _controls.LB_DESCRIPTION->setProtoMatchContent(true);
    }
}

void StoreGUI::configureList(ListBox &list) {
    list.setSelectionMode(ListBox::SelectionMode::OnClick);
    list.setRenderItemIconsForButtonProto(true);
    if (auto proto = list.protoItemOrNull()) {
        Control::Text text(proto->text());
        text.align = Control::TextAlign::LeftCenter;
        proto->setText(text);
        proto->setHilightColor(_hilightColor);
    }
    list.setOnItemClick([this](const std::string &) {
        refreshSelection();
    });
    // Double-clicking an item trades it, like selecting it and pressing Accept.
    list.setOnItemDoubleClick([this](const std::string &) {
        refreshSelection();
        accept();
    });
}

bool StoreGUI::handle(const input::Event &event) {
    if (event.type == input::EventType::KeyDown && !event.key.repeat &&
        event.key.code == input::KeyCode::Escape) {
        close();
        return true;
    }
    return GameGUI::handle(event);
}

void StoreGUI::update(float dt) {
    GameGUI::update(dt);
    // The world keeps running while the store is open: rebuild when the
    // lists or the credits change underneath the screen.
    if (signatureOf(currentItems()) != _signature || _game.party().gold() != _shownGold) {
        refresh();
    }
}

void StoreGUI::open() {
    _mode = Mode::Buy;
    _shownGold = -1;
    _signature.clear();
    refresh();
}

void StoreGUI::close() {
    _game.closeStore();
}

void StoreGUI::setMode(Mode mode) {
    _mode = mode;
    _signature.clear();
    refresh();
}

ListBox *StoreGUI::activeList() const {
    return (_mode == Mode::Buy ? _controls.LB_SHOPITEMS : _controls.LB_INVITEMS).get();
}

std::vector<std::shared_ptr<Item>> StoreGUI::currentItems() const {
    const StoreSession &session = _game.storeSession();
    return _mode == Mode::Buy ? session.stock() : session.sellable();
}

std::vector<std::pair<uint32_t, int>> StoreGUI::signatureOf(const std::vector<std::shared_ptr<Item>> &items) const {
    std::vector<std::pair<uint32_t, int>> result;
    result.reserve(items.size());
    for (const auto &item : items) {
        result.emplace_back(item->id(), item->stackSize());
    }
    return result;
}

void StoreGUI::refresh() {
    const StoreSession &session = _game.storeSession();
    auto items = currentItems();
    auto previous = selectedItem();
    _signature = signatureOf(items);
    _shownItems = std::move(items);
    _shownGold = _game.party().gold();

    bool buy = _mode == Mode::Buy;
    if (_controls.LB_SHOPITEMS) {
        _controls.LB_SHOPITEMS->setVisible(buy);
    }
    if (_controls.LB_INVITEMS) {
        _controls.LB_INVITEMS->setVisible(!buy);
    }
    if (_controls.LBL_BUYSELL) {
        _controls.LBL_BUYSELL->setTextMessage(buy ? _buyText : _sellText);
    }
    if (_controls.BTN_Accept) {
        _controls.BTN_Accept->setTextMessage(buy ? _buyText : _sellText);
        auto store = session.store();
        bool allowed = store && (buy ? store->sellsToPlayer() : store->buysFromPlayer());
        _controls.BTN_Accept->setDisabled(!allowed);
    }
    if (_controls.BTN_Examine) {
        _controls.BTN_Examine->setTextMessage(buy ? _showSellText : _showBuyText);
    }
    if (_controls.LBL_CREDITS_VALUE) {
        _controls.LBL_CREDITS_VALUE->setTextMessage(std::to_string(_shownGold));
    }

    ListBox *list = activeList();
    if (list) {
        int offset = list->getItemOffset();
        list->clearItems();
        for (const auto &item : _shownItems) {
            ListBox::Item row;
            row.tag = std::to_string(item->id());
            row.text = item->localizedName();
            row.iconTexture = item->icon();
            row.iconFrame = itemFrameTexture(item->stackSize());
            if (item->stackSize() > 1 && !(buy && item->isInfinite())) {
                row.iconText = std::to_string(item->stackSize());
            }
            list->addItem(std::move(row));
        }
        list->setItemOffset(offset);
        int index = 0;
        if (previous) {
            auto it = std::find(_shownItems.begin(), _shownItems.end(), previous);
            if (it != _shownItems.end()) {
                index = static_cast<int>(std::distance(_shownItems.begin(), it));
            }
        }
        list->setSelectedItemIndex(_shownItems.empty() ? -1 : index);
    }
    _describedIndex = -2;
    refreshSelection();
}

std::shared_ptr<Item> StoreGUI::selectedItem() const {
    ListBox *list = activeList();
    if (!list) {
        return nullptr;
    }
    int index = list->selectedItemIndex();
    if (index < 0 || index >= static_cast<int>(_shownItems.size())) {
        return nullptr;
    }
    return _shownItems[index];
}

bool StoreGUI::selectItem(const std::shared_ptr<Item> &item) {
    ListBox *list = activeList();
    auto it = std::find(_shownItems.begin(), _shownItems.end(), item);
    if (!list || it == _shownItems.end()) {
        return false;
    }
    list->setSelectedItemIndex(static_cast<int>(std::distance(_shownItems.begin(), it)));
    refreshSelection();
    return true;
}

void StoreGUI::refreshSelection() {
    const StoreSession &session = _game.storeSession();
    ListBox *list = activeList();
    int index = list ? list->selectedItemIndex() : -1;
    if (index == _describedIndex) {
        return;
    }
    _describedIndex = index;
    auto item = selectedItem();
    bool buy = _mode == Mode::Buy;
    if (_controls.LBL_COST_VALUE) {
        _controls.LBL_COST_VALUE->setTextMessage(
            item ? std::to_string(buy ? session.buyPrice(*item) : session.sellPrice(*item)) : "");
    }
    if (_controls.LBL_STOCK_VALUE) {
        std::string stock;
        if (item && !(buy && item->isInfinite())) {
            stock = std::to_string(item->stackSize());
        }
        _controls.LBL_STOCK_VALUE->setTextMessage(stock);
    }
    if (_controls.LBL_ONHAND_VALUE) {
        // How many of the selected item the party holds (the K2 layout's
        // In Inventory line; it showed its authored placeholder before).
        int held = 0;
        if (item) {
            auto owner = _game.party().sharedInventoryReceiver(_game.party().getLeader());
            if (owner) {
                for (const auto &own : owner->items()) {
                    if (own && boost::iequals(own->blueprintResRef(), item->blueprintResRef())) {
                        held += own->stackSize();
                    }
                }
            }
        }
        _controls.LBL_ONHAND_VALUE->setTextMessage(item ? std::to_string(held) : std::string());
    }
    if (_controls.LB_DESCRIPTION) {
        _controls.LB_DESCRIPTION->clearItems();
        if (item) {
            _controls.LB_DESCRIPTION->addTextLinesAsItems(
                joinItemDescriptionLines(buildItemDescriptionLines(*item, _services)));
        }
    }
}

StoreSession::Result StoreGUI::accept() {
    auto item = selectedItem();
    if (!item) {
        return StoreSession::Result::NoSuchItem;
    }
    auto result = _mode == Mode::Buy ? _game.storeBuy(item) : _game.storeSell(item);
    refresh();
    return result;
}

} // namespace game

} // namespace reone
