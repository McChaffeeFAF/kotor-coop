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

#include "reone/scene/node/sound.h"

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/audioclips.h"

using namespace reone::audio;

namespace reone {

namespace scene {

void SoundSceneNode::update(float dt) {
    SceneNode::update(dt);
    if (!_fading) {
        return;
    }
    _fadeLeft -= dt;
    if (_fadeLeft <= 0.0f || !_fading->isPlaying()) {
        _fading->stop();
        _fading.reset();
        return;
    }
    _fading->setGainFactor(_fadeFrom * (_fadeLeft / _fadeTotal));
}

void SoundSceneNode::playSound(const std::string &resRef, float gain, bool positional, bool loop, float pitch) {
    auto clip = _resourceSvc.audioClips.get(resRef);
    std::optional<glm::vec3> position;
    if (positional) {
        position = origin();
    }
    _playGain = gain;
    _gain = gain;
    _source = _audioSvc.mixer.play(
        std::move(clip),
        AudioType::Sound,
        gain,
        loop,
        std::move(position));
    if (_source && pitch != 1.0f) {
        _source->setPitch(pitch);
    }
}

void SoundSceneNode::setGain(float gain) {
    _gain = gain;
    if (_source) {
        _source->setGainFactor(_playGain > 0.0f ? gain / _playGain : 0.0f);
    }
}

void SoundSceneNode::setPitch(float pitch) {
    if (_source) {
        _source->setPitch(pitch);
    }
}

void SoundSceneNode::fadeOut(float seconds) {
    if (!_source) {
        return;
    }
    if (_fading) {
        _fading->stop();
    }
    _fading = std::move(_source);
    _source.reset();
    if (seconds <= 0.0f) {
        _fading->stop();
        _fading.reset();
        return;
    }
    _fadeTotal = seconds;
    _fadeLeft = seconds;
    _fadeFrom = _fading->gainFactor();
}

bool SoundSceneNode::isSoundPlaying() const {
    return _source && _source->isPlaying();
}

void SoundSceneNode::setAudible(bool audible) {
    if (_audible == audible) {
        return;
    }
    if (!audible && _source) {
        _source->stop();
        _source.reset();
    }
    _audible = audible;
}

} // namespace scene

} // namespace reone
