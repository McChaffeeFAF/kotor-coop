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

#include "reone/graphics/aabb.h"
#include "reone/graphics/walkmesh.h"

namespace reone {

namespace game {

/// Walkable face that is used for pathfinding.
struct Uniface {
    /// Indices into Uniwalk::vertices array.
    uint32_t vertices[3];

    /// Indices of adjecent faces. Edges [0, 1], [1, 2], [2, 0] are
    // adjecent when they are common with any other face.
    uint32_t adjecent[3];

    /// Center of a face. Used to estimate distance between two faces.
    glm::vec3 centroid;
};

/// Subdivision of a walkmesh. Uniroom associates a range of faces [begin, end)
/// to AABB.
struct Uniroom {
    uint32_t begin;
    uint32_t end;
    glm::vec3 min;
    glm::vec3 max;
};

/// Unified walkmesh, assembled from walkmeshes of all rooms in the area.
struct Uniwalk {
    std::vector<glm::vec3> vertices;
    std::vector<Uniface> faces;
    std::vector<Uniroom> rooms;
};

/// State of a face for A* algorithm.
struct AStarFace {
    // Fixed width: the search state is initialised with memset(0xFF), so the
    // all-ones pattern must be a valid value ("not visited yet").
    enum Flag : uint32_t {
        Unvisited = UINT32_MAX,
        Open = 1,
        Closed = 2,
    };

    Flag flag;
    uint32_t parent;
};

/// Element of a list of faces to consider next for A* algorithm.
struct AStarOpenFace {
    uint32_t index;
    float cost;
};

struct AStarContext {
    std::vector<AStarFace> state;
    std::vector<AStarOpenFace> open;
};

/// Fully calculated path.
struct AStarPath {
    std::vector<uint32_t> faces;
    glm::vec3 from;
    glm::vec3 to;
    uint32_t next;
    glm::vec3 nextPoint;

    int32_t index;
    bool active;
};

/// Handle to a calculated path.
struct Path {
    int32_t index;
};

/// A closed door that planning must not route through, as a segment across
/// the doorway on the ground plane.
struct PathBarrier {
    uint32_t objectId;
    glm::vec2 a;
    glm::vec2 b;
};

struct Pathfinder : public boost::noncopyable {
    Uniwalk uni;
    AStarContext astar;
    std::vector<AStarPath> paths;

    /// Doorways that are currently impassable. A path never crosses one,
    /// unless the barrier is at the path's destination (walking to the door).
    std::vector<PathBarrier> barriers;
    /// Changes whenever the barrier set changes, so paths planned against an
    /// older set can be replanned.
    uint32_t barrierRevision {0};
};

/// Replace the barrier set. Bumps the revision only when it changed.
void setPathBarriers(Pathfinder &pf, std::vector<PathBarrier> barriers);

void uniwalkLoadRoom(struct Uniwalk &wm, graphics::Walkmesh &data, std::set<uint32_t> &walkableMaterial);
void uniwalkFinalize(struct Uniwalk &uni);

std::optional<Path> createPath(Pathfinder &pf, const glm::vec3 &from, const glm::vec3 &to);
bool updatePath(Pathfinder &pf, Path p, const glm::vec3 &current);
void releasePath(Pathfinder &pf, Path path);

glm::vec3 getNextPathPoint(Pathfinder &pf, Path path);
glm::vec3 getLastPathPoint(Pathfinder &pf, Path path);
/// Rough remaining length of a path on the ground plane: from \p current
/// through the centres of the faces still ahead to the destination.
float getRemainingPathLength(Pathfinder &pf, Path path, const glm::vec3 &current);

glm::vec3 computeKeepoutForce(const Uniwalk &uni, const glm::vec3 &position);

/// True when \p position lies over a walkable face of the unified walkmesh.
bool isOnWalkmesh(const Uniwalk &uni, const glm::vec3 &position);

/// Height of the walkable face under (x, y) that lies nearest to \p nearZ,
/// searched over every room of the unified walkmesh (the scene graph only
/// holds the rooms near the camera). Empty when no face lies under (x, y).
std::optional<float> walkmeshElevationAt(const Uniwalk &uni, const glm::vec2 &xy, float nearZ);

} // namespace game

} // namespace reone
