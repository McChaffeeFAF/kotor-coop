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

namespace graphics {

template <class Value>
class KeyframeTrack {
public:
    struct Keyframe {
        float time {0.0f};
        Value value {Value()};
        Value incoming {Value()};
        Value outgoing {Value()};
        bool bezier {false};
    };

    void add(float time, Value value) {
        Keyframe kf;
        kf.time = time;
        kf.value = std::move(value);
        _keyframes.push_back(std::move(kf));
    }

    void update() {
        std::sort(_keyframes.begin(), _keyframes.end(), [](const auto &lhs, const auto &rhs) {
            return lhs.time < rhs.time;
        });
    }

    // MDL tangents are offsets from their key's value, not derivatives.
    void addBezier(float time, Value value, Value incoming, Value outgoing) {
        _keyframes.push_back({time, std::move(value), std::move(incoming), std::move(outgoing), true});
    }

    bool valueAtTime(float time, Value &value) const {
        if (_keyframes.empty()) {
            return false;
        }
        if (_keyframes.size() == 1ll || _keyframes[0].time >= time) {
            value = _keyframes[0].value;
            return true;
        }
        if (time >= _keyframes.back().time) {
            value = _keyframes.back().value;
            return true;
        }
        int rightKfIdx = 1; // the loop below always runs: there are at least two keyframes
        for (int i = 1; i < _keyframes.size(); ++i) {
            rightKfIdx = i;
            if (_keyframes[i].time >= time) {
                break;
            }
        }
        int leftKfIdx = rightKfIdx - 1;
        const auto &leftKeyframe = _keyframes[leftKfIdx];
        const auto &rightKeyframe = _keyframes[rightKfIdx];
        if (leftKeyframe.time == rightKeyframe.time) {
            value = leftKeyframe.value;
        } else {
            float factor = (time - leftKeyframe.time) / (rightKeyframe.time - leftKeyframe.time);
            value = interpolateKeyframes(leftKeyframe, rightKeyframe, factor);
        }
        return true;
    }

    const std::vector<Keyframe> &keyframes() const {
        return _keyframes;
    }

private:
    std::vector<Keyframe> _keyframes;

    template <typename T = Value,
              typename std::enable_if_t<!std::is_same<glm::quat, T>::value, int> = 0>
    Value interpolateKeyframes(const Keyframe &lhs, const Keyframe &rhs, float factor) const {
        if (lhs.time == rhs.time) {
            return lhs.value;
        }
        if (lhs.bezier && rhs.bezier) {
            const float inverse = 1.0f - factor;
            return inverse * inverse * inverse * lhs.value +
                   3.0f * inverse * inverse * factor * (lhs.value + lhs.outgoing) +
                   3.0f * inverse * factor * factor * (rhs.value + rhs.incoming) +
                   factor * factor * factor * rhs.value;
        }
        return glm::mix(lhs.value, rhs.value, factor);
    }

    // For quaternions we have to use slerp to always take a shorter path.
    template <typename T = Value,
              typename std::enable_if_t<std::is_same<glm::quat, T>::value, int> = 1>
    Value interpolateKeyframes(const Keyframe &lhs, const Keyframe &rhs, float factor) const {
        if (lhs.time == rhs.time) {
            return lhs.value;
        }
        return glm::slerp(lhs.value, rhs.value, factor);
    }
};

} // namespace graphics

} // namespace reone
