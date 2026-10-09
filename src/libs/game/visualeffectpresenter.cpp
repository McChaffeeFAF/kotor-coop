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

glm::mat4 VisualEffectPresenter::attachTransform(ModelSceneNode &target, AttachPoint point, bool keepWorldOrientation) {
    const glm::mat4 &root = target.absoluteTransform();
    glm::vec3 position(root[3]);
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

    std::pair<std::shared_ptr<Model>, AttachPoint> models[] {
        {desc.impHeadConNode, AttachPoint::HeadConjure},
        {desc.impImpactNode, AttachPoint::Impact},
        {desc.rootModel(), AttachPoint::Root}};
    for (auto &[model, point] : models) {
        if (!model || !ctx.graph) {
            continue;
        }
        Part part;
        part.node = ctx.graph->newModel(*model, ModelUsage::Projectile);
        part.point = point;
        auto &node = *part.node;
        if (once) {
            if (hasAnimation(node, "impact")) {
                playOnce(node, "impact");
            } else if (hasAnimation(node, "duration")) {
                playOnce(node, "duration");
            } else if (hasAnimation(node, "default")) {
                playOnce(node, "default");
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
    }
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
    debug(str(boost::format("vfx: present %u %s target=%u models=%u") % desc.id % desc.label % request->targetId % presented.parts.size()));
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
        glm::mat4 transform = root;
        if (target && part.point != AttachPoint::Root) {
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
        bool animated = !node.model().getAnimationNames().empty();
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
        std::remove_if(_active.begin(), _active.end(), [](auto &presented) {
            if (!presented.parts.empty()) {
                return false;
            }
            // A duration effect with no models (sound only) lives until it ends.
            bool duration = presented.request->desc->kind == VisualEffectDesc::Kind::Duration;
            return !duration || presented.endHandled || !presented.loopSound;
        }),
        _active.end());
    updateShake(dt, ctx);
}

} // namespace game

} // namespace reone
