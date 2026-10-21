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

#include <optional>
#include <string>

namespace reone {

namespace scene {
class ModelSceneNode;
class ISceneGraph;
} // namespace scene

namespace graphics {
class Model;
}

namespace game {

class Action;
struct CombatRound;
class Creature;
class Object;

/**
 * The creature clips of a Force power's cast, from its spells.2da conjanim
 * and castanim (design note v14_programmed_effects, section 9). K1's
 * supermodel has three cast poses, each a short raise into the pose
 * (castoutN, 0.33 s) and a held loop (castoutlpN): 1 a crouch with clawed
 * hands ("dark"), 2 one arm out, palm open ("self", most powers), 3 an arm
 * raised high ("up"). "throw" is the saber throw (throwsab, throwsablp).
 */
struct CastAnimations {
    std::string conjure; /**< played once at the start of the conjure time */
    std::string cast;    /**< looped through the cast time */
};

/** The clips for a spells.2da conjanim/castanim pair, or none ("jump", "monster", empty). */
std::optional<CastAnimations> forceCastAnimations(const std::string &conjAnim, const std::string &castAnim);

/**
 * What CastSpellAtObject plays: a grenade is thrown (throwgren), an item is
 * used (activate when it targets, inject otherwise), anything else - a Force
 * power or ability - takes its forceCastAnimations, or the item clips when
 * spells.2da names no pose. cast is empty when nothing is held.
 */
CastAnimations castActionAnimations(const std::string &conjAnim, const std::string &castAnim,
                                    bool fromItem, bool grenade, bool itemTargeting);

class SpellSchedule {
public:
    explicit SpellSchedule(float conjTime, float castTime) :
        _conjTime(conjTime), _castTime(castTime) {}

    enum State {
        WaitConjure,
        Conjure,
        WaitCast,
        Cast,
        WaitEffect,
        Effect,
        WaitFinish,
        Finish,
    };

    State update(const CombatRound &round, Action &action, float dt);

private:
    State _state {WaitConjure};
    float _time {0.0f};
    float _conjTime {0.0f};
    float _castTime {0.0f};
};

class Grenade {
public:
    ~Grenade();

    enum State {
        Swing,
        Throw,
        Wait,
        Explode,
        Finish,
    };

    void fire(Creature &caster, graphics::Model &projModel, float swingTime, float throwTime);
    void update(SpellSchedule::State spellState, Object &target, float dt);

private:
    // Compute parameters for a parabolic trajectory from origin to target.
    void computeTrajectory(glm::vec3 origin, glm::vec3 target, float throwTime);

private:
    State _state {Swing};
    std::shared_ptr<scene::ModelSceneNode> _projNode;
    float _time {0.0f};
    float _swingTime {0.0f};
    float _throwTime {0.0f};
    glm::vec3 _throwOrigin {0.0f};
    glm::vec3 _throwTarget {0.0f};
    glm::vec3 _throwVelocity {0.0f};
    glm::vec3 _throwAccel {0.0f};
};

} // namespace game
} // namespace reone
