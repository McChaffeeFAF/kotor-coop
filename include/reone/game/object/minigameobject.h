/*
 * Copyright (c) 2020-2026 The reone project contributors
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

namespace reone {

namespace game {

/**
 * The script identity of one minigame actor (player, enemy or obstacle). Its
 * state lives in the MiniGameSession; this object is what scripts see as
 * OBJECT_SELF, what DelayCommand queues on, and what the SWMG_ routines
 * resolve their object arguments from.
 */
class MiniGameObject : public Object {
public:
    MiniGameObject(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Object(
            id,
            ObjectType::MiniGame,
            std::move(sceneName),
            game,
            services) {
    }

    static bool classof(const Object *from) {
        return from->type() == ObjectType::MiniGame;
    }

    size_t actorIndex() const { return _actorIndex; }
    void setActorIndex(size_t index) { _actorIndex = index; }

private:
    size_t _actorIndex {0};
};

} // namespace game

} // namespace reone
