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

#include "runtimeref.h"

namespace reone {

namespace game {

class Creature;
class Game;
class Item;
class Object;
class Store;

/**
 * The merchant a party is trading with (OpenStore), and the one place that
 * buys and sells. Both the store screen's buttons and the harness `store`
 * command act through it (design note gui_screens, "Store").
 */
class StoreSession {
public:
    enum class Result {
        Ok,
        NotOpen,
        NotAllowed,       // this player may not transact (co-op: host only)
        StoreDoesNotSell, // BuySellFlag bit 0 clear
        StoreDoesNotBuy,  // BuySellFlag bit 1 clear
        NotEnoughGold,
        NoSuchItem,
        NotSellable       // plot, equipped or worthless
    };

    explicit StoreSession(Game &game) :
        _game(game) {
    }

    /** Unit value of an item: 0 for plot items, else its cost, at least 1. */
    static int itemValue(const Item &item, float costMultiplier = 1.0f);
    /** itemValue with the party's ChangeItemCost multiplier for its base item. */
    int partyItemValue(const Item &item) const;
    /** value x percent / 100, integer division, never below 0. */
    static int applyPercent(int value, int percent);
    static const char *describe(Result result);

    void open(const std::shared_ptr<Store> &store, const std::shared_ptr<Creature> &customer,
              int bonusMarkUp, int bonusMarkDown);
    void close();

    bool isOpen() const;
    std::shared_ptr<Store> store() const;
    std::shared_ptr<Creature> customer() const;
    int bonusMarkUp() const { return _bonusMarkUp; }
    int bonusMarkDown() const { return _bonusMarkDown; }

    /** What the store asks for one unit. */
    int buyPrice(const Item &item) const;
    /** What the store pays for one unit. */
    int sellPrice(const Item &item) const;

    /** The store's items, in its own order. */
    std::vector<std::shared_ptr<Item>> stock() const;
    /** The party items the store would buy. */
    std::vector<std::shared_ptr<Item>> sellable() const;
    bool isSellable(const Item &item) const;

    /**
     * Whether this creature's player may buy and sell. Co-op rule 4: only
     * the host transacts. Every transaction asks here.
     */
    bool mayTransact(const Creature *requester) const;

    /** Buy one unit of a store item. */
    Result buy(const std::shared_ptr<Item> &storeItem, const Creature *requester = nullptr);
    /** Sell one unit of a party item. */
    Result sell(const std::shared_ptr<Item> &partyItem, const Creature *requester = nullptr);

private:
    Game &_game;
    RuntimeObjectRef<Store> _store;
    RuntimeObjectRef<Creature> _customer;
    int _bonusMarkUp {0};
    int _bonusMarkDown {0};

    std::shared_ptr<Object> partyInventory() const;
};

} // namespace game

} // namespace reone
