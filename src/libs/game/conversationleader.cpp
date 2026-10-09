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

#include "reone/game/conversationleader.h"

#include <algorithm>

#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"

namespace reone {

namespace game {

static bool allowsPartyInteract(const Object &object) {
    if (auto creature = dyn_cast<Creature>(&object)) {
        return creature->isPartyInteract();
    }
    if (auto placeable = dyn_cast<Placeable>(&object)) {
        return placeable->isPartyInteract();
    }
    return false;
}

bool handConversationToPlayer(Game &game, const std::shared_ptr<Object> &speaker, const std::shared_ptr<Object> &other) {
    auto &party = game.party();
    auto companion = dyn_cast<Creature>(speaker);
    auto pc = party.actualPlayer();
    if (!companion || !pc || !other || companion == pc || companion != party.getLeader() || pc->isDead()) {
        return false;
    }
    if (party.isMember(*other) || allowsPartyInteract(*other)) {
        return false;
    }
    auto module = game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) {
        return false;
    }
    const auto &creatures = area->getObjectsByType(ObjectType::Creature);
    if (std::none_of(creatures.begin(), creatures.end(), [&pc](const auto &object) { return object == pc; })) {
        return false;
    }
    glm::vec3 companionPosition(companion->position());
    float companionFacing = companion->getFacing();
    companion->setPosition(pc->position());
    companion->setFacing(pc->getFacing());
    pc->setPosition(companionPosition);
    pc->setFacing(companionFacing);
    area->determineObjectRoom(*pc);
    area->determineObjectRoom(*companion);
    party.setPartyLeader(kNpcPlayer);
    party.setPostDialogSwitchBack(companion->id());
    return true;
}

void switchBackAfterConversation(Game &game) {
    auto &party = game.party();
    uint32_t companionId = party.postDialogSwitchBack();
    if (!companionId) {
        return;
    }
    party.setPostDialogSwitchBack(0);
    const auto &members = party.members();
    for (int i = 1; i < static_cast<int>(members.size()); ++i) {
        const auto &creature = members[i].creature;
        if (creature && creature->id() == companionId && !creature->isDead()) {
            party.setPartyLeaderByIndex(i);
            return;
        }
    }
}

} // namespace game

} // namespace reone
