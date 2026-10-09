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

#include "reone/scene/node/particle.h"

#include "reone/graphics/modelnode.h"

#include "reone/scene/node/emitter.h"

using namespace reone::graphics;

namespace reone {

namespace scene {

static constexpr float kGravity = -9.81f; // m/s^2, scaled by the emitter's mass

void ParticleSceneNode::syncTransform() {
    // Particles are simulated in world space. The scene-node transform is kept
    // in step so origin() and culling see the same point.
    if (_parent) {
        setLocalTransform(_parent->absoluteTransformInverse() * glm::translate(_position));
    } else {
        setLocalTransform(glm::translate(_position));
    }
}

void ParticleSceneNode::resetAt(const glm::vec3 &worldPosition) {
    _position = worldPosition;
    _birthPosition = worldPosition;
    _stuck = false;
    _rotation = 0.0f;
    _frameOffset = 0;
    syncTransform();
}

void ParticleSceneNode::translateBy(const glm::vec3 &offset) {
    _position += offset;
    _birthPosition += offset;
}

void ParticleSceneNode::transformBy(const glm::mat4 &delta) {
    _position = glm::vec3(delta * glm::vec4(_position, 1.0f));
    _birthPosition = glm::vec3(delta * glm::vec4(_birthPosition, 1.0f));
    _velocity = glm::mat3(delta) * _velocity;
}

void ParticleSceneNode::update(float dt) {
    // Lightning emitters are updated elsewhere
    auto &modelNode = _emitter.modelNode();
    auto &emitter = *modelNode.emitter();
    if (emitter.updateMode == ModelNode::Emitter::UpdateMode::Lightning) {
        return;
    }

    if (_emitter.lifeExpectancy() != -1.0f) {
        _lifetime = glm::min(_lifetime + dt, _emitter.lifeExpectancy());
        if (isExpired()) {
            return;
        }
    } else if (_lifetime == _animLength) {
        _lifetime = 0.0f;
    } else {
        _lifetime = glm::min(_lifetime + dt, _animLength);
    }

    if (!_stuck) {
        std::optional<glm::vec3> target;
        if (modelNode.emitter()->p2p) {
            target = _emitter.p2pTarget();
        }
        if (target && emitter.p2pSel && _emitter.lifeExpectancy() > 0.0f) {
            // Curve mode: travel from the birth point to the target over the
            // particle's life, bent by the two control distances.
            float t = glm::clamp(_lifetime / _emitter.lifeExpectancy(), 0.0f, 1.0f);
            glm::vec3 p0 = _birthPosition;
            glm::vec3 p3 = *target;
            glm::vec3 emitterAxis = glm::normalize(glm::vec3(_emitter.absoluteTransform()[2]) + glm::vec3(0.0f, 0.0f, 1e-6f));
            glm::vec3 p1 = p0 + emitterAxis * _emitter.p2pBezier2();
            glm::vec3 p2 = p3 + _emitter.p2pTargetAxis() * _emitter.p2pBezier3();
            float u = 1.0f - t;
            glm::vec3 next = u * u * u * p0 + 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 + t * t * t * p3;
            if (dt > 0.0f) {
                _velocity = (next - _position) / dt;
            }
            _position = next;
        } else {
            if (target) {
                // Gravity mode: pulled toward the target with strength grav.
                glm::vec3 toTarget = *target - _position;
                float distance = glm::length(toTarget);
                if (distance > 1e-4f) {
                    _velocity += _emitter.grav() * (toTarget / distance) * dt;
                }
            }
            _velocity.z += _emitter.mass() * kGravity * dt;
            if (_emitter.drag() > 0.0f) {
                _velocity *= glm::max(0.0f, 1.0f - _emitter.drag() * dt);
            }
            _position += _velocity * dt;
            if (emitter.affectedByWind) {
                _position += _emitter.windVelocity() * dt;
            }
            auto ground = _emitter.groundHeight();
            if (emitter.bounce && ground && _position.z < *ground && _velocity.z < 0.0f) {
                _position.z = *ground;
                if (emitter.splat) {
                    _stuck = true;
                    _velocity = glm::vec3(0.0f);
                } else if (_emitter.bounceCo() <= 0.0f) {
                    _velocity = glm::vec3(0.0f);
                } else {
                    // Reflect off the (flat) ground; only the rebound is damped.
                    _velocity.z = -_velocity.z * _emitter.bounceCo();
                }
            }
        }
        _rotation += _emitter.particleRot() * dt;
    }
    syncTransform();

    updateAnimation(dt);
}

void ParticleSceneNode::updateAnimation(float dt) {
    float factor;
    if (_emitter.lifeExpectancy() != -1.0f) {
        factor = _lifetime / static_cast<float>(_emitter.lifeExpectancy());
    } else if (_animLength > 0.0f) {
        factor = _lifetime / _animLength;
    } else {
        factor = 0.0f;
    }

    // A zero authored frame rate is a static atlas selection in Odyssey. It
    // does not mean that the frame range should be spread over particle life.
    int frameStart = _emitter.frameStart();
    int frameEnd = _emitter.frameEnd();
    int frame;
    if (_animLength > 0.0f) {
        frame = static_cast<int>(glm::ceil(frameStart + factor * (frameEnd - frameStart)));
    } else {
        frame = frameStart;
    }
    if (_frameOffset != 0) {
        // Random start frame: animate on from it, wrapping inside the range.
        int count = glm::abs(frameEnd - frameStart) + 1;
        int base = glm::min(frameStart, frameEnd);
        frame = base + ((frame - base + _frameOffset) % count + count) % count;
    }
    _frame = frame;
    _size = glm::vec2(_emitter.getParticleSize(factor), _emitter.getParticleSizeY(factor));
    _color = _emitter.getColor(factor);
    _alpha = _emitter.getAlpha(factor);
}

bool ParticleSceneNode::isExpired() const {
    return _lifetime >= _emitter.lifeExpectancy();
}

} // namespace scene

} // namespace reone
