/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#include "reone/game/gui/container.h"

#include "reone/gui/control/imagebutton.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/strings.h"
#include "reone/system/exception/validation.h"

#include "reone/game/di/services.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/coopsession.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"

using namespace reone::audio;

using namespace reone::gui;
using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kSwitchToResRef = 47884;
static constexpr int kGetItemsResRef = 38542;
static constexpr int kGiveItemResRef = 38543;
static constexpr int kInventoryResRef = 393;

void ContainerGUI::onGUILoaded() {
    bindControls();
    centerRootInCanvas(isTSL() ? 800 : 640, isTSL() ? 600 : 480);

    _giveItemMsg = _services.resource.strings.getText(kSwitchToResRef) + " " + _services.resource.strings.getText(kGiveItemResRef);
    _getItemsMsg = _services.resource.strings.getText(kSwitchToResRef) + " " + _services.resource.strings.getText(kGetItemsResRef);

    _controls.BTN_GIVEITEMS->setTextMessage(_giveItemMsg);

    std::string LBL_MESSAGE(_services.resource.strings.getText(kInventoryResRef));
    _controls.LBL_MESSAGE->setTextMessage(LBL_MESSAGE);

    _controls.BTN_OK->setOnClick([this]() {
        takeAll();
    });
    _controls.BTN_CANCEL->setOnClick([this]() {
        close();
    });
    _controls.BTN_GIVEITEMS->setOnClick([this]() {
        switchMode();
    });

    configureItemsListBox();
}

void ContainerGUI::configureItemsListBox() {
    ImageButton &protoItem = static_cast<ImageButton &>(_controls.LB_ITEMS->protoItem());

    Control::Text text(protoItem.text());
    // Centre the name on the scaled glyphs; authored top alignment assumed
    // the retail font filled the row.
    text.align = Control::TextAlign::LeftCenter;

    protoItem.setText(text);

    _controls.LB_ITEMS->setOnItemDoubleClick([this](const std::string &tag) {
        onItemDoubleClick(tag);
    });
}

std::vector<std::pair<uint32_t, int>> ContainerGUI::itemSignature(
    Object &source, bool onlyDropable, bool skipCredits) const {
    std::vector<std::pair<uint32_t, int>> result;
    for (auto &item : source.items()) {
        if ((onlyDropable && !item->isDropable()) || (skipCredits && item->isCredits())) {
            continue;
        }
        result.emplace_back(item->id(), item->stackSize());
    }
    return result;
}

void ContainerGUI::update(float dt) {
    GameGUI::update(dt);
    Object *source = nullptr;
    bool onlyDropable = true;
    bool skipCredits = false;
    if (_mode == Mode::ContainerToPlayer) {
        auto container = _container.resolve();
        if (!container) {
            return;
        }
        source = container.get();
    } else {
        auto leader = _game.party().getLeader();
        if (!leader) {
            return;
        }
        source = leader.get();
        onlyDropable = false;
        skipCredits = true;
    }
    if (itemSignature(*source, onlyDropable, skipCredits) != _shownItems) {
        populateItems(*source, onlyDropable, skipCredits);
    }
}

void ContainerGUI::populateItems(Object &source, bool onlyDropable, bool skipCredits) {
    _shownItems = itemSignature(source, onlyDropable, skipCredits);
    _controls.LB_ITEMS->clearItems();
    for (auto &item : source.items()) {
        if (onlyDropable && !item->isDropable()) {
            continue;
        }

        if (skipCredits && item->isCredits()) {
            continue;
        }

        ListBox::Item lbItem;
        lbItem.tag = item->tag();
        lbItem.text = item->localizedName();
        lbItem.iconTexture = item->icon();
        lbItem.iconFrame = itemFrameTexture(item->stackSize());
        if (item->stackSize() > 1) {
            lbItem.iconText = std::to_string(item->stackSize());
        }

        _controls.LB_ITEMS->addItem(std::move(lbItem));
    }
}

void ContainerGUI::open(std::shared_ptr<Object> container) {
    _controls.BTN_GIVEITEMS->setTextMessage(_giveItemMsg);
    // A co-op client has no party inventory of its own to give from.
    auto *coop = _game.coop();
    _controls.BTN_GIVEITEMS->setVisible(!(coop && coop->isClient()));
    _container = container;
    _mode = Mode::ContainerToPlayer;
    populateItems(*container, /*onlyDropable=*/true, /*skipCredits=*/false);

    auto placeable = dyn_cast<Placeable>(container);
    if (placeable) {
        placeable->openContainer(_game.party().getLeader()->id());
    }
}

Object &ContainerGUI::container() const {
    auto container = _container.resolve();
    if (!container) {
        throw ValidationException("Container is no longer a live runtime object");
    }
    return *container;
}

void ContainerGUI::close() {
    // A co-op client tells the host, which owns the container.
    if (auto *coop = _game.coop(); coop && coop->clientContainerClose()) {
        return;
    }
    // Leaving the screen shuts the container; its OnClosed runs in the tick.
    if (auto container = _container.resolve()) {
        if (auto placeable = dyn_cast<Placeable>(container)) {
            if (auto leader = _game.party().getLeader()) {
                placeable->signalClosed(leader->id());
            }
        }
    }
    _game.openInGame();
}

void ContainerGUI::switchMode() {
    if (auto *coop = _game.coop(); coop && coop->isClient()) {
        return;
    }
    switch (_mode) {
    case Mode::ContainerToPlayer: {
        _mode = Mode::PlayerToContainer;
        _controls.BTN_GIVEITEMS->setTextMessage(_getItemsMsg);
        populateItems(*_game.party().getLeader(), /*onlyDropable=*/false, /*skipCredits=*/true);
        break;
    }
    case Mode::PlayerToContainer: {
        _mode = Mode::ContainerToPlayer;
        _controls.BTN_GIVEITEMS->setTextMessage(_giveItemMsg);
        auto container = _container.resolve();
        if (!container) {
            close();
            return;
        }
        populateItems(*container, /*onlyDropable=*/true, /*skipCredits=*/false);
        break;
    }
    }
}

void ContainerGUI::takeAll() {
    // A co-op client asks the host: the first request for an item wins.
    if (auto *coop = _game.coop(); coop && coop->clientContainerTakeAll()) {
        return;
    }
    transferItemsToPlayer();
}

void ContainerGUI::transferItemsToPlayer() {
    auto container = _container.resolve();
    if (!container) {
        close();
        return;
    }
    std::shared_ptr<Creature> player = _game.party().player();
    // A creature container sees one REMOVED event per item taken. Credits
    // fold into the party gold and stop existing, so they report no item.
    std::vector<uint32_t> takenIds;
    auto creature = dyn_cast<Creature>(container);
    if (creature) {
        for (const auto &item : container->items()) {
            if (item->isDropable()) {
                takenIds.push_back(item->isCredits() ? script::kObjectInvalid : item->id());
            }
        }
    }
    container->moveDropableItemsTo(*player);

    auto placeable = dyn_cast<Placeable>(container);
    if (placeable) {
        placeable->runOnInvDisturbed(player->id(), InventoryDisturbType::Removed, script::kObjectInvalid);
    }
    if (creature) {
        for (uint32_t itemId : takenIds) {
            creature->signalInventoryDisturbed(InventoryDisturbType::Removed, player->id(), itemId);
        }
    }

    close();
}

void ContainerGUI::onItemDoubleClick(const std::string &tag) {
    if (_mode == Mode::ContainerToPlayer) {
        // Do nothing for the player for now.
        return;
    }
    auto container = _container.resolve();
    if (!container) {
        close();
        return;
    }

    std::shared_ptr<Creature> player = _game.party().player();
    std::shared_ptr<Item> item = player->getItemByTag(tag);
    if (!item) {
        return;
    }

    giveItemToContainer(_game, *player, item, *container);

    // Repopulate the list after the number of items changes.
    int offset = _controls.LB_ITEMS->getItemOffset();
    populateItems(*player, /*onlyDropable=*/false, /*skipCredits=*/true);

    // Try to keep scroll offset the same.
    _controls.LB_ITEMS->setItemOffset(offset);
}

} // namespace game

} // namespace reone
