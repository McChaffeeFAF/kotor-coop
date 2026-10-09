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

#include "reone/game/storesession.h"

#include "reone/game/coopsession.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/store.h"
#include "reone/game/party.h"

namespace reone {

namespace game {

int StoreSession::itemValue(const Item &item, float costMultiplier) {
    if (item.plotFlag()) {
        return 0;
    }
    // The item's cost (the template's computed Cost, which already holds
    // AddCost), scaled by the party's per-base-item cost multiplier that
    // ChangeItemCost sets (1.0 until then; design note c11_data).
    uint32_t cost = item.cost() > 0 ? item.cost() : item.addCost();
    auto scaled = static_cast<long long>(std::llround(static_cast<double>(cost) * costMultiplier));
    return static_cast<int>(std::clamp<long long>(scaled, 1, std::numeric_limits<int>::max()));
}

int StoreSession::partyItemValue(const Item &item) const {
    return itemValue(item, _game.party().itemCostMultiplier(item.baseItemType()));
}

int StoreSession::applyPercent(int value, int percent) {
    if (value <= 0 || percent <= 0) {
        return 0;
    }
    return static_cast<int>(static_cast<int64_t>(value) * percent / 100);
}

const char *StoreSession::describe(Result result) {
    switch (result) {
    case Result::Ok:
        return "ok";
    case Result::NotOpen:
        return "no store is open";
    case Result::NotAllowed:
        return "only the host may trade";
    case Result::StoreDoesNotSell:
        return "the store does not sell";
    case Result::StoreDoesNotBuy:
        return "the store does not buy";
    case Result::NotEnoughGold:
        return "not enough credits";
    case Result::NoSuchItem:
        return "no such item";
    case Result::NotSellable:
        return "the store will not buy that item";
    }
    return "unknown";
}

void StoreSession::open(const std::shared_ptr<Store> &store, const std::shared_ptr<Creature> &customer,
                        int bonusMarkUp, int bonusMarkDown) {
    _store = store;
    _customer = customer;
    _bonusMarkUp = bonusMarkUp;
    _bonusMarkDown = bonusMarkDown;
}

void StoreSession::close() {
    _store.reset();
    _customer.reset();
    _bonusMarkUp = 0;
    _bonusMarkDown = 0;
}

bool StoreSession::isOpen() const {
    return static_cast<bool>(_store.resolve());
}

std::shared_ptr<Store> StoreSession::store() const {
    return _store.resolve();
}

std::shared_ptr<Creature> StoreSession::customer() const {
    return _customer.resolve();
}

int StoreSession::buyPrice(const Item &item) const {
    auto store = _store.resolve();
    int markUp = (store ? store->markUp() : 100) + _bonusMarkUp;
    return applyPercent(partyItemValue(item), markUp);
}

int StoreSession::sellPrice(const Item &item) const {
    auto store = _store.resolve();
    int markDown = (store ? store->markDown() : 100) + _bonusMarkDown;
    return applyPercent(partyItemValue(item), markDown);
}

std::vector<std::shared_ptr<Item>> StoreSession::stock() const {
    auto store = _store.resolve();
    if (!store) {
        return {};
    }
    return store->items();
}

std::shared_ptr<Object> StoreSession::partyInventory() const {
    auto player = _game.party().actualPlayer();
    if (!player) {
        return nullptr;
    }
    return _game.party().sharedInventoryReceiver(player);
}

bool StoreSession::isSellable(const Item &item) const {
    return !item.isEquipped() && !item.plotFlag() && !item.isCredits() && partyItemValue(item) > 0;
}

std::vector<std::shared_ptr<Item>> StoreSession::sellable() const {
    std::vector<std::shared_ptr<Item>> result;
    auto inventory = partyInventory();
    if (!inventory) {
        return result;
    }
    for (const auto &item : inventory->items()) {
        if (item && isSellable(*item)) {
            result.push_back(item);
        }
    }
    return result;
}

bool StoreSession::mayTransact(const Creature *requester) const {
    // Co-op rule 4: inventory and credits are shared, and only the host buys
    // and sells. With no networked players the local player is the host, so
    // any request made through this game instance on behalf of a party
    // member (or with no named requester) is the host's.
    if (!requester) {
        return true;
    }
    return _game.party().isMember(*requester);
}

StoreSession::Result StoreSession::buy(const std::shared_ptr<Item> &storeItem, const Creature *requester) {
    if (auto *coop = _game.coop(); coop && coop->isClient() && storeItem && coop->clientStoreBuy(storeItem->id())) {
        // Co-op Free Mode: the host sells to the guest and sends the result.
        return Result::Ok;
    }
    auto store = _store.resolve();
    if (!store) {
        return Result::NotOpen;
    }
    if (!mayTransact(requester)) {
        return Result::NotAllowed;
    }
    if (!store->sellsToPlayer()) {
        return Result::StoreDoesNotSell;
    }
    const auto &items = store->items();
    if (!storeItem || std::find(items.begin(), items.end(), storeItem) == items.end()) {
        return Result::NoSuchItem;
    }
    auto inventory = partyInventory();
    if (!inventory) {
        return Result::NoSuchItem;
    }
    int price = buyPrice(*storeItem);
    if (_game.party().gold() < price) {
        return Result::NotEnoughGold;
    }

    _game.party().takeGold(price);
    auto bought = _game.newItemClone(*storeItem);
    bought->setStackSize(1);
    bought->setInfinite(false);
    bought->setOwner(0);
    inventory->addItem(bought);
    // The new unit may have merged into a stack the party already had.
    std::shared_ptr<Item> held;
    const auto &owned = inventory->items();
    if (std::find(owned.begin(), owned.end(), bought) != owned.end()) {
        held = bought;
    } else {
        auto it = std::find_if(owned.begin(), owned.end(), [&bought](const std::shared_ptr<Item> &item) {
            return item && item->isStackCompatibleWith(*bought);
        });
        if (it != owned.end()) {
            held = *it;
        }
    }
    if (!storeItem->isInfinite()) {
        bool last = false;
        store->removeItem(storeItem, last);
        if (last) {
            _game.destroyRuntimeObjectGraph(storeItem);
        }
    }
    if (auto module = _game.module(); module && held) {
        module->raiseItemAcquired(*held, store->id());
    }
    return Result::Ok;
}

StoreSession::Result StoreSession::sell(const std::shared_ptr<Item> &partyItem, const Creature *requester) {
    if (auto *coop = _game.coop(); coop && coop->isClient() && partyItem && coop->clientStoreSell(partyItem->id())) {
        return Result::Ok;
    }
    auto store = _store.resolve();
    if (!store) {
        return Result::NotOpen;
    }
    if (!mayTransact(requester)) {
        return Result::NotAllowed;
    }
    if (!store->buysFromPlayer()) {
        return Result::StoreDoesNotBuy;
    }
    auto inventory = partyInventory();
    if (!inventory || !partyItem) {
        return Result::NoSuchItem;
    }
    const auto &items = inventory->items();
    if (std::find(items.begin(), items.end(), partyItem) == items.end()) {
        return Result::NoSuchItem;
    }
    if (!isSellable(*partyItem)) {
        return Result::NotSellable;
    }
    int price = sellPrice(*partyItem);

    auto sold = _game.newItemClone(*partyItem);
    sold->setStackSize(1);
    sold->setInfinite(false);
    sold->setOwner(0);
    store->addItem(sold);

    bool last = false;
    inventory->removeItem(partyItem, last);
    if (auto module = _game.module()) {
        module->raiseItemLost(*partyItem, inventory->id());
    }
    if (last) {
        _game.destroyRuntimeObjectGraph(partyItem);
    }
    _game.party().giveGold(price);
    return Result::Ok;
}

} // namespace game

} // namespace reone
