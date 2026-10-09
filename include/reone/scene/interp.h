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

namespace scene {

class SceneNode;

/**
 * Render interpolation between the last two simulation ticks
 * (design note render_interp).
 *
 * The simulation writes scene-node transforms inside Game::tick. The first
 * write a node receives in a tick records the transform it had before (the
 * previous tick's). At present time apply() puts every such node at the
 * blend of the two for the frame's tick alpha and restore() puts the
 * simulated transform back after the frame is drawn, so nothing outside
 * rendering ever sees a blended value.
 *
 * Presentation-only: nothing here reads or writes gameplay state or draws a
 * random number. One registry serves the main thread.
 */
namespace interp {

/** Every node farther than this from where it stood a tick ago snaps (a warp). */
constexpr float kSnapDistance = 3.0f;

struct Stats {
    int registered {0}; /**< nodes the last tick moved */
    int blended {0};    /**< nodes drawn between two ticks last apply() */
    int snapped {0};    /**< nodes drawn at their tick value by a discontinuity */
    int roots {0};      /**< subtrees recomputed last apply() */
    uint64_t frames {0};      /**< apply() calls that blended something */
    double microseconds {0.0}; /**< time spent in those apply() and their restore(), summed */
};

/** Opens a simulation tick: restores, then forgets the previous tick's moves. */
void beginTick();
void endTick();
bool inTick();
uint64_t tickSerial();

/** RAII helper for Game::tick. */
class TickScope {
public:
    TickScope() { beginTick(); }
    ~TickScope() { endTick(); }
};

/**
 * Called by SceneNode::setLocalTransform before the new value is stored.
 * Inside a tick it registers the node (first write only). Outside a tick it
 * is a presentation write, which takes the node out of this tick's blend.
 */
void onWrite(SceneNode &node, const glm::mat4 &before);

/** Called from the SceneNode destructor. */
void forget(SceneNode &node);

/** Draw the node and its whole subtree at the tick value this tick. */
void snapTree(SceneNode &node);

/** Draw every node below this one (not the node itself) at the tick value. */
void snapBelow(SceneNode &node);

/** Drop every blend of the current tick (a load, a clock reset). */
void snapAll();

/** Place nodes at the blend for alpha in [0, 1]. alpha >= 1 does nothing. */
void apply(float alpha);

/** Put the simulated transforms back. Idempotent. */
void restore();

bool applied();

/**
 * True only while apply() recomputes the blended subtrees. A node told its
 * absolute transform changed at any other time (a tick, restore(), or
 * presentation code placing it itself) holds a simulated transform.
 */
bool blending();

const Stats &stats();

/**
 * The presented transform alpha of the way from a to b: translation and
 * scale mixed, orientation slerped.
 */
glm::mat4 blend(const glm::mat4 &a, const glm::mat4 &b, float alpha);

/** Tests: forget everything and start from serial 0. */
void resetForTest();

} // namespace interp

} // namespace scene

} // namespace reone
