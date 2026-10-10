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

#include "reone/game/types.h"

namespace reone {

namespace audio {
class AudioSource;
class IAudioMixer;
} // namespace audio

namespace scene {
class ISceneGraph;
class ModelSceneNode;
} // namespace scene

namespace game {

struct VisualEffectDesc;

/**
 * Draws visual effects (visualeffects.2da rows) once per rendered frame.
 *
 * The simulation owns the effect object and its lifetime. When an effect
 * starts on a target it hands the presenter a request; when it ends it marks
 * the request ended. The presenter owns every model and looping sound it
 * spawns for the request, and nothing it does flows back into the simulation.
 * See design note v_vfx_emitters, sections 3 and 4.
 */
class VisualEffectPresenter {
public:
    struct Request {
        const VisualEffectDesc *desc {nullptr};
        uint32_t targetId {0};          /**< 0: a location */
        glm::vec3 location {0.0f};      /**< where a location effect plays */
        float facing {0.0f};            /**< radians, for a location effect */
        bool ended {false};             /**< set by the simulation */
        uint32_t sourceId {0};          /**< a beam's effector (EffectBeam), 0: none */
        int sourceBodyNode {0};         /**< BodyNode the beam leaves from */
    };

    enum class AttachPoint {
        Root,
        Impact,
        HeadConjure,
        BeamSource /**< a beam model, on its effector's body node */
    };

    struct Context {
        scene::ISceneGraph *graph {nullptr};
        audio::IAudioMixer *mixer {nullptr};
        /** The target object's model, or null when it is gone. */
        std::function<scene::ModelSceneNode *(uint32_t)> modelOf;
        std::optional<glm::vec3> cameraPosition;
    };

    /** Called by the simulation: present this request from the next frame on. */
    void begin(std::shared_ptr<Request> request);

    /** Called once per rendered frame, after the ticks. */
    void update(float dt, const Context &ctx);

    /** Remove every model and sound (module or area change). */
    void clear();

    /** Camera offset for this frame's scene render (shake). Zero when none. */
    const glm::vec3 &cameraShake() const { return _shakeOffset; }

    size_t numActive() const { return _active.size(); }
    size_t numPending() const { return _pending.size(); }
    size_t numModels() const;

    /** Every model the presenter shows now (inspection and tests). */
    std::vector<scene::ModelSceneNode *> models() const;

    /** World transform of the attach point, for a target model and a point. */
    static glm::mat4 attachTransform(scene::ModelSceneNode &target, AttachPoint point, bool keepWorldOrientation);

    /** World point a beam leaves from: the effector's node for a BodyNode. */
    static glm::vec3 beamSourcePoint(scene::ModelSceneNode &source, int bodyNode);

    /** World point a beam ends at: the target's impact node. */
    static glm::vec3 beamTargetPoint(scene::ModelSceneNode &target);

private:
    enum class Phase {
        Impact,    /**< playing impact once */
        Duration,  /**< looping duration/default */
        Cessation, /**< playing cessation once, then removed */
        Once       /**< a single playthrough, then removed */
    };

    struct Part {
        std::shared_ptr<scene::ModelSceneNode> node;
        AttachPoint point {AttachPoint::Root};
        Phase phase {Phase::Once};
        float age {0.0f};
        bool unanimated {false}; /**< none of its animations was played */
    };

    struct Presented {
        std::shared_ptr<Request> request;
        std::vector<Part> parts;
        std::shared_ptr<audio::AudioSource> loopSound;
        glm::mat4 lastRoot {1.0f};
        glm::vec3 lastBeamSource {0.0f};
        bool sourceLost {false};
        bool endHandled {false};
        bool targetLost {false};
    };

    struct Shake {
        glm::vec3 position {0.0f};
        float amplitude {0.0f};
        float duration {0.0f};
        float age {0.0f};
    };

    std::vector<std::shared_ptr<Request>> _pending;
    std::vector<Presented> _active;
    std::vector<Shake> _shakes;
    glm::vec3 _shakeOffset {0.0f};
    scene::ISceneGraph *_graph {nullptr};

    void start(const std::shared_ptr<Request> &request, const Context &ctx);
    void place(Presented &presented, const Context &ctx);
    void advance(Presented &presented, float dt, const Context &ctx);
    void placeBeam(Presented &presented, Part &part, scene::ModelSceneNode *target, const Context &ctx);
    void finish(Presented &presented, const Context &ctx);
    void removePart(Part &part);
    void updateShake(float dt, const Context &ctx);
};

} // namespace game

} // namespace reone
