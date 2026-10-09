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

#include "../action.h"
#include "../object/item.h"
#include "../traps.h"

namespace reone {

namespace game {

/**
 * Disable, recover, flag or examine a trap (ActionType DisableTrap,
 * RecoverTrap, FlagTrap, ExamineTrap). Not saved: a pending trap action is
 * dropped by a save, as a user click would be.
 */
class TrapSkillAction : public Action {
public:
    TrapSkillAction(Game &game,
                    ServicesView &services,
                    ActionType type,
                    std::shared_ptr<Object> trap) :
        Action(game, services, type),
        _trap(std::move(trap)) {
        requireRuntimeObject(_trap);
    }

    static bool classof(Action *from) {
        auto type = from->type();
        return type == ActionType::DisableTrap || type == ActionType::RecoverTrap ||
               type == ActionType::FlagTrap || type == ActionType::ExamineTrap;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    bool isEphemeral() const override { return true; }

    static TrapSkill skillFor(ActionType type);

private:
    std::shared_ptr<Object> _trap;
};

/** Lays a mine item at a point (ActionType SetTrap). Not saved. */
class SetTrapAction : public Action {
public:
    SetTrapAction(Game &game,
                  ServicesView &services,
                  std::shared_ptr<Item> mine,
                  glm::vec3 position) :
        Action(game, services, ActionType::SetTrap),
        _mine(std::move(mine)),
        _position(std::move(position)) {
        requireRuntimeObject(_mine);
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::SetTrap;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    bool isEphemeral() const override { return true; }

private:
    std::shared_ptr<Item> _mine;
    glm::vec3 _position;
    float _setTime {0.0f};
    bool _reached {false};
};

} // namespace game

} // namespace reone
