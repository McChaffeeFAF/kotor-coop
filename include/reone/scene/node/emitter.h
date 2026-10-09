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

#include "reone/system/timer.h"

#include "modelnode.h"

namespace reone {

namespace scene {

class ModelSceneNode;
class ParticleSceneNode;

class EmitterSceneNode : public ModelNodeSceneNode {
public:
    EmitterSceneNode(
        graphics::ModelNode &modelNode,
        ISceneGraph &sceneGraph,
        graphics::GraphicsServices &graphicsSvc,
        audio::AudioServices &audioSvc,
        resource::ResourceServices &resourceSvc) :
        ModelNodeSceneNode(
            modelNode,
            SceneNodeType::Emitter,
            sceneGraph,
            graphicsSvc,
            audioSvc,
            resourceSvc) {
    }

    void init();

    void update(float dt) override;

    void renderLeafs(IRenderPass &pass, const std::vector<SceneNode *> &leafs) override;

    void detonate();
    void rearmSingle();

    /**
     * Fill a continuous emitter with the particle field it would be carrying
     * had it already been running, so its first rendered frame shows an
     * established effect rather than one starting from nothing.
     *
     * Only Fountain emitters are populated. Single, Lightning, Explosion and
     * unsupported modes keep their ordinary lifecycle untouched.
     */
    void prewarmContinuousParticles();

    float getParticleSize(float time) const { return _particleSize.get(time, _percentStart, _percentMid, _percentEnd); };
    /** Particle height over its life: sizeStart_y/Mid_y/End_y when authored, else the width. */
    float getParticleSizeY(float time) const {
        return _hasSizeY ? _particleSizeY.get(time, _percentStart, _percentMid, _percentEnd) : getParticleSize(time);
    }
    glm::vec3 getColor(float time) const { return _color.get(time, _percentStart, _percentMid, _percentEnd); };
    float getAlpha(float time) const { return _alpha.get(time, _percentStart, _percentMid, _percentEnd); };

    float lifeExpectancy() const { return _lifeExpectancy; }
    int frameStart() const { return _frameStart; }
    int frameEnd() const { return _frameEnd; }
    float grav() const { return _grav; }
    float mass() const { return _mass; }
    float drag() const { return _drag; }
    float bounceCo() const { return _bounceCo; }
    float particleRot() const { return _particleRot; }
    float birthrate() const { return _birthrate; }
    float p2pBezier2() const { return _p2pBezier2; }
    float p2pBezier3() const { return _p2pBezier3; }

    /** World-space ground height under the emitter, when a bouncing emitter has found one. */
    std::optional<float> groundHeight() const { return _groundHeight; }
    void setGroundHeight(std::optional<float> height) { _groundHeight = height; }

    /** World-space wind velocity at the emitter (zero unless affected by wind). */
    glm::vec3 windVelocity() const { return _windVelocity; }

    /**
     * World position of the point-to-point target: the emitter's first dummy
     * child. Empty when the emitter has none.
     */
    std::optional<glm::vec3> p2pTarget() const;
    glm::vec3 p2pTargetAxis() const;

    /**
     * Apply the values the playing animation keys for this emitter. Controllers
     * the animation does not key return to the geometry (rest) value.
     */
    void applyAnimatedControllers(
        const std::vector<std::pair<uint16_t, float>> &floats,
        const std::vector<std::pair<uint16_t, glm::vec3>> &vectors);

private:
    template <class T>
    struct StartMidEnd {
        T start;
        T mid;
        T end;

        T get(float factor, float percentStart, float percentMid, float percentEnd) const {
            percentStart = glm::clamp(percentStart, 0.0f, 1.0f);
            percentMid = glm::clamp(percentMid, 0.0f, 1.0f);
            percentEnd = glm::clamp(percentEnd, 0.0f, 1.0f);
            if (factor <= percentStart) {
                return start;
            }
            if (percentMid > percentStart && factor < percentMid) {
                return glm::mix(start, mid, (factor - percentStart) / (percentMid - percentStart));
            }
            if (factor < percentEnd && percentEnd > percentMid) {
                return glm::mix(mid, end, (factor - percentMid) / (percentEnd - percentMid));
            }
            return end;
        }
    };

    StartMidEnd<float> _particleSize;
    StartMidEnd<float> _particleSizeY;
    bool _hasSizeY {false};
    StartMidEnd<glm::vec3> _color;
    StartMidEnd<float> _alpha;

    float _percentStart {0.0f};
    float _percentMid {0.5f};
    float _percentEnd {1.0f};

    float _birthrate {0.0f};      /**< rate of particle birth per second */
    float _randomBirthrate {0.0f}; /**< integral random variation applied by Fountain emitters */
    float _lifeExpectancy {0.0f}; /**< life of each particle in seconds */
    glm::vec2 _size {0.0f};
    int _frameStart {0};
    int _frameEnd {0};
    float _fps {0.0f};
    float _spread {0.0f};
    float _velocity {0.0f};
    float _randomVelocity {0.0f};
    float _blurLength {0.0f};
    float _mass {0.0f};
    float _grav {0.0f};
    float _lightningDelay {0.0f};
    float _lightningRadius {0.0f};
    float _lightningScale {0.0f};
    int _lightningSubDiv {0};
    float _bounceCo {0.0f};
    float _drag {0.0f};
    float _particleRot {0.0f};
    float _p2pBezier2 {0.0f};
    float _p2pBezier3 {0.0f};

    /** Geometry values of every controller, restored when no animation keys them. */
    std::unordered_map<uint16_t, float> _restFloats;
    std::unordered_map<uint16_t, glm::vec3> _restVectors;
    bool _animatedLastUpdate {false};

    // Motion of the emitter between updates, for the inherit flags and trails

    bool _hasPrevious {false};
    glm::mat4 _prevAbsTransform {1.0f};
    glm::mat4 _simAbsTransform {1.0f}; /**< the simulated absolute transform, while a blended one is drawn */
    glm::vec3 _prevOwnerPosition {0.0f};
    glm::vec3 _emitterVelocity {0.0f}; /**< world-space, measured between updates */
    glm::vec3 _blurVelocity {0.0f};    /**< last non-zero emitter velocity, for motion blur of inherited particles */
    float _blurVelocityAge {0.0f};
    float _lastDt {0.0f};
    glm::vec3 _windVelocity {0.0f};
    float _windTime {0.0f};
    std::optional<float> _groundHeight;
    float _groundSampleTimer {0.0f};

    // END Motion

    float _birthInterval {0.0f};
    float _particleAccumulator {0.0f};
    Timer _birthTimer;
    bool _spawned {false};

    std::deque<ParticleSceneNode *> _particlePool; /**< pre-allocated pool of particles */

    void setController(uint16_t type, float value);
    void setController(uint16_t type, const glm::vec3 &value);
    void captureRest();
    void onAbsoluteTransformChanged() override;
    void updateMotion(float dt);
    SceneNode *owner() const;

    void spawnParticles(float dt);
    void removeExpiredParticles(float dt);
    void spawnBatch(int count);
    ParticleSceneNode *doSpawnParticle(float pathFraction = 1.0f);
    int fountainSpawnCount(float elapsed);
    void spawnLightningParticles();
};

} // namespace scene

} // namespace reone
