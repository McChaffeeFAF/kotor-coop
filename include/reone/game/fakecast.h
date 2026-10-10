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

#include <glm/vec3.hpp>
#include <memory>

#include "reone/game/castspell.h"
#include "reone/game/visualeffectpresenter.h"

namespace reone {

namespace game {

class Creature;
struct ServicesView;
// Declared the way castspellatobject.h does, so the mangled names agree.
class Spell;

/**
 * ActionCastFakeSpellAt*: the presentation of a Force power with nothing
 * else. The caster faces the target, holds still, plays the conjure clip for
 * the power's conjtime and the cast clip (with castsound) for its casttime.
 * No script runs, no Force points are spent and nothing is recorded.
 */
class FakeCast {
public:
    ~FakeCast();

    /** Advance by dt. Returns true once the cast is over. */
    bool update(Creature &caster, const Spell *spell, const glm::vec3 &target, ServicesView &services, float dt);

    /** Release the caster if the action ends early. */
    void cancel(Creature &caster);

private:
    enum class Stage {
        Start,
        Conjure,
        Cast,
        Done
    };

    Stage _stage {Stage::Start};
    float _elapsed {0.0f};
    std::shared_ptr<VisualEffectPresenter::Request> _castHand; /**< spells.2da casthandvisual */
    std::optional<CastAnimations> _anims;                      /**< from spells.2da conjanim/castanim */

    void finish(Creature &caster);
};

} // namespace game

} // namespace reone
