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

#include "reone/scene/objectlights.h"

#include <algorithm>

namespace reone {

namespace scene {

// Radius below which an object light falls off as if its radius were this.
static constexpr float kMinObjectLightRadius = 0.1f;

// 1 / (1 + k (d / r)^2): 1 at the light, about 0.18 at its radius.
static constexpr float kObjectLightFalloff = 4.5f;

static int dynamicTypeRank(int dynamicType) {
    switch (dynamicType) {
    case 1:
        return 0;
    case 2:
        return 1;
    default:
        return 2;
    }
}

bool isDynamicObjectLight(const ObjectLightSource &light) {
    if (light.radius <= 0.0f) {
        return false;
    }
    if (light.dynamicType == 0 && !light.affectDynamic) {
        return false;
    }
    return light.color != glm::vec3(0.0f);
}

std::vector<int> selectObjectLights(const std::vector<ObjectLightSource> &lights,
                                    const glm::vec3 &objectCenter,
                                    int maxLights,
                                    bool requireReach) {
    struct Candidate {
        int index;
        float relDistance;
    };
    std::vector<Candidate> candidates;
    for (size_t i = 0; i < lights.size(); ++i) {
        const auto &light = lights[i];
        if (!isDynamicObjectLight(light)) {
            continue;
        }
        float relDistance = glm::distance(light.position, objectCenter) / light.radius;
        if (requireReach && relDistance > 1.0f) {
            continue;
        }
        candidates.push_back(Candidate {static_cast<int>(i), relDistance});
    }
    std::stable_sort(candidates.begin(), candidates.end(), [&lights](const auto &a, const auto &b) {
        const auto &la = lights[a.index];
        const auto &lb = lights[b.index];
        if (la.priority != lb.priority) {
            return la.priority < lb.priority;
        }
        int ra = dynamicTypeRank(la.dynamicType);
        int rb = dynamicTypeRank(lb.dynamicType);
        if (ra != rb) {
            return ra < rb;
        }
        return a.relDistance < b.relDistance;
    });
    std::vector<int> selected;
    for (const auto &candidate : candidates) {
        if (static_cast<int>(selected.size()) >= maxLights) {
            break;
        }
        selected.push_back(candidate.index);
    }
    return selected;
}

void updateObjectLightSlots(std::vector<ObjectLightSlot> &slots,
                            const std::vector<const void *> &wanted,
                            const std::function<bool(const void *)> &isFading,
                            float dt,
                            int maxLights) {
    auto isWanted = [&wanted](const void *key) {
        return std::find(wanted.begin(), wanted.end(), key) != wanted.end();
    };
    float step = dt * kObjectLightFadeRate;
    for (auto it = slots.begin(); it != slots.end();) {
        bool fading = isFading(it->key);
        if (isWanted(it->key)) {
            it->weight = fading ? glm::min(1.0f, it->weight + step) : 1.0f;
            ++it;
            continue;
        }
        if (fading) {
            it->weight = glm::max(0.0f, it->weight - step);
            if (it->weight > 0.0f) {
                ++it;
                continue;
            }
        }
        it = slots.erase(it);
    }
    for (const void *key : wanted) {
        if (static_cast<int>(slots.size()) >= maxLights) {
            break;
        }
        bool present = std::any_of(slots.begin(), slots.end(), [key](const auto &slot) { return slot.key == key; });
        if (!present) {
            slots.push_back(ObjectLightSlot {key, 0.0f});
        }
    }
}

float objectLightAttenuation(float radius, float distance) {
    float r = glm::max(kMinObjectLightRadius, radius);
    float rel = distance / r;
    return 1.0f / (1.0f + kObjectLightFalloff * rel * rel);
}

} // namespace scene

} // namespace reone
