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

#include <functional>
#include <optional>

#include "reone/graphics/animation.h"
#include "reone/graphics/lipanimation.h"
#include "reone/graphics/material.h"
#include "reone/graphics/model.h"
#include "reone/graphics/types.h"

#include "../animeventlistener.h"
#include "../animproperties.h"
#include "../node.h"
#include "../objectlights.h"
#include "../types.h"

#include "dummy.h"
#include "emitter.h"
#include "light.h"
#include "mesh.h"

namespace reone {

namespace graphics {

class Textures;
class Uniforms;

} // namespace graphics

namespace scene {

/**
 * A programmed body effect (visualeffects.2da progfx 14xx): a second,
 * slightly inflated, additive pass over every mesh of a model and the models
 * attached to it. See design note v14_programmed_effects.
 */
struct BodyOverlay {
    graphics::Texture *texture {nullptr}; /**< drawn on the mesh UVs, with its own TXI (cycle, blending, decal) */
    float strength {0.0f};                /**< 0-1: fade in and out */
    float time {0.0f};                    /**< seconds since it started: the cycle frame */

    // An afterimage (the speed effect, progfx 1500/1601): the mesh's own
    // texture, tinted and added, drawn moved by offset and pushed out along
    // the normals by inflate. Its texture is ignored.
    bool ghost {false};
    glm::vec3 offset {0.0f}; /**< world space */
    glm::vec3 tint {1.0f};
    float inflate {0.0f}; /**< metres */
};

class ModelSceneNode : public SceneNode {
public:
    enum class AnimationBlendMode {
        Single,
        Blend,
        Overlay
    };

    /**
     * How the model looks under Force Sight: grey, grey and see-through, or
     * glowing in its glow colour (upstream #356).
     */
    enum class ForceSightStyle {
        Grey,
        Translucent,
        Glow
    };

    struct AnimationStateFlags {
        static constexpr int transform = 1;
        static constexpr int alpha = 2;
        static constexpr int selfIllumColor = 4;
        static constexpr int color = 8;
        static constexpr int controllers = 16; /**< emitter / light controller values */
    };

    struct AnimationState {
        int flags {0};
        glm::mat4 transform {1.0f};
        float alpha {0.0f};
        glm::vec3 selfIllumColor {0.0f};
        glm::vec3 color {0.0f};
        std::vector<std::pair<uint16_t, float>> floats;      /**< emitter/light float controllers keyed now */
        std::vector<std::pair<uint16_t, glm::vec3>> vectors; /**< emitter colour controllers keyed now */
    };

    struct AnimationChannel {
        graphics::Animation *anim;
        std::shared_ptr<graphics::LipAnimation> lipAnim;
        AnimationProperties properties;
        float time {0.0f};
        std::unordered_map<uint16_t, AnimationState> stateByNodeNumber;
        bool freeze {false};     /**< channel time is not to be updated */
        bool transition {false}; /**< when computing states, use animation transition time as channel time */
        bool finished {false};   /**< finished channels will be erased from the queue */
        float weight {1.0f};     /**< how far a layer pulls the nodes it animates toward its own state */

        AnimationChannel(graphics::Animation &anim, std::shared_ptr<graphics::LipAnimation> lipAnim, AnimationProperties properties) :
            anim(&anim),
            lipAnim(std::move(lipAnim)),
            properties(std::move(properties)) {
            // A channel played backwards starts at its end.
            if (this->properties.speed < 0.0f) {
                time = this->lipAnim ? this->lipAnim->length() : anim.length();
            }
        }
    };

    ModelSceneNode(
        graphics::Model &model,
        ModelUsage usage,
        ISceneGraph &sceneGraph,
        graphics::GraphicsServices &graphicsSvc,
        audio::AudioServices &audioSvc,
        resource::ResourceServices &resourceSvc) :
        SceneNode(
            SceneNodeType::Model,
            sceneGraph,
            graphicsSvc,
            audioSvc,
            resourceSvc),
        _model(&model),
        _usage(usage) {
    }

    void init();

    void update(float dt) override;

    void renderLeafs(IRenderPass &pass, const std::vector<SceneNode *> &leafs) override;
    void renderAABB(IRenderPass &pass);

    void computeAABB();
    void signalEvent(const std::string &name);

    /**
     * Prewarm every continuous emitter in this model, so a scene that is shown
     * rather than entered does not have to build its effects up from nothing
     * in front of the viewer.
     *
     * Opt-in: nothing calls this on an ordinary model.
     */
    void prewarmEmitters();

    bool isPickable() const { return _pickable; }

    ModelNodeSceneNode *getNodeByNumber(uint16_t number);
    ModelNodeSceneNode *getNodeByName(const std::string &name);

    const graphics::Model &model() const { return *_model; }
    ModelUsage usage() const { return _usage; }
    float drawDistance() const { return _drawDistance; }

    void setModel(graphics::Model &model);
    void setDrawDistance(float distance) { _drawDistance = distance; }
    void setMainTexture(graphics::Texture *texture);
    void setEnvironmentMap(graphics::Texture *texture);
    void setPickable(bool pickable) { _pickable = pickable; }
    void setAnimationEventListener(IAnimationEventListener &listener) { _animEventListener = &listener; }
    /** Stops sending animation events to the listener. */
    void clearAnimationEventListener() { _animEventListener = nullptr; }

    /**
     * Draws a copy of every mesh pushed out along its normals by \p offset,
     * in \p texture, over the model and its attachments (the stealth and
     * cloak shells). Drawn by the programmed-overlay pass.
     */
    void setBumpedOutShell(std::shared_ptr<graphics::Texture> texture, float offset) {
        _bumpedOutShellTexture = std::move(texture);
        _bumpedOutShellOffset = offset;
    }
    /** Stops drawing the shell. */
    void clearBumpedOutShell() {
        _bumpedOutShellTexture.reset();
        _bumpedOutShellOffset = 0.0f;
    }
    bool hasBumpedOutShell() const { return static_cast<bool>(_bumpedOutShellTexture); }
    const std::shared_ptr<graphics::Texture> &bumpedOutShellTexture() const { return _bumpedOutShellTexture; }
    float bumpedOutShellOffset() const { return _bumpedOutShellOffset; }

    // Animation

    void playAnimation(const std::string &name, std::shared_ptr<graphics::LipAnimation> lipAnim = nullptr, AnimationProperties properties = AnimationProperties());
    void playAnimation(graphics::Animation &anim, std::shared_ptr<graphics::LipAnimation> lipAnim = nullptr, AnimationProperties properties = AnimationProperties());
    bool restartAnimation(const std::string &name);

    void pauseAnimation();
    void resumeAnimation();
    void setAnimationTime(float time);

    /**
     * Stop the channel playing the named animation, leaving every other channel
     * alone.
     *
     * Overlay animations are additive by design: playing one pushes a channel
     * and nothing ever pops it. That is fine for a fixed set of layers, but a
     * model whose overlay state changes over time - a HUD that swaps one pose
     * for another while unrelated layers keep running - would otherwise grow a
     * channel per change. Removing the outgoing animation by name keeps the set
     * bounded without disturbing the layers that should persist.
     *
     * Safe and idempotent: removing an animation that is not playing is a no-op.
     *
     * @return true if a channel was removed
     */
    bool removeAnimation(const std::string &name);

    /**
     * Let the named layer blend out from where it is, as a finished layer
     * does, leaving every other channel alone. The layer is erased once it
     * has blended out; played again before then, it starts over and blends
     * back in.
     *
     * @return true if a layer was found
     */
    bool fadeOutLayer(const std::string &name);

    /**
     * Stop every layer of this model, leaving the animations underneath and
     * the attachments' layers alone.
     */
    void removeLayers();

    /** The front channel under any layers, if there is one. */
    std::optional<std::reference_wrapper<const AnimationChannel>> baseAnimationChannel() const;

    bool isAnimationPlaying(const std::string &name) const;

    size_t animationChannelCount() const { return _animChannels.size(); }

    bool isAnimationFinished() const;

    std::string activeAnimationName() const;

    const std::deque<AnimationChannel> &animationChannels() const {
        return _animChannels;
    }

    // END Animation

    // Attachments

    void attach(const std::string &parentName, SceneNode &node);

    SceneNode *getAttachment(const std::string &parentName);

    /**
     * Move the model attached at a reference node onto another node, as an
     * effect does with the end point of a beam. Only a reference flagged
     * reattachable in the MDL may move; any other stays on its own node.
     *
     * @return true if the reference was moved
     */
    bool reattachReference(const std::string &referenceNodeName, SceneNode &newParent);

    /**
     * Hold every reattachable reference node at a world point (a beam's
     * target), without moving it out of this model: the emitters above them
     * then reach from where this model sits to that point. The aim is kept
     * over animation updates until cleared.
     *
     * @return the number of references aimed
     */
    int aimReattachableReferences(const glm::vec3 &worldPoint);
    void clearReferenceAim() { _referenceAim.reset(); }

    /**
     * A whole-object fade applied over every mesh alpha, attachments
     * included (they read it from their lighting root).
     */
    float fadeAlpha() const { return _fadeAlpha; }
    void setFadeAlpha(float alpha) { _fadeAlpha = alpha; }

    // END Attachments

    // Head look-at

    /**
     * Turn the named bone toward a world point, within the horizontal and
     * vertical arcs (degrees), closing a quarter-second of the gap each frame.
     * False when this model has no such bone.
     */
    bool beginLookAt(const std::string &bone, float arcH, float arcV);
    /** Moves the world point a running look-at turns toward. */
    void setLookAtPoint(const glm::vec3 &point);
    /** Return the bone to rest over a quarter second. */
    void endLookAt();
    bool isLookingAt() const { return _lookAt.has_value() && !_lookAt->returning; }

    // END Head look-at

    // Hilite and Force Sight

    /**
     * Multiply the diffuse colour and add to the emissive colour of every
     * mesh, attachments included (they read it from their lighting root).
     */
    void setHiliteColor(std::optional<glm::vec3> color) { _hiliteColor = color; }
    const std::optional<glm::vec3> &hiliteColor() const { return _hiliteColor; }

    /** Sets how the model looks under Force Sight and, for a glowing model, its glow colour. */
    void setForceSightStyle(ForceSightStyle style, glm::vec4 glowColor = glm::vec4 {0.0f}) {
        _forceSightStyle = style;
        _forceGlowColor = glowColor;
    }
    ForceSightStyle forceSightStyle() const { return _forceSightStyle; }
    const glm::vec4 &forceGlowColor() const { return _forceGlowColor; }

    // END Hilite and Force Sight

    // Object lighting

    /**
     * Whether this model is lit as a dynamic object (its own few lights)
     * rather than by the scene's light list. Rooms and GUI models are not.
     */
    bool usesObjectLighting() const;

    /** The outermost model this one is attached to, which owns the light set. */
    const ModelSceneNode &lightingRoot() const;

    const std::vector<graphics::MaterialLight> &objectLights() const { return _objectLights; }
    bool hasObjectLights() const { return _objectLightsValid; }

    void setObjectLights(std::vector<graphics::MaterialLight> lights) {
        _objectLights = std::move(lights);
        _objectLightsValid = true;
    }

    // END Object lighting

    // Programmed body effects

    /**
     * Overlays drawn over this model's meshes and its attachments', and the
     * opacity they leave the model at (a cloak). Presentation only.
     */
    void setBodyOverlays(std::vector<BodyOverlay> overlays, float opacity) {
        _bodyOverlays = std::move(overlays);
        _bodyOpacity = opacity;
    }
    const std::vector<BodyOverlay> &bodyOverlays() const { return _bodyOverlays; }
    float bodyOpacity() const { return _bodyOpacity; }

    // END Programmed body effects

private:
    /** A bone turned toward a world point, or returning to rest from one. */
    struct LookAt {
        std::string bone;
        float arcH {0.0f};
        float arcV {0.0f};
        glm::vec3 point {0.0f};
        bool returning {false};
        float returnTime {0.0f};
        glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
        glm::quat returnFrom {1.0f, 0.0f, 0.0f, 0.0f};
    };

    std::vector<BodyOverlay> _bodyOverlays;
    float _bodyOpacity {1.0f};

    float _fadeAlpha {1.0f};
    std::optional<glm::vec3> _hiliteColor;
    ForceSightStyle _forceSightStyle {ForceSightStyle::Grey};
    glm::vec4 _forceGlowColor {0.0f};
    std::shared_ptr<graphics::Texture> _bumpedOutShellTexture;
    float _bumpedOutShellOffset {0.0f};
    std::optional<LookAt> _lookAt;

    graphics::Model *_model;
    ModelUsage _usage;

    IAnimationEventListener *_animEventListener {nullptr};
    float _drawDistance {std::numeric_limits<float>::max()};

    // Lookups

    std::unordered_map<uint16_t, ModelNodeSceneNode *> _nodeByNumber;
    std::unordered_map<std::string, ModelNodeSceneNode *> _nodeByName;

    std::optional<glm::vec3> _referenceAim;

    int applyReferenceAim();
    std::unordered_map<std::string, SceneNode *> _attachments;

    // END Lookups

    // Animation

    /**
     * Layers lead the channels, newest first; the rest are the base channels,
     * which the blend mode describes.
     */
    std::deque<AnimationChannel> _animChannels;
    AnimationBlendMode _animBlendMode {AnimationBlendMode::Single};
    bool _culledLastTick {false}; /**< bone states were not applied last update */

    // END Animation

    // Flags

    bool _pickable {false};

    // END Flags

    std::vector<graphics::MaterialLight> _objectLights;
    bool _objectLightsValid {false};

public:
    /** This object's light set with fade weights (see updateObjectLightSlots). */
    std::vector<ObjectLightSlot> &objectLightSlots() { return _objectLightSlots; }

private:
    std::vector<ObjectLightSlot> _objectLightSlots;

    void buildNodeTree(graphics::ModelNode &node, SceneNode &parent);

    // Animation

    /** Plays \p anim as a layer over the base channels. */
    void playLayer(graphics::Animation &anim, std::shared_ptr<graphics::LipAnimation> lipAnim, AnimationProperties properties);
    /** Plays \p anim in the base channels, under the layers. */
    void playBase(graphics::Animation &anim, std::shared_ptr<graphics::LipAnimation> lipAnim, AnimationProperties properties);
    /** The number of layers leading the channels. */
    size_t layerCount() const;

    void updateAnimations(float dt);
    void updateAnimationChannel(AnimationChannel &channel, float dt);
    void rearmSingleEmitters(const std::string &animationRoot);
    void computeAnimationStates(AnimationChannel &channel, float time, const graphics::ModelNode &modelNode);
    void applyAnimationStates(const graphics::ModelNode &modelNode, size_t layers);

    /** Turns the look-at bone, if any, by one frame. */
    void applyLookAt(float dt);

    static AnimationBlendMode getAnimationBlendMode(int flags);

    // END Animation
};

} // namespace scene

} // namespace reone
