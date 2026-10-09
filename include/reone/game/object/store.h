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

#pragma once

#include "../object.h"
#include "reone/resource/gff.h"
#include "reone/resource/strings.h"

namespace reone {

namespace game {

class Store : public Object {
public:
    Store(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Object(
            id,
            ObjectType::Store,
            std::move(sceneName),
            game,
            services) {
    }

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Store;
    }

    void deserialize(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);

    // BuySellFlag bits: the store sells to the player / buys from the player.
    static constexpr uint8_t kSellsToPlayer = 1;
    static constexpr uint8_t kBuysFromPlayer = 2;

    int markUp() const { return _markUp; }
    int markDown() const { return _markDown; }
    const std::string &onOpenStore() const { return _onOpenStore; }
    bool sellsToPlayer() const { return (_buySellFlag & kSellsToPlayer) != 0; }
    bool buysFromPlayer() const { return (_buySellFlag & kBuysFromPlayer) != 0; }

    void setMarkUp(int markUp) { _markUp = markUp; }
    void setMarkDown(int markDown) { _markDown = markDown; }
    void setBuySellFlag(uint8_t flag) { _buySellFlag = flag; }

private:
    friend class ModuleSnapshotBuilder;
    // Serializable
    resource::LocString _locName;
    int32_t _markDown {100};
    int32_t _markUp {100};
    std::string _onOpenStore;
    uint8_t _buySellFlag {kSellsToPlayer | kBuysFromPlayer};
    // END Serializable

    void deserializeAll(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    void deserializeItems(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
};

} // namespace game

} // namespace reone
