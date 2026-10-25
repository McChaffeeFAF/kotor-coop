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

#include "../../camerastyle.h"

#include "../camera.h"

#include "reone/graphics/aabb.h"

namespace reone {

namespace game {

/**
 * Conversation camera for nodes without a camera animation or a static
 * camera: close-up, over-the-shoulder, wide and hold shots framed from the
 * speaker's and the listener's anchors. Behaviour: design note p2_cine.
 */
class DialogCamera : public Camera {
public:
    /** Resolved shot kinds (a node's angle 0 is resolved before it gets here). */
    enum class Angle {
        CloseUp = 1,
        OverShoulder = 2,
        Wide = 3,
        Hold = 4 // stay put and look at the speaker
    };

    struct Shot {
        Angle angle {Angle::CloseUp};
        /** Side of the speaker/listener line: 1 or 2. */
        int side {1};
        /** Blocked by the area: frame the close-up instead, with no range. */
        bool closeUpFallback {false};
        /** CU_PB_Range: how much further out the close-up sits, metres. */
        float closeUpRange {0.0f};
        /** Node CamHeightOffset / TarHeightOffset, metres. */
        float cameraHeightOffset {0.0f};
        float targetHeightOffset {0.0f};
    };

    struct Framing {
        glm::vec3 eye {0.0f};
        glm::vec3 target {0.0f};
    };

    DialogCamera(
        uint32_t id,
        CameraStyle style,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Camera(
            id,
            std::move(sceneName),
            game,
            services),
        _style(std::move(style)) {
    }

    void load();

    /** Anchors of the speaker and the listener; the shot follows them. */
    void setEndpoints(glm::vec3 speaker, glm::vec3 listener);
    void setShot(const Shot &shot);

    const Shot &shot() const { return _shot; }
    const Framing &framing() const { return _framing; }

    /**
     * Where `shot` puts the camera for these anchors. `holdEye` is where a
     * hold shot keeps the camera.
     */
    static Framing frame(const Shot &shot, glm::vec3 speaker, glm::vec3 listener, glm::vec3 holdEye);

    /**
     * Number of these sight lines the area blocks: each goes from one of
     * `from` to the camera of `shot`.
     */
    int countBlocked(const Shot &shot, glm::vec3 speaker, glm::vec3 listener, std::initializer_list<glm::vec3> from) const;

    /** Whether the area blocks `shot` as a node's shot is checked. */
    bool isBlocked(const Shot &shot, glm::vec3 speaker, glm::vec3 listener) const;

    /**
     * Whether the line from `from` to `to` enters `box` before reaching `to`.
     * A box that contains `from` (a speaker sitting in a chair, standing in a
     * console's bounds) does not block.
     */
    static bool lineEntersBox(const glm::vec3 &from, const glm::vec3 &to, const graphics::AABB &box);

    /** The side of the line with fewer blocked views for this pair: 1 or 2. */
    int chooseSide(glm::vec3 speaker, glm::vec3 listener) const;

private:
    CameraStyle _style;

    glm::vec3 _speakerPosition {0.0f};
    glm::vec3 _listenerPosition {0.0f};
    Shot _shot;
    Framing _framing;

    bool blocked(glm::vec3 from, glm::vec3 to) const;
    /**
     * The first closed door or placeable whose model box the line from
     * `from` to `to` enters (one that contains `from` does not count), or
     * nullptr. The walkmesh alone misses door leaves and set pieces.
     */
    const Object *blockingObject(glm::vec3 from, glm::vec3 to) const;
    void updateSceneNode();
    float projectionFovy() const override;
};

} // namespace game

} // namespace reone
