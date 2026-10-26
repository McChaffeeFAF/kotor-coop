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

#include "reone/game/combatqueue.h"

#include "reone/game/action.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/action/usetalentonobject.h"
#include "reone/game/d20/spell.h"
#include "reone/game/object.h"

namespace reone {

namespace game {

namespace combatqueue {

net::QueueKind kindForActionType(ActionType type) {
    switch (type) {
    case ActionType::AttackObject:
        return net::kQueueAttack;
    case ActionType::SwitchWeapons:
        return net::kQueueSwap;
    case ActionType::UseFeat:
        return net::kQueueFeat;
    case ActionType::CastSpellAtObject:
        return net::kQueueSpell;
    default:
        return net::kQueueOther;
    }
}

net::QueueEntry entryFor(const Action &action) {
    // A talent the party AI picked is a dispatcher: show what it runs.
    if (action.type() == ActionType::UseTalentOnObject) {
        const auto &sub = static_cast<const UseTalentOnObjectAction &>(action).subAction();
        if (sub) {
            return entryFor(*sub);
        }
        return {net::kQueueOther, 0};
    }
    net::QueueEntry entry;
    entry.kind = kindForActionType(action.type());
    if (entry.kind == net::kQueueFeat) {
        entry.param = static_cast<uint16_t>(static_cast<const UseFeatAction &>(action).feat());
    } else if (entry.kind == net::kQueueSpell) {
        const auto &spell = static_cast<const CastSpellAtObjectAction &>(action).spell();
        entry.param = spell ? static_cast<uint16_t>(spell->type) : 0;
    }
    return entry;
}

std::vector<net::QueueEntry> entriesOf(const Object &object) {
    std::vector<net::QueueEntry> entries;
    for (const auto &action : object.actions()) {
        if (entries.size() >= net::kMaxQueueEntries) {
            break;
        }
        entries.push_back(entryFor(*action));
    }
    return entries;
}

} // namespace combatqueue

} // namespace game

} // namespace reone
