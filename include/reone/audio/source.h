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

namespace reone {

namespace audio {

class AudioClip;

class AudioSource : boost::noncopyable {
public:
    AudioSource(std::shared_ptr<AudioClip> clip,
                float gain = 1.0f,
                bool loop = false,
                std::optional<glm::vec3> position = std::nullopt) :
        _stream(std::move(clip)),
        _gain(gain),
        _loop(loop),
        _position(std::move(position)) {
    }

    ~AudioSource() { deinit(); }

    void init();
    void render();

    void play();
    void stop();
    /** Pauses a playing source; resume() continues it. */
    void pause();
    /**
     * Continues a paused source, or starts one that was created paused. A
     * source that was stopped, cancelled or has ended is left as it is.
     */
    void resume();
    /** Counts the source as playing without starting it; resume() starts it. */
    void startPaused();

    void setPosition(glm::vec3 position);

    /**
     * Scale the gain this source was created with (which already includes
     * the mixer's per-type volume). 1.0 restores it. Safe without a device.
     */
    void setGainFactor(float factor);

    /** Replace the created gain (the mixer re-applies per-type volume). */
    void setGain(float gain);
    float gain() const { return _gain; }

    /** Playback-rate multiplier; 1.0 is the recorded rate. */
    void setPitch(float pitch);

    float gainFactor() const { return _gainFactor; }
    float pitch() const { return _pitch; }

    bool isPlaying() const { return _playing; }

    float duration() const;

private:
    std::shared_ptr<AudioClip> _stream;
    float _gain;
    bool _loop;
    std::optional<glm::vec3> _position;
    float _gainFactor {1.0f};
    float _pitch {1.0f};

    bool _inited {false};
    bool _playing {false};

    std::vector<uint32_t> _buffers;
    bool _streaming {false};
    uint32_t _source {0};
    int _nextFrame {0};
    int _nextBuffer {0};

    bool _playingDirty {false};
    bool _positionDirty {false};

    void deinit();
};

} // namespace audio

} // namespace reone
