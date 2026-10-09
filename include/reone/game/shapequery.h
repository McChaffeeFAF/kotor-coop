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

#pragma once

#include <glm/vec3.hpp>

namespace reone {

namespace game {

/**
 * One Get{First,Next}ObjectInShape query (SHAPE_*). Behaviour notes:
 * design note c09_ai_combat, "Objects in a shape".
 */
struct ShapeQuery {
    int shape {4};
    float size {0.0f};
    glm::vec3 target {0.0f};
    glm::vec3 origin {0.0f};
    bool lineOfSight {false};
    int objectFilter {1};

    // Derived by prepareShapeQuery.
    glm::vec3 end {0.0f}; /**< far end of a spell cylinder */
    float minX {0.0f};
    float maxX {0.0f};
};

/** Half-width of SHAPE_SPELLCYLINDER, in metres. */
constexpr float kSpellCylinderRadius = 1.5f;
/** Cosine of SHAPE_SPELLCONE's half-angle (30 degrees). */
constexpr float kSpellConeCosine = 0.866f;

/** Fill the derived fields: the cylinder's far end and the x search range. */
void prepareShapeQuery(ShapeQuery &query);

/** Whether a point lies inside the query's shape. */
bool shapeContains(const ShapeQuery &query, const glm::vec3 &point);

/** OBJECT_TYPE_* mask test; OBJECT_TYPE_ALL (32767) accepts anything. */
bool shapeFilterAccepts(int filter, int objectType);

/** Where a line-of-sight check starts: the caster for the aimed shapes. */
glm::vec3 shapeLineOfSightOrigin(const ShapeQuery &query);

} // namespace game

} // namespace reone
