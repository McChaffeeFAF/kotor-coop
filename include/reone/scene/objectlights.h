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

#include "reone/graphics/types.h"

#include <functional>

namespace reone {

namespace scene {

/**
 * Per-object lighting of dynamic objects (creatures, placeables, doors and
 * what they carry). Rooms keep their baked lightmaps and are not lit this way.
 *
 * Each dynamic object is lit by at most kMaxObjectLights lights, picked from
 * every light in the scene:
 *
 * - a light qualifies when it is flagged to affect dynamic objects, either by
 *   its dynamic type (1 or 2) or by its affect-dynamic flag, has a positive
 *   radius and a non-black colour, and the object's centre lies within its
 *   radius;
 * - qualifying lights are ordered by priority (1 first, 5 last), then by
 *   dynamic type (1, then 2, then the rest), then by distance relative to the
 *   radius (nearest first); the first kMaxObjectLights are used.
 *
 * The chosen lights fall off as 1 / (1 + 4.5 (d / r)^2).
 */
struct ObjectLightSource {
    glm::vec3 position {0.0f};
    glm::vec3 color {0.0f};
    float multiplier {1.0f};
    float radius {0.0f};
    int priority {0};
    int dynamicType {0};
    bool affectDynamic {false};
    bool ambientOnly {false};
};

/** Whether the light may light dynamic objects at all. */
bool isDynamicObjectLight(const ObjectLightSource &light);

/**
 * Indices into lights of the lights that light an object centred at
 * objectCenter, best first, at most maxLights.
 *
 * requireReach false drops the "centre within the radius" test: a GUI stage
 * (character generation, the main menu) is a small authored scene whose few
 * lights are all meant for the model on it, so each light is used whatever
 * the model's centre, with the same fall-off and no cut-off.
 */
std::vector<int> selectObjectLights(const std::vector<ObjectLightSource> &lights,
                                    const glm::vec3 &objectCenter,
                                    int maxLights = graphics::kMaxObjectLights,
                                    bool requireReach = true);

/**
 * One light in an object's light set, with its weight (0..1). The key names
 * the light and is only compared, never followed.
 */
struct ObjectLightSlot {
    const void *key {nullptr};
    float weight {0.0f};
};

/** Weight change per second of a light flagged fading (K2: 3.0/s). */
constexpr float kObjectLightFadeRate = 3.0f;

/**
 * K2 behaviour: advance an object's light set by dt towards the wanted
 * lights (best first).
 *
 * - A light in the set that is still wanted: a fading one gains
 *   dt * kObjectLightFadeRate (capped at 1); another goes to 1.
 * - A light in the set that is no longer wanted: a fading one loses
 *   dt * kObjectLightFadeRate and leaves the set at 0, keeping its place
 *   until then; another leaves at once.
 * - Then newly wanted lights join at weight 0, while the set is smaller
 *   than maxLights.
 */
void updateObjectLightSlots(std::vector<ObjectLightSlot> &slots,
                            const std::vector<const void *> &wanted,
                            const std::function<bool(const void *)> &isFading,
                            float dt,
                            int maxLights = graphics::kMaxObjectLights);

/** Fall-off of an object light of the given radius at distance. */
float objectLightAttenuation(float radius, float distance);

} // namespace scene

} // namespace reone
