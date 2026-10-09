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

#include "reone/game/shapequery.h"

#include <algorithm>

#include <glm/geometric.hpp>
#include <glm/gtx/norm.hpp>

namespace reone {

namespace game {

namespace {

constexpr int kShapeSpellCylinder = 0;
constexpr int kShapeCone = 1;
constexpr int kShapeCube = 2;
constexpr int kShapeSpellCone = 3;

constexpr int kObjectTypeAll = 32767;

glm::vec3 safeNormalize(const glm::vec3 &v) {
    float length2 = glm::length2(v);
    return length2 > 0.0f ? v / std::sqrt(length2) : glm::vec3(0.0f);
}

// Squared distance from point to the infinite line through a and b.
float squaredDistanceToLine(const glm::vec3 &point, const glm::vec3 &a, const glm::vec3 &b) {
    glm::vec3 axis = b - a;
    float axisLength2 = glm::length2(axis);
    if (axisLength2 <= 0.0f) {
        return glm::length2(point - a);
    }
    float t = glm::dot(point - a, axis) / axisLength2;
    return glm::length2(point - (a + t * axis));
}

} // namespace

void prepareShapeQuery(ShapeQuery &query) {
    query.end = query.target;
    if (query.shape == kShapeSpellCylinder) {
        // The cylinder runs fSize metres from the caster towards the target.
        glm::vec3 dir = query.target - query.origin;
        if (glm::length2(dir) <= 1e-4f) {
            dir = glm::vec3(0.0f, 1.0f, 0.0f);
        } else {
            dir = glm::normalize(dir);
        }
        query.end = query.origin + dir * query.size;
        glm::vec3 side(-dir.y * kSpellCylinderRadius, dir.x * kSpellCylinderRadius, 0.0f);
        float xs[] = {
            (query.origin + side).x, (query.origin - side).x,
            (query.end + side).x, (query.end - side).x};
        query.minX = *std::min_element(std::begin(xs), std::end(xs));
        query.maxX = *std::max_element(std::begin(xs), std::end(xs));
        return;
    }
    float low = query.target.x;
    float high = query.target.x;
    if (query.shape == kShapeCone || query.shape == kShapeSpellCone) {
        low = std::min(query.origin.x, query.target.x);
        high = std::max(query.origin.x, query.target.x);
    }
    query.minX = low - query.size;
    query.maxX = high + query.size;
}

bool shapeContains(const ShapeQuery &query, const glm::vec3 &point) {
    float size2 = query.size * query.size;
    switch (query.shape) {
    case kShapeSpellCylinder: {
        glm::vec3 fromOrigin = point - query.origin;
        if (glm::length2(fromOrigin) > size2) {
            return false;
        }
        glm::vec3 axis = safeNormalize(query.end - query.origin);
        if (glm::dot(safeNormalize(fromOrigin), axis) < 0.0f) {
            return false;
        }
        return squaredDistanceToLine(point, query.origin, query.end) <=
               kSpellCylinderRadius * kSpellCylinderRadius;
    }
    case kShapeCone: {
        // Radius grows linearly from 0 at the origin to fSize at the target.
        glm::vec3 axis = query.target - query.origin;
        float axisLength = glm::length(axis);
        if (axisLength <= 0.0f) {
            return false;
        }
        glm::vec3 fromOrigin = point - query.origin;
        if (glm::dot(safeNormalize(fromOrigin), axis / axisLength) <= 0.0f) {
            return false;
        }
        float along = glm::dot(fromOrigin, axis / axisLength);
        float radius = query.size * along / axisLength;
        return squaredDistanceToLine(point, query.origin, query.target) <= radius * radius;
    }
    case kShapeCube:
        return query.target.x - query.size <= point.x && point.x < query.target.x + query.size &&
               query.target.y - query.size <= point.y && point.y < query.target.y + query.size &&
               query.target.z - query.size <= point.z && point.z < query.target.z + query.size;
    case kShapeSpellCone: {
        glm::vec3 fromOrigin = point - query.origin;
        if (glm::length2(fromOrigin) > size2) {
            return false;
        }
        glm::vec3 axis = safeNormalize(query.target - query.origin);
        return glm::dot(safeNormalize(fromOrigin), axis) >= kSpellConeCosine;
    }
    default:
        // SHAPE_SPHERE, and any unknown shape.
        return glm::length2(point - query.target) <= size2;
    }
}

bool shapeFilterAccepts(int filter, int objectType) {
    if ((filter & kObjectTypeAll) == kObjectTypeAll) {
        return true;
    }
    // OBJECT_TYPE_* bits match the engine's object type values 1..512.
    return objectType > 0 && objectType <= 512 && (filter & objectType) != 0;
}

glm::vec3 shapeLineOfSightOrigin(const ShapeQuery &query) {
    switch (query.shape) {
    case kShapeSpellCylinder:
    case kShapeCone:
    case kShapeSpellCone:
        return query.origin;
    default:
        return query.target;
    }
}

} // namespace game

} // namespace reone
