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

#include "reone/game/action/giveitem.h"

#include "reone/game/equipmentrules.h"

namespace reone {

namespace game {

void GiveItemAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // The actor hands over an item it carries. The receiver's OnDisturbed
    // (creature) or OnInventoryDisturbed (placeable) sees an ADDED item.
    if (_item && _giveTo && _item->owner() == actor.id()) {
        transferItemAndSignalDisturbed(_game, _item, *_giveTo, actor.id());
    }
    complete();
}

} // namespace game

} // namespace reone
