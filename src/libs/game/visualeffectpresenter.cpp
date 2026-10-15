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

#include "reone/game/visualeffectpresenter.h"

#include "reone/audio/mixer.h"
#include "reone/audio/source.h"
#include "reone/game/visualeffects.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/scene/collision.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/model.h"
#include "reone/scene/node/modelnode.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

static constexpr float kNoAnimationLifetime = 2.0f; // a model without animations shows this long (F)
static constexpr float kShakeDefaultDuration = 0.5f;
static constexpr float kShakeLight = 0.04f; // metres
static constexpr float kShakeHeavy = 0.12f;
static constexpr float kShakeFalloff = 30.0f; // no shake beyond this distance from the camera

static bool hasAnimation(ModelSceneNode &node, const char *name) {
    return static_cast<bool>(node.model().getAnimation(name));
}

static void playOnce(ModelSceneNode &node, const char *name) {
    node.playAnimation(name, nullptr, AnimationProperties());
}

static void playLooped(ModelSceneNode &node, const char *name) {
    node.playAnimation(name, nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
}

static glm::mat4 rotationOf(const glm::mat4 &transform) {
    glm::mat4 result(1.0f);
    for (int i = 0; i < 3; ++i) {
        glm::vec3 axis(transform[i]);
        float len = glm::length(axis);
        result[i] = glm::vec4(len > 1e-6f ? axis / len : axis, 0.0f);
    }
    return result;
}

// The medal (v_medal_dur) is authored upright in model space (its node turns
// the flat medal up, ribbon on top) with its one face towards -Y, so it is
// turned about to face out of the target's chest; it is pinned this far out
// from the impact node in the target's frame (models face +Y, their left is
// -X). Our placement (design note v15_vfx_leftovers).
static const glm::vec3 kMedalOffset(-0.08f, 0.14f, 0.10f);

// A node of the target or of the head model attached at its head hook (K1's
// creature heads are models of their own).
static ModelNodeSceneNode *findBodyOrHeadNode(ModelSceneNode &target, const std::string &name) {
    if (auto node = target.getNodeByName(name)) {
        return node;
    }
    if (auto head = target.getAttachment("headhook"); head && head->type() == SceneNodeType::Model) {
        return static_cast<ModelSceneNode *>(head)->getNodeByName(name);
    }
    return nullptr;
}

// Revan's masks: the supermodels carry a hook for each, revmask1hook under the
// right hand (the mask held) and revmask2hook under head_g (the mask worn).
static glm::mat4 maskTransform(ModelSceneNode &target, const std::string &hook) {
    if (auto node = findBodyOrHeadNode(target, hook)) {
        return glm::translate(node->origin()) * rotationOf(node->absoluteTransform());
    }
    return VisualEffectPresenter::attachTransform(target, VisualEffectPresenter::AttachPoint::Head, false);
}

glm::mat4 VisualEffectPresenter::attachTransform(ModelSceneNode &target, AttachPoint point, bool keepWorldOrientation) {
    const glm::mat4 &root = target.absoluteTransform();
    glm::vec3 position(root[3]);
    if (point == AttachPoint::Head) {
        // The masks (v_revmask1/2_dur) are authored on the head hook: from the
        // neck up, facing +Y. They turn with the head.
        ModelNodeSceneNode *node = target.getNodeByName("headhook");
        if (!node) {
            node = target.getNodeByName("head");
        }
        if (node) {
            return glm::translate(node->origin()) * rotationOf(node->absoluteTransform());
        }
        point = AttachPoint::Impact;
    }
    if (point == AttachPoint::AboveHead) {
        // The fizzle (v_fizzle_imp): a small burst of flares, a little over
        // the head hook, where it is not hidden inside the body. Our choice.
        glm::vec3 above = position + glm::vec3(0.0f, 0.0f, 1.9f);
        if (auto node = target.getNodeByName("headhook")) {
            above = node->origin() + glm::vec3(0.0f, 0.0f, 0.3f);
        }
        return glm::translate(above) * (keepWorldOrientation ? glm::mat4(1.0f) : rotationOf(root));
    }
    if (point == AttachPoint::Chest) {
        glm::mat4 rotation = rotationOf(root);
        glm::vec3 chest = position + glm::vec3(0.0f, 0.0f, 1.2f);
        if (auto node = target.getNodeByName("impact")) {
            chest = node->origin();
        }
        glm::mat4 aboutFace(glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f)));
        return glm::translate(chest + glm::vec3(rotation * glm::vec4(kMedalOffset, 0.0f))) * rotation * aboutFace;
    }
    if (point != AttachPoint::Root) {
        // Head-conjure falls back to impact, impact to the root.
        ModelNodeSceneNode *node = nullptr;
        if (point == AttachPoint::HeadConjure) {
            node = target.getNodeByName("headconjure");
        }
        if (!node) {
            node = target.getNodeByName("impact");
        }
        if (node) {
            position = node->origin();
        }
    }
    // Models turn with the target's facing, not with the bone they sit on.
    glm::mat4 rotation = keepWorldOrientation ? glm::mat4(1.0f) : rotationOf(root);
    return glm::translate(position) * rotation;
}

// A named hook on a model, as a world point. A placeable names its hooks
// after its model without the "plc_" prefix ("PwrCond_impact" on
// PLC_PwrCond), so "<name>_impact" is tried after the plain names.
static std::optional<glm::vec3> hookPoint(ModelSceneNode &model, std::initializer_list<const char *> names) {
    for (auto name : names) {
        if (auto node = model.getNodeByName(name)) {
            return node->origin();
        }
    }
    return std::nullopt;
}

static std::optional<glm::vec3> placeableImpact(ModelSceneNode &model) {
    std::string name(model.model().name());
    if (name.size() > 4 && boost::istarts_with(name, "plc_")) {
        name = name.substr(4);
    }
    for (auto &candidate : {name + "_impact", boost::to_lower_copy(name) + "_impact"}) {
        if (auto node = model.getNodeByName(candidate)) {
            return node->origin();
        }
    }
    return std::nullopt;
}

static glm::vec3 aboveRoot(ModelSceneNode &model) {
    return glm::vec3(model.absoluteTransform()[3]) + glm::vec3(0.0f, 0.0f, 1.0f);
}

glm::vec3 VisualEffectPresenter::beamTargetPoint(ModelSceneNode &target) {
    if (auto point = hookPoint(target, {"impact"})) {
        return *point;
    }
    if (auto point = placeableImpact(target)) {
        return *point;
    }
    return aboveRoot(target);
}

glm::vec3 VisualEffectPresenter::beamSourcePoint(ModelSceneNode &source, int bodyNode) {
    std::optional<glm::vec3> point;
    switch (static_cast<BodyNode>(bodyNode)) {
    case BodyNode::Hand:
    case BodyNode::HandRight:
        point = hookPoint(source, {"rhand", "handconjure"});
        break;
    case BodyNode::HandLeft:
        point = hookPoint(source, {"lhand", "handconjure"});
        break;
    case BodyNode::Head:
        point = hookPoint(source, {"headconjure", "headhook"});
        break;
    case BodyNode::Chest:
    default:
        break;
    }
    if (point) {
        return *point;
    }
    // The chest, and any node a placeable or door lacks: its impact hook.
    return beamTargetPoint(source);
}

// Programmed body overlays (design note v14_programmed_effects, section 4).
static constexpr float kOverlayFadeIn = 0.25f;
static constexpr float kOverlayFadeOut = 0.4f;
static constexpr float kOverlayImpactLifetime = 1.5f;
static constexpr float kOverlayImpactFadeIn = 0.1f;
static constexpr float kOverlayImpactFadeOut = 0.6f;

float VisualEffectPresenter::overlayStrength(bool lasting, float age, float sinceEnd) {
    if (lasting) {
        float in = glm::clamp(age / kOverlayFadeIn, 0.0f, 1.0f);
        float out = sinceEnd < 0.0f ? 1.0f : glm::clamp(1.0f - sinceEnd / kOverlayFadeOut, 0.0f, 1.0f);
        return in * out;
    }
    float in = glm::clamp(age / kOverlayImpactFadeIn, 0.0f, 1.0f);
    float out = glm::clamp((kOverlayImpactLifetime - age) / kOverlayImpactFadeOut, 0.0f, 1.0f);
    return in * out;
}

bool VisualEffectPresenter::overlayDone(bool lasting, float age, float sinceEnd) {
    if (lasting) {
        return sinceEnd >= kOverlayFadeOut;
    }
    return age >= kOverlayImpactLifetime;
}

void VisualEffectPresenter::begin(std::shared_ptr<Request> request) {
    if (!request || !request->desc) {
        return;
    }
    _pending.push_back(std::move(request));
}

size_t VisualEffectPresenter::numModels() const {
    size_t count = 0;
    for (auto &presented : _active) {
        count += presented.parts.size();
    }
    return count;
}

std::vector<ModelSceneNode *> VisualEffectPresenter::models() const {
    std::vector<ModelSceneNode *> result;
    for (auto &presented : _active) {
        for (auto &part : presented.parts) {
            if (part.node) {
                result.push_back(part.node.get());
            }
        }
    }
    return result;
}

void VisualEffectPresenter::removePart(Part &part) {
    if (part.node && _graph) {
        _graph->removeRoot(*part.node);
    }
    part.node.reset();
}

void VisualEffectPresenter::clear() {
    for (auto &presented : _active) {
        for (auto &part : presented.parts) {
            removePart(part);
        }
        if (presented.loopSound) {
            presented.loopSound->stop();
        }
    }
    _active.clear();
    _pending.clear();
    _overlayTargets.clear(); // the models go with the area
    _motion.clear();
    _shakes.clear();
    _shakeOffset = glm::vec3(0.0f);
}

void VisualEffectPresenter::start(const std::shared_ptr<Request> &request, const Context &ctx) {
    const VisualEffectDesc &desc = *request->desc;
    bool duration = desc.kind == VisualEffectDesc::Kind::Duration;
    // A duration effect that ended before it was ever drawn (applied as
    // instant) is shown once, like a fire-and-forget one.
    bool once = !duration || request->ended;

    Presented presented;
    presented.request = request;

    auto addPart = [&](const std::shared_ptr<Model> &model, AttachPoint point) {
        if (!model || !ctx.graph) {
            return;
        }
        Part part;
        part.node = ctx.graph->newModel(*model, ModelUsage::Projectile);
        part.point = point;
        auto &node = *part.node;
        // A cast-hand visual animates by its own clip (v_con_dark "cast01").
        bool castClip = point == AttachPoint::Hand &&
                        !hasAnimation(node, "impact") && !hasAnimation(node, "duration") &&
                        !hasAnimation(node, "default") && hasAnimation(node, "cast01");
        if (castClip) {
            if (once) {
                playOnce(node, "cast01");
                part.phase = Phase::Once;
            } else {
                playLooped(node, "cast01");
                part.phase = Phase::Duration;
            }
        } else if (once) {
            if (hasAnimation(node, "impact")) {
                playOnce(node, "impact");
            } else if (hasAnimation(node, "duration")) {
                playOnce(node, "duration");
            } else if (hasAnimation(node, "default")) {
                playOnce(node, "default");
            } else {
                // Its animations are none of these (a beam's "conjure01"):
                // nothing plays, so it shows for the fixed lifetime.
                part.unanimated = true;
            }
            part.phase = Phase::Once;
        } else if (hasAnimation(node, "impact")) {
            playOnce(node, "impact");
            part.phase = Phase::Impact;
        } else {
            if (hasAnimation(node, "duration")) {
                playLooped(node, "duration");
            } else if (hasAnimation(node, "default")) {
                playLooped(node, "default");
            }
            part.phase = Phase::Duration;
        }
        presented.parts.push_back(std::move(part));
    };
    addPart(desc.impHeadConNode, AttachPoint::HeadConjure);
    addPart(desc.impImpactNode, AttachPoint::Impact);
    addPart(desc.rootModel(), AttachPoint::Root);
    // A programmed effect drawn with its own model (fizzle, medal, masks).
    if (desc.progfxModel && request->targetId != 0) {
        AttachPoint point = AttachPoint::Impact;
        if (desc.progfxPlace == VisualEffectDesc::ProgfxPlace::AboveHead) {
            point = AttachPoint::AboveHead;
        } else if (desc.progfxPlace == VisualEffectDesc::ProgfxPlace::Chest) {
            point = AttachPoint::Chest;
        } else if (desc.progfxPlace == VisualEffectDesc::ProgfxPlace::Head) {
            point = AttachPoint::Head;
        }
        addPart(desc.progfxModel, point);
    }
    // A programmed beam: its model leaves from the effector and reaches the
    // target. Without an effector (a plain EffectVisualEffect of a beam row)
    // there is nothing to draw it from.
    if (request->sourceId != 0 && request->sourceId != request->targetId) {
        addPart(desc.beamModel, AttachPoint::BeamSource);
    }
    if (request->targetId != 0) {
        addPart(desc.handModel, AttachPoint::Hand);
    }
    presented.lasting = duration && !request->ended;
    _graph = ctx.graph;

    place(presented, ctx);
    for (auto &part : presented.parts) {
        ctx.graph->addRoot(part.node);
    }

    glm::vec3 position(presented.lastRoot[3]);
    if (ctx.mixer) {
        if (desc.soundImpact) {
            ctx.mixer->play(desc.soundImpact, audio::AudioType::Sound, 1.0f, false, position);
        }
        if (desc.soundDuration && !(duration && request->ended)) {
            presented.loopSound = ctx.mixer->play(desc.soundDuration, audio::AudioType::Sound, 1.0f, true, position);
        }
    }
    if (desc.shakeType > 0) {
        Shake shake;
        shake.position = position;
        shake.amplitude = desc.shakeType >= 2 ? kShakeHeavy : kShakeLight;
        shake.duration = desc.shakeDuration > 0.0f ? desc.shakeDuration : kShakeDefaultDuration;
        _shakes.push_back(shake);
    }
    // rumblepattern / rumblecutoff: no rumble device is driven.

    if (duration && request->ended) {
        presented.endHandled = true;
        if (ctx.mixer && desc.soundCessation) {
            ctx.mixer->play(desc.soundCessation, audio::AudioType::Sound, 1.0f, false, position);
        }
    }
    debug(str(boost::format("vfx: present %u %s target=%u models=%u%s%s") % desc.id % desc.label % request->targetId % presented.parts.size() %
               (request->sourceId != 0 ? str(boost::format(" source=%u beam=%u") % request->sourceId % (desc.beamModel ? 1 : 0)) : std::string()) %
               (desc.bodyLook ? str(boost::format(" look=%s%s") % desc.bodyLook->texture % (desc.bodyTexture ? "" : "(missing)")) : std::string())));
    _active.push_back(std::move(presented));
}

void VisualEffectPresenter::place(Presented &presented, const Context &ctx) {
    const Request &request = *presented.request;
    const VisualEffectDesc &desc = *request.desc;
    ModelSceneNode *target = nullptr;
    if (request.targetId != 0 && ctx.modelOf && !presented.targetLost) {
        target = ctx.modelOf(request.targetId);
        if (!target) {
            presented.targetLost = true;
        }
    }
    glm::mat4 root;
    if (target) {
        root = attachTransform(*target, AttachPoint::Root, desc.orientationOff);
    } else if (request.targetId == 0) {
        root = glm::translate(request.location) *
               (desc.orientationOff ? glm::mat4(1.0f) : glm::rotate(glm::mat4(1.0f), request.facing, glm::vec3(0.0f, 0.0f, 1.0f)));
    } else {
        root = presented.lastRoot; // target gone: stay where it was last
    }
    if (desc.orientWithGround && ctx.graph) {
        Collision collision;
        if (ctx.graph->testElevation(glm::vec3(root[3]), collision)) {
            glm::vec3 up(glm::normalize(collision.normal));
            glm::vec3 forward(rotationOf(root)[1]);
            glm::vec3 right(glm::cross(forward, up));
            if (glm::length(right) > 1e-5f) {
                right = glm::normalize(right);
                forward = glm::cross(up, right);
                glm::mat4 tilt(1.0f);
                tilt[0] = glm::vec4(right, 0.0f);
                tilt[1] = glm::vec4(forward, 0.0f);
                tilt[2] = glm::vec4(up, 0.0f);
                tilt[3] = glm::vec4(collision.intersection, 1.0f);
                root = tilt;
            }
        }
    }
    presented.lastRoot = root;

    for (auto &part : presented.parts) {
        if (!part.node) {
            continue;
        }
        if (part.point == AttachPoint::BeamSource) {
            placeBeam(presented, part, target, ctx);
            continue;
        }
        glm::mat4 transform = root;
        if (target && part.point == AttachPoint::Hand) {
            transform = glm::translate(beamSourcePoint(*target, static_cast<int>(BodyNode::HandRight))) * rotationOf(root);
        } else if (target && part.point == AttachPoint::Head) {
            transform = maskTransform(*target, desc.progfxDuration == 1701 ? "revmask1hook" : "revmask2hook");
        } else if (target && part.point != AttachPoint::Root) {
            transform = attachTransform(*target, part.point, desc.orientationOff);
        } else if (!target && part.point != AttachPoint::Root && request.targetId != 0) {
            transform = part.node->localTransform(); // hold the last attach point
        }
        part.node->setLocalTransform(transform);
    }
    if (presented.loopSound) {
        presented.loopSound->setPosition(glm::vec3(root[3]));
    }
}

void VisualEffectPresenter::placeBeam(Presented &presented, Part &part, ModelSceneNode *target, const Context &ctx) {
    const Request &request = *presented.request;
    ModelSceneNode *source = nullptr;
    if (ctx.modelOf && !presented.sourceLost) {
        source = ctx.modelOf(request.sourceId);
        if (!source) {
            presented.sourceLost = true;
        }
    }
    glm::vec3 from = source ? beamSourcePoint(*source, request.sourceBodyNode) : presented.lastBeamSource;
    presented.lastBeamSource = from;
    std::optional<glm::vec3> to;
    if (target) {
        to = beamTargetPoint(*target);
    } else if (request.targetId == 0) {
        to = request.location;
    }
    // Turn the model to face along the beam, as a caster faces its target
    // (models face +Y); keep the last heading once the target is gone.
    glm::mat4 rotation = rotationOf(part.node->localTransform());
    if (to) {
        glm::vec3 dir(*to - from);
        if (glm::length(glm::vec2(dir)) > 1e-4f) {
            float heading = std::atan2(-dir.x, dir.y);
            rotation = glm::rotate(glm::mat4(1.0f), heading, glm::vec3(0.0f, 0.0f, 1.0f));
        }
    }
    part.node->setLocalTransform(glm::translate(from) * rotation);
    if (to) {
        part.node->aimReattachableReferences(*to);
    }
}

void VisualEffectPresenter::finish(Presented &presented, const Context &ctx) {
    // The simulation ended a duration effect: cessation, then gone.
    presented.endHandled = true;
    const VisualEffectDesc &desc = *presented.request->desc;
    if (presented.loopSound) {
        presented.loopSound->stop();
        presented.loopSound.reset();
    }
    if (ctx.mixer && desc.soundCessation) {
        ctx.mixer->play(desc.soundCessation, audio::AudioType::Sound, 1.0f, false, glm::vec3(presented.lastRoot[3]));
    }
    for (auto &part : presented.parts) {
        if (!part.node) {
            continue;
        }
        if (hasAnimation(*part.node, "cessation")) {
            playOnce(*part.node, "cessation");
            part.phase = Phase::Cessation;
        } else {
            removePart(part);
        }
    }
}

void VisualEffectPresenter::advance(Presented &presented, float dt, const Context &ctx) {
    const VisualEffectDesc &desc = *presented.request->desc;
    bool duration = desc.kind == VisualEffectDesc::Kind::Duration;
    if (duration && !presented.endHandled && (presented.request->ended || presented.targetLost)) {
        finish(presented, ctx);
    }
    for (auto &part : presented.parts) {
        if (!part.node) {
            continue;
        }
        part.age += dt;
        auto &node = *part.node;
        bool animated = !part.unanimated && !node.model().getAnimationNames().empty();
        switch (part.phase) {
        case Phase::Impact:
            if (node.isAnimationFinished()) {
                if (hasAnimation(node, "duration")) {
                    playLooped(node, "duration");
                } else if (hasAnimation(node, "default")) {
                    playLooped(node, "default");
                }
                part.phase = Phase::Duration;
            }
            break;
        case Phase::Duration:
            break;
        case Phase::Cessation:
            if (node.isAnimationFinished()) {
                removePart(part);
            }
            break;
        case Phase::Once:
        default:
            if (animated ? node.isAnimationFinished() : part.age >= kNoAnimationLifetime) {
                removePart(part);
            }
            break;
        }
    }
    presented.parts.erase(
        std::remove_if(presented.parts.begin(), presented.parts.end(), [](auto &part) { return !part.node; }),
        presented.parts.end());
}

void VisualEffectPresenter::updateShake(float dt, const Context &ctx) {
    float amplitude = 0.0f;
    for (auto &shake : _shakes) {
        shake.age += dt;
        if (shake.age >= shake.duration) {
            continue;
        }
        float fade = 1.0f - shake.age / shake.duration;
        float distanceScale = 1.0f;
        if (ctx.cameraPosition) {
            float distance = glm::distance(*ctx.cameraPosition, shake.position);
            distanceScale = glm::max(0.0f, 1.0f - distance / kShakeFalloff);
        }
        amplitude = glm::max(amplitude, shake.amplitude * fade * distanceScale);
    }
    _shakes.erase(
        std::remove_if(_shakes.begin(), _shakes.end(), [](auto &shake) { return shake.age >= shake.duration; }),
        _shakes.end());
    if (amplitude > 0.0f) {
        _shakeOffset = amplitude * glm::vec3(
                                       cosmeticRandomFloat(-1.0f, 1.0f),
                                       cosmeticRandomFloat(-1.0f, 1.0f),
                                       cosmeticRandomFloat(-1.0f, 1.0f));
    } else {
        _shakeOffset = glm::vec3(0.0f);
    }
}

void VisualEffectPresenter::update(float dt, const Context &ctx) {
    if (ctx.graph) {
        _graph = ctx.graph;
    }
    std::vector<std::shared_ptr<Request>> pending;
    pending.swap(_pending);
    for (auto &request : pending) {
        start(request, ctx);
    }
    for (auto &presented : _active) {
        place(presented, ctx);
        advance(presented, dt, ctx);
        presented.age += dt;
        if (presented.lasting && (presented.request->ended || presented.targetLost)) {
            presented.sinceEnd = presented.sinceEnd < 0.0f ? 0.0f : presented.sinceEnd + dt;
        }
    }
    for (auto &presented : _active) {
        bool duration = presented.request->desc->kind == VisualEffectDesc::Kind::Duration;
        if (presented.parts.empty() && presented.loopSound && (!duration || presented.endHandled)) {
            // A fire-and-forget loop sound lasts while its models play.
            presented.loopSound->stop();
            presented.loopSound.reset();
        }
    }
    _active.erase(
        std::remove_if(_active.begin(), _active.end(), [this](auto &presented) {
            if (!presented.parts.empty() || overlayLive(presented)) {
                return false;
            }
            // A duration effect with no models (sound only) lives until it ends.
            bool duration = presented.request->desc->kind == VisualEffectDesc::Kind::Duration;
            return !duration || presented.endHandled || !presented.loopSound;
        }),
        _active.end());
    updateOverlays(dt, ctx);
    updateShake(dt, ctx);
}

bool VisualEffectPresenter::overlayLive(const Presented &presented) const {
    const VisualEffectDesc &desc = *presented.request->desc;
    bool look = desc.bodyLook && desc.bodyTexture;
    if ((!look && !desc.speedTrail) || presented.request->targetId == 0 || presented.targetLost) {
        return false;
    }
    return !overlayDone(presented.lasting, presented.age, presented.sinceEnd);
}

// The speed effect (design note v15_vfx_leftovers): afterimages a little
// behind a moving target, blue-white, fainter further back; a standing one
// gives off a slow outward pulse instead. Our design: K1's progfx 1500/1601
// name a motion blur that no shipped data describes.
static const glm::vec3 kSpeedTint(1.1f, 1.6f, 2.4f); // over 1: the texture brightened, blue-white
static constexpr int kSpeedGhosts = 3;
static constexpr float kSpeedGhostSpacing = 0.05f;      // seconds of travel between afterimages
static constexpr float kSpeedMovingFrom = 0.5f;         // m/s: slower counts as standing
static constexpr float kSpeedMovingFull = 2.0f;         // m/s: the trail at full strength
static constexpr float kSpeedPulseRate = 1.6f;          // standing pulses a second
static constexpr float kSpeedVelocitySmoothing = 0.08f; // seconds

std::vector<BodyOverlay> VisualEffectPresenter::speedGhosts(const glm::vec3 &velocity, float strength, float time) {
    std::vector<BodyOverlay> ghosts;
    if (strength <= 0.0f) {
        return ghosts;
    }
    float speed = glm::length(velocity);
    float moving = glm::clamp((speed - kSpeedMovingFrom) / (kSpeedMovingFull - kSpeedMovingFrom), 0.0f, 1.0f);
    if (moving > 0.0f) {
        for (int i = 1; i <= kSpeedGhosts; ++i) {
            BodyOverlay ghost;
            ghost.ghost = true;
            ghost.offset = -velocity * (kSpeedGhostSpacing * static_cast<float>(i));
            ghost.tint = kSpeedTint;
            ghost.inflate = 0.005f;
            ghost.strength = strength * moving * (0.75f - 0.2f * static_cast<float>(i));
            ghost.time = time;
            ghosts.push_back(ghost);
        }
    }
    if (moving < 1.0f) {
        float phase = glm::fract(time * kSpeedPulseRate);
        BodyOverlay pulse;
        pulse.ghost = true;
        pulse.tint = kSpeedTint;
        pulse.inflate = 0.01f + 0.06f * phase;
        pulse.strength = strength * (1.0f - moving) * 0.8f * (1.0f - phase);
        pulse.time = time;
        ghosts.push_back(pulse);
    }
    return ghosts;
}

void VisualEffectPresenter::updateOverlays(float dt, const Context &ctx) {
    struct Look {
        std::vector<BodyOverlay> overlays;
        float opacity {1.0f};
    };
    // How fast each speed-effect target moves, smoothed over a few frames
    // (its model moves by simulation ticks, not every rendered frame).
    std::map<uint32_t, Motion> motion;
    for (auto &presented : _active) {
        if (!presented.request->desc->speedTrail || !overlayLive(presented)) {
            continue;
        }
        uint32_t id = presented.request->targetId;
        auto model = ctx.modelOf ? ctx.modelOf(id) : nullptr;
        if (!model || motion.count(id)) {
            continue;
        }
        glm::vec3 position(model->absoluteTransform()[3]);
        Motion next;
        next.position = position;
        auto previous = _motion.find(id);
        if (previous != _motion.end()) {
            next.velocity = previous->second.velocity;
            if (dt > 0.0f) {
                glm::vec3 raw((position - previous->second.position) / dt);
                next.velocity = glm::mix(next.velocity, raw, 1.0f - std::exp(-dt / kSpeedVelocitySmoothing));
            }
        }
        motion[id] = next;
    }
    _motion = std::move(motion);

    std::map<uint32_t, Look> looks;
    for (auto &presented : _active) {
        if (!overlayLive(presented)) {
            continue;
        }
        const VisualEffectDesc &desc = *presented.request->desc;
        float strength = overlayStrength(presented.lasting, presented.age, presented.sinceEnd);
        auto &target = looks[presented.request->targetId];
        if (desc.speedTrail) {
            auto it = _motion.find(presented.request->targetId);
            glm::vec3 velocity = it != _motion.end() ? it->second.velocity : glm::vec3(0.0f);
            for (auto &ghost : speedGhosts(velocity, strength, presented.age)) {
                target.overlays.push_back(ghost);
            }
        }
        if (!desc.bodyLook || !desc.bodyTexture) {
            continue;
        }
        const auto &look = *desc.bodyLook;
        BodyOverlay overlay;
        overlay.texture = desc.bodyTexture.get();
        overlay.strength = strength;
        overlay.time = presented.age;
        target.overlays.push_back(overlay);
        target.opacity = glm::min(target.opacity, glm::mix(1.0f, look.opacity, strength));
    }
    std::set<uint32_t> targets;
    for (auto &[id, look] : looks) {
        targets.insert(id);
        if (auto model = ctx.modelOf ? ctx.modelOf(id) : nullptr) {
            model->setBodyOverlays(std::move(look.overlays), look.opacity);
        }
    }
    for (uint32_t id : _overlayTargets) {
        if (targets.count(id) == 0 && ctx.modelOf) {
            if (auto model = ctx.modelOf(id)) {
                model->setBodyOverlays({}, 1.0f);
            }
        }
    }
    _overlayTargets = std::move(targets);
}

} // namespace game

} // namespace reone
