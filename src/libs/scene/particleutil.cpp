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

#include "reone/scene/particleutil.h"

namespace reone {

namespace scene {

namespace particleutil {

static constexpr float kMinVectorLength = 1e-6f;
static constexpr float kMotionBlurTrailTime = 0.25f;

MotionBlurBasis buildMotionBlurBasis(
    const glm::mat4 &emitterTransform,
    const glm::vec3 &localVelocity,
    const glm::vec3 &toCamera,
    const glm::vec3 &cameraRight,
    const glm::vec3 &cameraUp,
    float particleLength,
    float blurLength,
    float minTrailTime) {

    glm::vec3 worldVelocity(emitterTransform * glm::vec4(localVelocity, 0.0f));
    float speed = glm::length(worldVelocity);
    if (speed <= kMinVectorLength || particleLength <= kMinVectorLength) {
        return {cameraRight, cameraUp, 1.0f};
    }

    glm::vec3 up(worldVelocity / speed);
    glm::vec3 right(glm::cross(up, toCamera));
    float rightLength = glm::length(right);
    if (rightLength <= kMinVectorLength) {
        return {cameraRight, cameraUp, 1.0f};
    }
    right /= rightLength;

    // The trail never falls short of the distance covered since the last
    // frame: a short blur length (a blaster bolt's 0.01) still streaks.
    float trailTime = glm::max(kMotionBlurTrailTime * glm::max(blurLength, 0.0f), glm::max(minTrailTime, 0.0f));
    float trailLength = trailTime * speed;
    return {right, up, 1.0f + trailLength / particleLength};
}

} // namespace particleutil

} // namespace scene

} // namespace reone
