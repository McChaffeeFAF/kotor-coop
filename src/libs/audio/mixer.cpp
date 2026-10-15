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

#include "reone/audio/clip.h"
#include "reone/audio/mixer.h"

#include <algorithm>

namespace reone {

namespace audio {

void AudioMixer::render() {
    // Volume sliders apply to sounds already playing, not only new ones.
    int volumes[4] {_options.musicVolume, _options.voiceVolume, _options.soundVolume, _options.movieVolume};
    if (!std::equal(std::begin(volumes), std::end(volumes), std::begin(_appliedVolumes)) ||
        _appliedMuted != _options.muted) {
        std::copy(std::begin(volumes), std::end(volumes), std::begin(_appliedVolumes));
        _appliedMuted = _options.muted;
        for (auto &active : _sources) {
            active.source->setGain(gainByType(active.type, active.gain));
        }
    }
    for (auto it = _sources.begin(); it != _sources.end();) {
        auto &source = it->source;
        source->render();
        if (!source->isPlaying()) {
            it = _sources.erase(it);
        } else {
            ++it;
        }
    }
}

void AudioMixer::stop(AudioType type) {
    for (auto it = _sources.begin(); it != _sources.end();) {
        if (it->type != type) {
            ++it;
            continue;
        }

        it->source->stop();
        it = _sources.erase(it);
    }
}

void AudioMixer::stopAll() {
    for (auto &source : _sources) {
        source.source->stop();
    }
    _sources.clear();
}

void AudioMixer::setSoundsHeldForMovie(bool held) {
    if (_soundsHeldForMovie == held) {
        return;
    }
    _soundsHeldForMovie = held;
    for (auto &active : _sources) {
        if (active.type == AudioType::Movie) {
            continue;
        }
        if (held) {
            active.source->pause();
        } else {
            active.source->resume();
        }
    }
}

std::shared_ptr<AudioSource> AudioMixer::play(std::shared_ptr<AudioClip> clip,
                                              AudioType type,
                                              float gain,
                                              bool loop,
                                              std::optional<glm::vec3> position) {
    if (!clip || clip->getFrameCount() == 0) {
        return nullptr;
    }

    auto source = std::make_shared<AudioSource>(
        std::move(clip),
        gainByType(type, gain),
        loop,
        std::move(position));
    source->init();
    if (_soundsHeldForMovie && type != AudioType::Movie) {
        source->startPaused(); // begins when the movie's hold ends
    } else {
        source->play();
    }
    _sources.push_back(ActiveSource {source, type, gain});
    return source;
}

float AudioMixer::gainByType(AudioType type, float gain) const {
    if (_options.muted) {
        return 0.0f;
    }
    int volume;
    switch (type) {
    case AudioType::Music:
        volume = _options.musicVolume;
        break;
    case AudioType::Voice:
        volume = _options.voiceVolume;
        break;
    case AudioType::Sound:
        volume = _options.soundVolume;
        break;
    case AudioType::Movie:
        volume = _options.movieVolume;
        break;
    default:
        volume = 85.0f;
        break;
    }
    return gain * (volume / 100.0f);
}

} // namespace audio

} // namespace reone
