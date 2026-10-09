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

#include "../action.h"

namespace reone {

namespace game {

class FollowLeaderAction : public Action {
public:
    /**
     * How close party members keep to the leader. Retail companions trail a
     * couple of metres behind (KotOR 2's follow keeps them within about
     * 2-3 m); 5 m left them noticeably far back.
     */
    static constexpr float kPartyFollowDistance = 2.5f;

    FollowLeaderAction(Game &game, ServicesView &services) :
        Action(game, services, ActionType::FollowLeader) {
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::FollowLeader;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;

    /// A follower that cannot reach the leader and is at least this far away,
    /// out of the leader's sight, rejoins at its formation slot.
    static constexpr float kRegroupDistance = 15.0f;
};

} // namespace game

} // namespace reone
