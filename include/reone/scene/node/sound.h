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

#include "reone/audio/source.h"

#include "../node.h"

namespace reone {

namespace scene {

class SoundSceneNode : public SceneNode {
public:
    SoundSceneNode(
        ISceneGraph &sceneGraph,
        graphics::GraphicsServices &graphicsSvc,
        audio::AudioServices &audioSvc,
        resource::ResourceServices &resourceSvc) :
        SceneNode(
            SceneNodeType::Sound,
            sceneGraph,
            graphicsSvc,
            audioSvc,
            resourceSvc) {
    }

    void update(float dt) override;

    void playSound(const std::string &resRef, float gain, bool positional, bool loop, float pitch = 1.0f);

    /** Change the gain of the sound playing now (and of later plays). */
    void setGain(float gain);

    /** Change the playback rate of the sound playing now. */
    void setPitch(float pitch);

    /**
     * Detach the sound playing now and fade it to silence over the given
     * wall-clock seconds, then stop it. The fade runs in update() and does
     * not depend on audibility; a new sound may start meanwhile.
     */
    void fadeOut(float seconds);

    bool isSoundPlaying() const;
    /** Stops and releases the sound, and any fading one, if playing. */
    void stopSound();
    bool isFading() const { return static_cast<bool>(_fading); }

    int priority() const { return _priority; }
    float maxDistance() const { return _maxDistance; }
    bool auidible() const { return _audible; }

    void setPriority(int priority) { _priority = priority; }
    void setMaxDistance(float distance) { _maxDistance = distance; }
    void setAudible(bool audible);

private:
    int _priority {0};
    float _maxDistance {0.0f};
    bool _audible {false};

    float _playGain {1.0f};
    float _gain {1.0f};

    std::shared_ptr<audio::AudioSource> _source;

    std::shared_ptr<audio::AudioSource> _fading;
    float _fadeTotal {0.0f};
    float _fadeLeft {0.0f};
    float _fadeFrom {1.0f};
};

} // namespace scene

} // namespace reone
