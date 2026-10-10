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

#include "reone/scene/node/emitter.h"

#include "reone/scene/interp.h"

#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/uniforms.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/textures.h"
#include "reone/scene/collision.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/camera.h"
#include "reone/scene/node/particle.h"
#include "reone/scene/particleutil.h"
#include "reone/scene/render/pass.h"
#include "reone/system/randomutil.h"

using namespace reone::graphics;

namespace reone {

namespace scene {

static constexpr float kGroundSampleInterval = 0.5f; // seconds between walkmesh height samples

void EmitterSceneNode::setController(uint16_t type, float value) {
    switch (type) {
    case ControllerTypes::birthrate:
        _birthrate = value;
        _birthInterval = value != 0.0f ? 1.0f / value : 0.0f;
        break;
    case ControllerTypes::randomBirthRate:
        _randomBirthrate = value;
        break;
    case ControllerTypes::lifeExp:
        _lifeExpectancy = value;
        break;
    case ControllerTypes::xSize:
        _size.x = value;
        break;
    case ControllerTypes::ySize:
        _size.y = value;
        break;
    case ControllerTypes::frameStart:
        _frameStart = static_cast<int>(value);
        break;
    case ControllerTypes::frameEnd:
        _frameEnd = static_cast<int>(value);
        break;
    case ControllerTypes::fps:
        _fps = value;
        break;
    case ControllerTypes::spread:
        _spread = value;
        break;
    case ControllerTypes::velocity:
        _velocity = value;
        break;
    case ControllerTypes::randVel:
        _randomVelocity = value;
        break;
    case ControllerTypes::blurLength:
        _blurLength = value;
        break;
    case ControllerTypes::mass:
        _mass = value;
        break;
    case ControllerTypes::grav:
        _grav = value;
        break;
    case ControllerTypes::drag:
        _drag = value;
        break;
    case ControllerTypes::bounceCo:
        _bounceCo = value;
        break;
    case ControllerTypes::particleRot:
        _particleRot = value;
        break;
    case ControllerTypes::p2pBezier2:
        _p2pBezier2 = value;
        break;
    case ControllerTypes::p2pBezier3:
        _p2pBezier3 = value;
        break;
    case ControllerTypes::lightingDelay:
        _lightningDelay = value;
        break;
    case ControllerTypes::lightingRadius:
        _lightningRadius = value;
        break;
    case ControllerTypes::lightingScale:
        _lightningScale = value;
        break;
    case ControllerTypes::lightingSubDiv:
        _lightningSubDiv = static_cast<int>(value);
        break;
    case ControllerTypes::sizeStart:
        _particleSize.start = value;
        break;
    case ControllerTypes::sizeMid:
        _particleSize.mid = value;
        break;
    case ControllerTypes::sizeEnd:
        _particleSize.end = value;
        break;
    case ControllerTypes::sizeStartY:
        _particleSizeY.start = value;
        break;
    case ControllerTypes::sizeMidY:
        _particleSizeY.mid = value;
        break;
    case ControllerTypes::sizeEndY:
        _particleSizeY.end = value;
        break;
    case ControllerTypes::alphaStart:
        _alpha.start = value;
        break;
    case ControllerTypes::alphaMid:
        _alpha.mid = value;
        break;
    case ControllerTypes::alphaEnd:
        _alpha.end = value;
        break;
    case ControllerTypes::percentStart:
        _percentStart = value;
        break;
    case ControllerTypes::percentMid:
        _percentMid = value;
        break;
    case ControllerTypes::percentEnd:
        _percentEnd = value;
        break;
    default:
        // combinetime, threshold, targetsize, the control-point and tangent
        // controllers and lightningzigzag are not simulated.
        break;
    }
}

void EmitterSceneNode::setController(uint16_t type, const glm::vec3 &value) {
    switch (type) {
    case ControllerTypes::colorStart:
        _color.start = value;
        break;
    case ControllerTypes::colorMid:
        _color.mid = value;
        break;
    case ControllerTypes::colorEnd:
        _color.end = value;
        break;
    default:
        break;
    }
}

void EmitterSceneNode::captureRest() {
    // The geometry value of every controller, at time 0. Values the playing
    // animation does not key fall back to these.
    for (auto &[type, track] : _modelNode.floatTracks()) {
        float value;
        if (track.valueAtTime(0.0f, value)) {
            _restFloats[static_cast<uint16_t>(type)] = value;
            setController(static_cast<uint16_t>(type), value);
        }
    }
    for (auto &[type, track] : _modelNode.vectorTracks()) {
        glm::vec3 value;
        if (track.valueAtTime(0.0f, value)) {
            _restVectors[static_cast<uint16_t>(type)] = value;
            setController(static_cast<uint16_t>(type), value);
        }
    }
}

void EmitterSceneNode::init() {
    captureRest();
    // Height follows its own ramp only when one is authored non-zero.
    _hasSizeY = _particleSizeY.start != 0.0f || _particleSizeY.mid != 0.0f || _particleSizeY.end != 0.0f;

    // Pre-allocate particles
    int numParticles;
    if (_modelNode.emitter()->updateMode == ModelNode::Emitter::UpdateMode::Single) {
        numParticles = 1;
    } else {
        numParticles = kMaxParticles;
    }
    for (int i = 0; i < numParticles; ++i) {
        _particlePool.push_back(_sceneGraph.newParticle(*this).get());
    }
}

void EmitterSceneNode::applyAnimatedControllers(
    const std::vector<std::pair<uint16_t, float>> &floats,
    const std::vector<std::pair<uint16_t, glm::vec3>> &vectors) {

    if (_animatedLastUpdate) {
        // Controllers the last animation keyed and this one may not return to
        // rest before the new values land.
        for (auto &[type, value] : _restFloats) {
            setController(type, value);
        }
        for (auto &[type, value] : _restVectors) {
            setController(type, value);
        }
        _animatedLastUpdate = false;
    }
    for (auto &[type, value] : floats) {
        setController(type, value);
    }
    for (auto &[type, value] : vectors) {
        setController(type, value);
    }
    _animatedLastUpdate = !floats.empty() || !vectors.empty();
    _hasSizeY = _particleSizeY.start != 0.0f || _particleSizeY.mid != 0.0f || _particleSizeY.end != 0.0f;
}

SceneNode *EmitterSceneNode::owner() const {
    const SceneNode *node = _parent;
    while (node && node->type() != SceneNodeType::Model) {
        node = node->parent();
    }
    return const_cast<SceneNode *>(node);
}

std::optional<glm::vec3> EmitterSceneNode::p2pTarget() const {
    for (auto &child : _children) {
        if (child->type() == SceneNodeType::Dummy) {
            return child->origin();
        }
    }
    return std::nullopt;
}

glm::vec3 EmitterSceneNode::p2pTargetAxis() const {
    for (auto &child : _children) {
        if (child->type() == SceneNodeType::Dummy) {
            glm::vec3 axis(child->absoluteTransform()[2]);
            float len = glm::length(axis);
            return len > 1e-6f ? axis / len : glm::vec3(0.0f, 0.0f, 1.0f);
        }
    }
    return glm::vec3(0.0f, 0.0f, 1.0f);
}

void EmitterSceneNode::updateMotion(float dt) {
    auto &emitter = *_modelNode.emitter();
    _lastDt = dt;
    glm::vec3 position(_absTransform[3]);
    SceneNode *ownerNode = owner();
    glm::vec3 ownerPosition = ownerNode ? ownerNode->origin() : position;

    if (!_hasPrevious) {
        _prevAbsTransform = _absTransform;
        _prevOwnerPosition = ownerPosition;
        _emitterVelocity = glm::vec3(0.0f);
    } else {
        glm::vec3 prevPosition(_prevAbsTransform[3]);
        glm::vec3 moved = position - prevPosition;
        _emitterVelocity = dt > 0.0f ? moved / dt : glm::vec3(0.0f);
        // An emitter moved by the simulation tick may sit still for a render
        // frame between two moves; hold its last motion briefly for the blur.
        if (glm::length(_emitterVelocity) > 1e-4f) {
            _blurVelocity = _emitterVelocity;
            _blurVelocityAge = 0.0f;
        } else {
            _blurVelocityAge += dt;
            if (_blurVelocityAge > 0.1f) {
                _blurVelocity = glm::vec3(0.0f);
            }
        }

        if (_absTransform != _prevAbsTransform || ownerPosition != _prevOwnerPosition) {
            glm::mat4 delta = _absTransform * glm::inverse(_prevAbsTransform);
            glm::vec3 ownerMoved = ownerPosition - _prevOwnerPosition;
            for (auto &child : _children) {
                if (child->type() != SceneNodeType::Particle) {
                    continue;
                }
                auto particle = static_cast<ParticleSceneNode *>(child);
                if (particle->isStuck()) {
                    continue;
                }
                if (emitter.inherit) {
                    particle->transformBy(delta);
                } else if (emitter.inheritLocal) {
                    particle->translateBy(ownerMoved);
                }
                if (emitter.inheritParticle) {
                    particle->translateBy(moved);
                }
            }
        }
    }

    if (emitter.affectedByWind) {
        // Area wind: 0 none, 1 weak, 2 strong. Direction and gust are ours.
        static const glm::vec3 kWindDirection = glm::normalize(glm::vec3(0.8f, 0.6f, 0.0f));
        float power = _sceneGraph.windPower();
        float speed = power <= 0.0f ? 0.0f : (power < 1.5f ? 1.0f : 2.5f);
        _windTime += dt;
        float gust = 0.75f + 0.25f * glm::sin(0.7f * _windTime);
        _windVelocity = kWindDirection * (speed * gust);
    }

    if (emitter.bounce) {
        _groundSampleTimer -= dt;
        if (_groundSampleTimer <= 0.0f) {
            _groundSampleTimer = kGroundSampleInterval;
            Collision collision;
            if (_sceneGraph.testElevation(position, collision)) {
                _groundHeight = collision.intersection.z;
            }
        }
    }
}

void EmitterSceneNode::update(float dt) {
    updateMotion(dt);
    removeExpiredParticles(dt);
    spawnParticles(dt);

    for (auto &child : _children) {
        if (child->type() != SceneNodeType::Particle) {
            continue;
        }
        auto particle = static_cast<ParticleSceneNode *>(child);
        particle->update(dt);
    }

    _prevAbsTransform = _absTransform;
    SceneNode *ownerNode = owner();
    _prevOwnerPosition = ownerNode ? ownerNode->origin() : glm::vec3(_absTransform[3]);
    _hasPrevious = true;
}

void EmitterSceneNode::removeExpiredParticles(float dt) {
    if (_lifeExpectancy == -1.0f) {
        return;
    }
    std::vector<ParticleSceneNode *> expiredParticles;
    for (auto &child : _children) {
        if (child->type() != SceneNodeType::Particle) {
            continue;
        }
        auto particle = static_cast<ParticleSceneNode *>(child);
        if (particle->isExpired()) {
            expiredParticles.push_back(particle);
        }
    }
    for (auto &particle : expiredParticles) {
        removeChild(*particle);
        _particlePool.push_back(particle);
    }
}

void EmitterSceneNode::spawnBatch(int count) {
    bool trail = _modelNode.emitter()->spawnType == ModelNode::Emitter::SpawnType::Trail;
    for (int i = 0; i < count; ++i) {
        // A trail spreads this update's births along the path the emitter
        // moved, the first at the start of it.
        float fraction = trail ? static_cast<float>(i + 1) / static_cast<float>(count) : 1.0f;
        doSpawnParticle(fraction);
    }
}

void EmitterSceneNode::spawnParticles(float dt) {
    std::shared_ptr<ModelNode::Emitter> emitter(_modelNode.emitter());
    switch (emitter->updateMode) {
    case ModelNode::Emitter::UpdateMode::Fountain:
        if (_birthrate != 0.0f) {
            _particleAccumulator += dt;
            if (_particleAccumulator >= _birthInterval) {
                int count = fountainSpawnCount(_particleAccumulator);
                spawnBatch(count);
                _particleAccumulator = 0.0f;
            }
        }
        break;
    case ModelNode::Emitter::UpdateMode::Single:
        if (!_spawned || (_children.empty() && emitter->loop)) {
            doSpawnParticle();
            _spawned = true;
        }
        break;
    case ModelNode::Emitter::UpdateMode::Lightning:
        _birthTimer.update(dt);
        if (_birthTimer.elapsed()) {
            spawnLightningParticles();
            _birthTimer.reset(_lightningDelay);
        }
        break;
    case ModelNode::Emitter::UpdateMode::Explosion:
        // Nothing is born over time; detonate() bursts.
        break;
    default:
        break;
    }
}

ParticleSceneNode *EmitterSceneNode::doSpawnParticle(float pathFraction) {
    // kMaxParticles is the size of one GPU uniform batch, not an emitter
    // population limit. Retail allocates another particle when its dead list is
    // empty; SceneGraph likewise owns every particle allocated here and later
    // splits a large emitter into kMaxParticles-sized draw batches.
    if (_particlePool.empty()) {
        _particlePool.push_back(_sceneGraph.newParticle(*this).get());
    }
    auto &emitter = *_modelNode.emitter();
    auto particle = static_cast<ParticleSceneNode *>(_particlePool.front());
    particle->setLifetime(0.0f);

    float halfW = 0.005f * _size.x;
    float halfH = 0.005f * _size.y;
    glm::vec4 offset(cosmeticRandomFloat(-halfW, halfW), cosmeticRandomFloat(-halfH, halfH), 0.0f, 1.0f);
    glm::vec3 worldPosition(_absTransform * offset);
    if (pathFraction < 1.0f && _hasPrevious) {
        glm::vec3 previous(_prevAbsTransform * offset);
        worldPosition = glm::mix(previous, worldPosition, pathFraction);
    }

    // Direction: the emitter's +Z, spread; particles then move in world space.
    float halfSpread = 0.5f * _spread;
    float angle1 = cosmeticRandomFloat(-halfSpread, halfSpread);
    float angle2 = cosmeticRandomFloat(-halfSpread, halfSpread);
    glm::vec3 dir(glm::sin(angle1), glm::sin(angle2), glm::cos(angle1) * glm::cos(angle2));
    glm::vec3 worldDir(glm::mat3(_absTransform) * dir);
    float dirLength = glm::length(worldDir);
    if (dirLength > 1e-6f) {
        worldDir /= dirLength;
    }
    glm::vec3 velocity((_velocity + cosmeticRandomFloat(0.0f, _randomVelocity)) * worldDir);
    if (emitter.inheritVelocity) {
        velocity += _emitterVelocity;
    }

    // Remove particle from pool and append it to emitter
    _particlePool.pop_front();
    addChild(*particle);
    particle->resetAt(worldPosition);
    particle->setVelocity(std::move(velocity));

    particle->setFrame(_frameStart);
    if (emitter.random) {
        int count = glm::abs(_frameEnd - _frameStart) + 1;
        if (count > 1) {
            particle->setFrameOffset(cosmeticRandomInt(0, count - 1));
        }
    }
    if (_fps > 0.0f) {
        particle->setAnimLength((_frameEnd - _frameStart + 1) / _fps);
    }
    if (emitter.updateMode != ModelNode::Emitter::UpdateMode::Lightning) {
        // Lightning sizes and colours its bolts itself.
        particle->startLife();
    }

    return particle;
}

int EmitterSceneNode::fountainSpawnCount(float elapsed) {
    float effectiveRate = _birthrate;
    int randomRange = static_cast<int>(glm::round(_randomBirthrate));
    if (randomRange > 0) {
        bool add = cosmeticRandomInt(0, 1) != 0;
        int variation = cosmeticRandomInt(0, randomRange - 1);
        effectiveRate += add ? variation : -variation;
        effectiveRate = glm::max(0.0f, effectiveRate);
    }

    int divisor = static_cast<int>(effectiveRate) + 1;
    return divisor > 0 ? static_cast<int>(elapsed * effectiveRate) % divisor : 0;
}

void EmitterSceneNode::prewarmContinuousParticles() {
    auto emitter = _modelNode.emitter();
    if (!emitter || emitter->updateMode != ModelNode::Emitter::UpdateMode::Fountain) {
        return;
    }
    if (_birthrate <= 0.0f || _lifeExpectancy <= 0.0f) {
        return;
    }

    // Take the phase where a particle is born exactly as the scene opens. The
    // ones still alive behind it are then those emitted one, two, three birth
    // intervals ago, up to the last whose age is still short of the life
    // expectancy: ceil(birthrate * lifeExpectancy) of them.
    int emissionSlots = static_cast<int>(glm::ceil(_birthrate * _lifeExpectancy));
    for (int i = 0; i < emissionSlots; ++i) {
        int count = fountainSpawnCount(_birthInterval);
        for (int j = 0; j < count; ++j) {
            auto particle = doSpawnParticle();
            if (!particle) {
                break;
            }
            // Age by whole birth intervals, not by an even share of the
            // lifetime: the two agree only when birthrate times life
            // expectancy is a whole number, and elsewhere an even share
            // invents ages the authored cadence could never have produced.
            particle->update(i * _birthInterval);
        }
    }

    // The next birth is a full interval away, so the field this leaves behind
    // is not immediately followed by an extra particle.
    _particleAccumulator = 0.0f;
}

void EmitterSceneNode::spawnLightningParticles() {
    // Ensure there is a reference node directly under this emitter
    auto ref = std::find_if(_children.begin(), _children.end(), [](auto &child) { return child->type() == SceneNodeType::Dummy; });
    if (ref == _children.end()) {
        return;
    }

    float halfW = 0.005f * _size.x;
    float halfH = 0.005f * _size.y;
    glm::vec3 origin(cosmeticRandomFloat(-halfW, halfW), cosmeticRandomFloat(-halfH, halfH), 0.0f);
    glm::vec3 emitterSpaceRefPos(_absTransformInv * glm::vec4((*ref)->origin(), 1.0f));
    glm::vec3 refToOrigin(emitterSpaceRefPos - origin);
    float distance = glm::abs(refToOrigin.z);
    float segmentLength = distance / static_cast<float>(_lightningSubDiv + 1);
    float halfRadius = 0.5f * _lightningRadius;

    std::vector<std::pair<glm::vec3, glm::vec3>> segments;
    segments.resize(_lightningSubDiv + 1);
    segments[0].first = origin;
    for (int i = 1; i < _lightningSubDiv + 1; ++i) {
        glm::vec3 dir(glm::normalize(emitterSpaceRefPos - segments[i - 1].first));
        glm::vec3 offset(
            cosmeticRandomFloat(-halfRadius, halfRadius),
            cosmeticRandomFloat(-halfRadius, halfRadius),
            0.0f);
        segments[i - 1].second = segments[i - 1].first + segmentLength * dir + offset;
        segments[i].first = segments[i - 1].second;
    }
    segments[_lightningSubDiv].second = emitterSpaceRefPos;

    // Return all particles to pool
    for (auto it = _children.begin(); it != _children.end();) {
        auto child = *it;
        if ((*it)->type() == SceneNodeType::Particle) {
            _particlePool.push_back(static_cast<ParticleSceneNode *>(child));
            it = _children.erase(it);
        } else {
            ++it;
        }
    }

    for (auto &segment : segments) {
        // Take particle from the pool, if available
        if (_particlePool.empty()) {
            return;
        }
        auto particle = _particlePool.front();
        _particlePool.pop_front();
        particle->setLifetime(0.0f);

        glm::vec3 endToStart(segment.second - segment.first);
        glm::vec3 center(0.5f * (segment.first + segment.second));
        addChild(*particle);
        particle->resetAt(glm::vec3(_absTransform * glm::vec4(center, 1.0f)));
        particle->setDir(_absTransform * glm::vec4(glm::normalize(endToStart), 0.0f));
        // A bolt segment is as wide as the emitter's particle size: the K1
        // rays (cold, stun, drain, the space laser) author lightningscale 0
        // and a size of 0.1-0.6, and a scale-wide segment drew nothing.
        // Lightning particles are not aged (ParticleSceneNode::update), so
        // they take the start colour and alpha.
        particle->setSize(glm::vec2(getParticleSize(0.0f), glm::length(endToStart)));
        particle->setColor(getColor(0.0f));
        particle->setAlpha(getAlpha(0.0f));
    }
}

void EmitterSceneNode::detonate() {
    // Only explosion emitters answer the animation's detonate event: each one
    // bursts birthrate particles at once.
    if (_modelNode.emitter()->updateMode != ModelNode::Emitter::UpdateMode::Explosion) {
        return;
    }
    int count = glm::max(1, static_cast<int>(glm::round(_birthrate)));
    for (int i = 0; i < count; ++i) {
        doSpawnParticle();
    }
}

void EmitterSceneNode::rearmSingle() {
    auto emitter = _modelNode.emitter();
    if (emitter && emitter->updateMode == ModelNode::Emitter::UpdateMode::Single) {
        _spawned = false;
    }
}

void EmitterSceneNode::onAbsoluteTransformChanged() {
    // Only apply()'s blend is a drawn-only transform. Presentation code that
    // places the emitter's model itself (a visual effect following its
    // target) moves it for real, also while other nodes are blended.
    if (!interp::blending()) {
        _simAbsTransform = _absTransform;
    }
}

void EmitterSceneNode::renderLeafs(IRenderPass &pass, const std::vector<SceneNode *> &leafs) {
    if (leafs.empty()) {
        return;
    }
    auto emitter = _modelNode.emitter();
    auto texture = _resourceSvc.textures.get(emitter->textureName, TextureUsage::MainTex);
    if (!texture) {
        return;
    }
    auto emitterRight = glm::vec3(_absTransform[0]);
    auto emitterUp = glm::vec3(_absTransform[1]);
    auto emitterForward = glm::vec3(_absTransform[2]);

    auto &cameraNode = _sceneGraph.camera()->get();
    auto view = cameraNode.camera()->view();
    auto cameraRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
    auto cameraUp = glm::vec3(view[0][1], view[1][1], view[2][1]);

    // Linked particles form a ribbon: each one reaches to the particle born
    // just before it (lightning sets its own direction instead).
    std::unordered_map<const ParticleSceneNode *, const ParticleSceneNode *> olderNeighbour;
    if (emitter->renderMode == ModelNode::Emitter::RenderMode::Linked) {
        std::vector<const ParticleSceneNode *> chain;
        for (auto &child : _children) {
            if (child->type() == SceneNodeType::Particle) {
                chain.push_back(static_cast<const ParticleSceneNode *>(child));
            }
        }
        std::sort(chain.begin(), chain.end(), [](auto a, auto b) { return a->lifetime() < b->lifetime(); });
        for (size_t i = 0; i + 1 < chain.size(); ++i) {
            olderNeighbour[chain[i]] = chain[i + 1];
        }
    }
    // Render interpolation: the emitter may be drawn between two ticks
    // (reone/scene/interp.h). Particles that follow it are drawn moved with
    // it; the others stay where the simulation left them in world space.
    bool shifted = interp::applied() && _absTransform != _simAbsTransform;
    glm::mat4 presentDelta(1.0f);
    glm::vec3 presentShift(0.0f);
    if (shifted) {
        presentDelta = _absTransform * glm::inverse(_simAbsTransform);
        presentShift = glm::vec3(_absTransform[3]) - glm::vec3(_simAbsTransform[3]);
    }
    auto presented = [&](const ParticleSceneNode &p) -> glm::vec3 {
        if (!shifted || p.isStuck()) {
            return p.position();
        }
        if (emitter->inherit) {
            return glm::vec3(presentDelta * glm::vec4(p.position(), 1.0f));
        }
        if (emitter->inheritLocal || emitter->inheritParticle) {
            return p.position() + presentShift;
        }
        return p.position();
    };
    auto particles = std::vector<ParticleInstance>(leafs.size());
    for (size_t i = 0; i < leafs.size(); ++i) {
        const auto particle = static_cast<ParticleSceneNode *>(leafs[i]);
        particles[i].frame = particle->frame();
        particles[i].position = presented(*particle);
        particles[i].size = glm::vec2(particle->size());
        particles[i].color = glm::vec4(particle->color(), particle->alpha());
        glm::vec3 right, up;
        bool spins = false;
        switch (emitter->renderMode) {
        case ModelNode::Emitter::RenderMode::BillboardToLocalZ:
            right = emitterUp;
            up = emitterRight;
            spins = true;
            break;
        case ModelNode::Emitter::RenderMode::MotionBlur: {
            // Particle velocity is already in world space. A particle that
            // inherits the emitter's motion (a blaster bolt) moves with it, so
            // the emitter's motion is part of what the blur shows.
            glm::vec3 blurVelocity(particle->velocity());
            if (emitter->inherit && !particle->isStuck()) {
                blurVelocity += _blurVelocity;
            }
            auto basis = particleutil::buildMotionBlurBasis(
                glm::mat4(1.0f),
                blurVelocity,
                cameraNode.origin() - presented(*particle),
                cameraRight,
                cameraUp,
                particles[i].size.y,
                _blurLength,
                _lastDt);
            // The streak runs back from the particle to where it was, not
            // half ahead of it: shift the centred quad back by half the trail.
            particles[i].position -= basis.up * (0.5f * particles[i].size.y * (basis.lengthScale - 1.0f));
            particles[i].size.y *= basis.lengthScale;
            right = basis.right;
            up = basis.up;
            break;
        }
        case ModelNode::Emitter::RenderMode::BillboardToWorldZ:
            right = glm::vec3(0.0f, 1.0f, 0.0f);
            up = glm::vec3(1.0f, 0.0f, 0.0f);
            spins = true;
            break;
        case ModelNode::Emitter::RenderMode::AlignedToWorldZ: {
            // Upright quad turning about world Z to face the camera.
            up = glm::vec3(0.0f, 0.0f, 1.0f);
            glm::vec3 toCamera(cameraNode.origin() - presented(*particle));
            glm::vec3 side(glm::cross(up, toCamera));
            float sideLength = glm::length(side);
            right = sideLength > 1e-5f ? side / sideLength : cameraRight;
            break;
        }
        case ModelNode::Emitter::RenderMode::AlignedToParticleDir:
            right = emitterRight;
            up = emitterForward;
            break;
        case ModelNode::Emitter::RenderMode::Linked: {
            auto particleUp = particle->dir();
            if (particleUp == glm::vec3(0.0f)) {
                auto neighbour = olderNeighbour.find(particle);
                if (neighbour == olderNeighbour.end()) {
                    particles[i].size = glm::vec2(0.0f); // the chain's last link
                    right = cameraRight;
                    up = cameraUp;
                    break;
                }
                glm::vec3 span(presented(*neighbour->second) - presented(*particle));
                float length = glm::length(span);
                if (length < 1e-5f) {
                    particles[i].size = glm::vec2(0.0f);
                    right = cameraRight;
                    up = cameraUp;
                    break;
                }
                // The quad is centred on its position: centre it on the link.
                particles[i].position = presented(*particle) + 0.5f * span;
                particles[i].size.y = length;
                up = span / length;
                glm::vec3 side(glm::cross(up, cameraNode.origin() - particles[i].position));
                float sideLength = glm::length(side);
                right = sideLength > 1e-5f ? side / sideLength : cameraRight;
                break;
            }
            auto particleForward = glm::cross(particleUp, cameraRight);
            right = glm::cross(particleForward, particleUp);
            up = particleUp;
            break;
        }
        case ModelNode::Emitter::RenderMode::Normal:
        default:
            right = cameraRight;
            up = cameraUp;
            spins = true;
            break;
        }
        if (particle->isStuck()) {
            // A splat lies flat on the ground it hit.
            right = glm::vec3(1.0f, 0.0f, 0.0f);
            up = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        if (spins && particle->rotation() != 0.0f) {
            float c = glm::cos(particle->rotation());
            float s = glm::sin(particle->rotation());
            glm::vec3 spunRight(c * right + s * up);
            glm::vec3 spunUp(c * up - s * right);
            right = spunRight;
            up = spunUp;
        }
        particles[i].right = right;
        particles[i].up = up;
    }
    // Billboard_to_Local_Z quads lie in the emitter's XY plane with their
    // axes swapped (right is local Y, up is local X), so their front face is
    // local -Z. Seen from +Z (the character screen's alignment mist and
    // streams, which face the camera along Dummy02's +Z) they were culled.
    bool twosided = _modelNode.emitter()->twosided ||
                    _modelNode.emitter()->renderMode == ModelNode::Emitter::RenderMode::MotionBlur ||
                    _modelNode.emitter()->renderMode == ModelNode::Emitter::RenderMode::BillboardToLocalZ;
    auto faceCulling = twosided ? FaceCullMode::None : FaceCullMode::Back;
    bool premultipliedAlpha = emitter->blendMode == ModelNode::Emitter::BlendMode::Lighten;
    pass.drawParticles(*texture, faceCulling, premultipliedAlpha, emitter->gridSize, particles);
}

} // namespace scene

} // namespace reone
