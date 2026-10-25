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

#include "reone/game/object/camera/dialog.h"

#include <cmath>

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/door.h"
#include "reone/game/object/module.h"
#include "reone/scene/node/model.h"
#include "reone/graphics/types.h"
#include "reone/scene/collision.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/camera.h"
#include "reone/system/logutil.h"

using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

static constexpr float kMinDialogCameraDistance = 0.0001f;

enum class DialogCameraWarning {
    CannotResolveEndpoints,
    InvalidResolvedEndpoints,
    InvalidDirection,
    InvalidTransformEndpoints,
    InvalidTransform,
    Count
};

static bool isFinite(const glm::vec3 &position) {
    return std::isfinite(position.x) &&
           std::isfinite(position.y) &&
           std::isfinite(position.z);
}

static bool isFinite(const glm::mat4 &transform) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(transform[col][row])) {
                return false;
            }
        }
    }
    return true;
}

static glm::vec3 fallbackDirection() {
    return glm::vec3(1.0f, 0.0f, 0.0f);
}

static void warnOnce(DialogCameraWarning warning, const char *message) {
    static bool warned[static_cast<int>(DialogCameraWarning::Count)] {};
    int index = static_cast<int>(warning);
    if (!warned[index]) {
        warned[index] = true;
        warn(message);
    }
}

static bool resolveEndpoints(glm::vec3 &listenerPosition, glm::vec3 &speakerPosition) {
    bool listenerFinite = isFinite(listenerPosition);
    bool speakerFinite = isFinite(speakerPosition);

    if (!listenerFinite && !speakerFinite) {
        warnOnce(DialogCameraWarning::CannotResolveEndpoints, "DialogCamera: cannot resolve camera endpoints");
        return false;
    }

    glm::vec3 fallbackDir(fallbackDirection());
    if (listenerFinite && !speakerFinite) {
        speakerPosition = listenerPosition + fallbackDir;
        return true;
    }
    if (!listenerFinite && speakerFinite) {
        listenerPosition = speakerPosition - fallbackDir;
        return true;
    }

    glm::vec3 listenerToSpeaker(speakerPosition - listenerPosition);
    float distance = glm::length(listenerToSpeaker);
    if (!std::isfinite(distance) || distance < kMinDialogCameraDistance) {
        // Some dialog/computer paths supply coincident endpoints; keep the
        // center stable while giving the camera a real direction.
        glm::vec3 center(listenerPosition);
        listenerPosition = center - 0.5f * fallbackDir;
        speakerPosition = center + 0.5f * fallbackDir;
    }
    return true;
}

void DialogCamera::load() {
    auto &scene = _services.scene.graphs.get(_sceneName);
    _sceneNode = scene.newCamera();
    rebuildProjection();
}

float DialogCamera::projectionFovy() const {
    return glm::radians(_style.viewAngle);
}

void DialogCamera::setEndpoints(glm::vec3 speaker, glm::vec3 listener) {
    if (_speakerPosition != speaker || _listenerPosition != listener) {
        _speakerPosition = speaker;
        _listenerPosition = listener;
        updateSceneNode();
    }
}

void DialogCamera::setShot(const Shot &shot) {
    _shot = shot;
    updateSceneNode();
}

// A vector turned about the vertical axis, counter-clockwise for positive
// degrees. Side 1 turns one way and side 2 the other.
static glm::vec3 turned(const glm::vec3 &v, float degrees) {
    float r = glm::radians(degrees);
    float c = std::cos(r);
    float s = std::sin(r);
    return glm::vec3(c * v.x - s * v.y, s * v.x + c * v.y, v.z);
}

static glm::vec3 normalizedOr(const glm::vec3 &v, const glm::vec3 &fallback) {
    float length = glm::length(v);
    return (std::isfinite(length) && length > kMinDialogCameraDistance) ? v / length : fallback;
}

DialogCamera::Framing DialogCamera::frame(const Shot &shot, glm::vec3 speaker, glm::vec3 listener, glm::vec3 holdEye) {
    Framing framing;
    bool closeUp = shot.angle == Angle::CloseUp || shot.closeUpFallback;
    // Coincident anchors: the listener is taken off to one side and the
    // close-up frames the speaker alone.
    if (glm::distance(speaker, listener) < kMinDialogCameraDistance) {
        listener = speaker + glm::vec3(1.5f, 1.5f, 0.1f);
        closeUp = true;
    }
    float sign = shot.side == 2 ? -1.0f : 1.0f;

    if (shot.angle == Angle::Hold && !shot.closeUpFallback) {
        framing.eye = holdEye;
        framing.target = speaker + glm::vec3(0.0f, 0.0f, shot.targetHeightOffset);
        return framing;
    }

    if (closeUp) {
        float range = shot.closeUpFallback ? 0.0f : shot.closeUpRange;
        glm::vec3 level(listener.x, listener.y, speaker.z);
        glm::vec3 toSpeaker(speaker - level);
        glm::vec3 across(normalizedOr(toSpeaker, fallbackDirection()));
        glm::vec3 target(speaker - 0.2f * across);
        target.z += -0.04f + shot.targetHeightOffset - 0.2f * range;
        glm::vec3 view(normalizedOr(turned(toSpeaker, sign * 30.0f), fallbackDirection()));
        framing.target = target;
        framing.eye = target - (range + 0.5f) * view;
        framing.eye.z += shot.cameraHeightOffset + 0.2f * range;
        return framing;
    }

    // Over the shoulder and wide: distances scale with the separation.
    bool wide = shot.angle == Angle::Wide;
    float separation = glm::distance(listener, speaker);
    float along = wide ? 0.5f : 0.3f;
    float yaw = wide ? 90.0f : 30.0f;
    float back = wide ? 1.5f : 0.8f;
    glm::vec3 a(speaker);
    glm::vec3 b(listener);
    if (!wide) {
        a.z += -0.1f * separation;
        b.z += -0.05f * separation;
    }
    glm::vec3 target(b + along * (a - b));
    target.z += (wide ? -0.2f * separation : 0.0f) + shot.targetHeightOffset;
    glm::vec3 view(turned(a - b, sign * yaw));
    glm::vec3 eye(target - back * view);
    if (!wide) {
        eye -= 0.15f * normalizedOr(view, fallbackDirection());
    }
    eye.z += (wide ? 0.3f * separation : 0.0f) + shot.cameraHeightOffset;
    framing.eye = eye;
    framing.target = target;
    return framing;
}

bool DialogCamera::blocked(glm::vec3 from, glm::vec3 to) const {
    if (!isFinite(from) || !isFinite(to) || glm::distance(from, to) < kMinDialogCameraDistance) {
        return false;
    }
    Collision collision;
    auto &scene = _services.scene.graphs.get(_sceneName);
    if (scene.testLineOfSight(from, to, collision)) {
        return true;
    }
    if (const Object *object = blockingObject(from, to)) {
        if (_game.cutsceneAuditEnabled()) {
            info(str(boost::format("Cutscene audit: dialog camera avoids %s") % object->tag()));
        }
        return true;
    }
    return false;
}

const Object *DialogCamera::blockingObject(glm::vec3 from, glm::vec3 to) const {
    auto module = _game.module();
    if (!module || !module->area()) {
        return nullptr;
    }
    for (const auto &object : module->area()->objects()) {
        if (!object || !object->isRuntimeLive()) {
            continue;
        }
        if (object->type() == ObjectType::Door) {
            // Open: the doorway is clear. Opening: the leaves still stand.
            if (static_cast<const Door &>(*object).isOpen()) {
                continue;
            }
        } else if (object->type() != ObjectType::Placeable) {
            continue;
        }
        auto node = object->sceneNode();
        if (!node || node->type() != scene::SceneNodeType::Model || !node->isEnabled()) {
            continue;
        }
        if (lineEntersBox(from, to, node->aabb() * node->absoluteTransform())) {
            return object.get();
        }
    }
    return nullptr;
}

bool DialogCamera::lineEntersBox(const glm::vec3 &from, const glm::vec3 &to, const graphics::AABB &box) {
    if (box.isDegenerate() || box.contains(from)) {
        return false;
    }
    glm::vec3 delta(to - from);
    float length = glm::length(delta);
    if (!(length > 1e-3f)) {
        return false;
    }
    glm::vec3 invDir(1.0f / (delta / length));
    float distance = 0.0f;
    return box.raycast(from, invDir, length, distance) && distance < length;
}

int DialogCamera::countBlocked(const Shot &shot, glm::vec3 speaker, glm::vec3 listener, std::initializer_list<glm::vec3> from) const {
    glm::vec3 eye(frame(shot, speaker, listener, glm::vec3(0.0f)).eye);
    int count = 0;
    for (const auto &point : from) {
        count += blocked(point, eye) ? 1 : 0;
    }
    return count;
}

bool DialogCamera::isBlocked(const Shot &shot, glm::vec3 speaker, glm::vec3 listener) const {
    if (shot.angle == Angle::Hold) {
        return false;
    }
    if (shot.angle == Angle::Wide) {
        glm::vec3 middle(0.5f * (speaker + listener));
        return countBlocked(shot, speaker, listener, {middle, speaker, listener}) > 0;
    }
    return countBlocked(shot, speaker, listener, {speaker}) > 0;
}

int DialogCamera::chooseSide(glm::vec3 a, glm::vec3 b) const {
    glm::vec3 middle(0.5f * (a + b));
    auto count = [&](int side) {
        Shot wide;
        wide.angle = Angle::Wide;
        wide.side = side;
        Shot shoulder;
        shoulder.angle = Angle::OverShoulder;
        shoulder.side = side;
        // The other one speaking mirrors the side, so the camera stays on
        // the same side of the pair.
        Shot mirrored(shoulder);
        mirrored.side = side == 1 ? 2 : 1;
        return countBlocked(wide, a, b, {middle, a, b}) +
               countBlocked(shoulder, a, b, {a}) +
               countBlocked(mirrored, b, a, {b});
    };
    int second = count(2);
    int first = count(1);
    return first < second ? 1 : 2;
}

void DialogCamera::updateSceneNode() {
    static glm::vec3 up(0.0f, 0.0f, 1.0f);

    glm::vec3 listenerPosition(_listenerPosition);
    glm::vec3 speakerPosition(_speakerPosition);
    if (!resolveEndpoints(listenerPosition, speakerPosition)) {
        return;
    }
    Framing framing(frame(_shot, speakerPosition, listenerPosition, _framing.eye));
    if (!isFinite(framing.eye) || !isFinite(framing.target) ||
        glm::distance(framing.eye, framing.target) < kMinDialogCameraDistance) {
        warnOnce(DialogCameraWarning::InvalidTransformEndpoints, "DialogCamera: invalid camera transform endpoints");
        return;
    }
    glm::mat4 transform(glm::inverse(glm::lookAt(framing.eye, framing.target, up)));
    if (!isFinite(transform)) {
        warnOnce(DialogCameraWarning::InvalidTransform, "DialogCamera: invalid camera transform");
        return;
    }
    _framing = framing;
    if (_sceneNode) {
        _sceneNode->setLocalTransform(transform);
    }
}

} // namespace game

} // namespace reone
