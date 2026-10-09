/*
 * Copyright (c) 2020-2026 The reone project contributors
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

#include "reone/input/event.h"

#include "minigame.h"
#include "minigameview.h"

namespace reone {

namespace graphics {

class ModelNode;

}

namespace scene {

class ModelSceneNode;
class SceneNode;

}

namespace game {

class Game;
class FirstPersonCamera;
struct ServicesView;

/**
 * Presentation of the swoop race. The race itself - speed, steering, pads,
 * obstacles, the countdown, the timer and the result - is the MiniGameSession
 * running the module's scripts; this view only loads the models, places them
 * from session state each tick, plays what the scripts ask for, and turns
 * local keys into the minigame input seam.
 */
class SwoopRace : public IMiniGameView {
public:
    SwoopRace(Game &game, ServicesView &services) :
        _game(game),
        _services(services) {
    }

    /**
     * @param obstaclePositions world placement of each session obstacle, in
     *        session obstacle order
     */
    bool start(MiniGameSession &session,
               const MinigameSpec &spec,
               const std::vector<glm::vec3> &obstaclePositions,
               FirstPersonCamera *camera);
    void stop();

    /** After each session step: place models, run queued animations, camera. */
    void update(float dt);

    bool handle(const input::Event &event);

    bool isActive() const { return _active; }

    // IMiniGameView

    void playMiniGameAnimation(const MiniGameActor &actor,
                               const std::string &name,
                               bool looping,
                               bool queue,
                               bool overlay) override;
    void removeMiniGameAnimation(const MiniGameActor &actor, const std::string &name) override;
    void playMiniGameSound(const std::string &resRef, const glm::vec3 &position) override;
    void onMiniGameActorDied(const MiniGameActor &actor) override;

    // END IMiniGameView

    // Diagnostics

    size_t modelCount() const;
    bool haveCameraHook() const { return _cameraHook != nullptr; }

private:
    struct ActorModels {
        std::vector<std::shared_ptr<scene::ModelSceneNode>> nodes; // [0] is the root
        float removeAfter {-1.0f}; // seconds until a dying actor leaves the scene
        bool removed {false};
    };

    Game &_game;
    ServicesView &_services;

    bool _active {false};
    MiniGameSession *_session {nullptr};
    FirstPersonCamera *_camera {nullptr};
    std::vector<ActorModels> _actors;
    scene::SceneNode *_cameraHook {nullptr};
    MiniGameAnimator _animator;

    bool _steerLeft {false};
    bool _steerRight {false};
    bool _fireKey {false};
    bool _fireButton {false};

    std::shared_ptr<scene::ModelSceneNode> loadModel(const std::string &resRef);
    void releaseActor(ActorModels &models);
    void placeActors();
    void updateCamera();
    void setCameraProjection(float fovDegrees, float nearClip, float farClip);
    void publishInput();
};

} // namespace game

} // namespace reone
