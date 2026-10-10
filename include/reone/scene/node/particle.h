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

#include "../node.h"

namespace reone {

namespace scene {

class EmitterSceneNode;

class ParticleSceneNode : public SceneNode {
public:
    ParticleSceneNode(
        const EmitterSceneNode &emitter,
        ISceneGraph &sceneGraph,
        graphics::GraphicsServices &graphicsSvc,
        audio::AudioServices &audioSvc,
        resource::ResourceServices &resourceSvc) :
        SceneNode(
            SceneNodeType::Particle,
            sceneGraph,
            graphicsSvc,
            audioSvc,
            resourceSvc),
        _emitter(emitter) {

        _point = false;
    }

    void update(float dt);

    bool isExpired() const;

    /** World-space position. Particles are simulated in world space. */
    const glm::vec3 &position() const { return _position; }
    const glm::vec3 &dir() const { return _dir; }
    const glm::vec3 &color() const { return _color; }
    /** World-space velocity. */
    const glm::vec3 &velocity() const { return _velocity; }
    const glm::vec2 &size() const { return _size; }
    float animLength() const { return _animLength; }
    float lifetime() const { return _lifetime; }
    float alpha() const { return _alpha; }
    float rotation() const { return _rotation; }
    int frame() const { return _frame; }
    /** A splat particle that has landed: drawn flat on the ground. */
    bool isStuck() const { return _stuck; }

    void setDir(glm::vec3 dir) { _dir = std::move(dir); }
    void setVelocity(glm::vec3 velocity) { _velocity = std::move(velocity); }
    void setSize(glm::vec2 size) { _size = std::move(size); }
    void setColor(glm::vec3 color) { _color = std::move(color); }
    void setAlpha(float alpha) { _alpha = alpha; }
    void setFrame(int frame) { _frame = frame; }
    void setFrameOffset(int offset) { _frameOffset = offset; }
    void setAnimLength(float length) { _animLength = length; }
    void setLifetime(float lifetime) { _lifetime = lifetime; }
    void setRotation(float rotation) { _rotation = rotation; }

    /**
     * Place the particle at a world position and remember it as the start of
     * a point-to-point curve. Also keeps the scene-node transform in step.
     */
    void resetAt(const glm::vec3 &worldPosition);

    /**
     * Take the frame, size, colour and alpha of age 0. A particle born after
     * its emitter's update in a frame (an explosion burst fired by an
     * animation event) is drawn before its own first update, and must not be
     * drawn with its last life's state or the defaults (a white 1 m square).
     */
    void startLife() { updateAnimation(0.0f); }

    /** Move the particle by a world-space offset (inherit flags). */
    void translateBy(const glm::vec3 &offset);

    /** Move and turn the particle with its emitter (inherit flag). */
    void transformBy(const glm::mat4 &delta);

private:
    const EmitterSceneNode &_emitter;

    glm::vec3 _position {0.0f};
    glm::vec3 _birthPosition {0.0f};
    glm::vec3 _dir {0.0f}; // used in Linked render mode
    glm::vec3 _color {1.0f};
    glm::vec3 _velocity {0.0f};
    glm::vec2 _size {1.0f};
    float _animLength {0.0f};
    float _lifetime {0.0f};
    float _alpha {1.0f};
    float _rotation {0.0f};
    int _frame {0};
    int _frameOffset {0};
    bool _stuck {false};

    void syncTransform();
    void updateAnimation(float dt);
};

} // namespace scene

} // namespace reone
