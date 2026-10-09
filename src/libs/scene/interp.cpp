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

#include "reone/scene/interp.h"

#include "reone/scene/node.h"

#include <chrono>

namespace reone {

namespace scene {

namespace interp {

struct Access {
    static uint64_t &born(SceneNode &n) { return n._interpBorn; }
    static uint64_t &serial(SceneNode &n) { return n._interpSerial; }
    static size_t &index(SceneNode &n) { return n._interpIndex; }
    static uint64_t &snapTree(SceneNode &n) { return n._interpSnapTree; }
    static uint64_t &snapBelow(SceneNode &n) { return n._interpSnapBelow; }
    static bool &applied(SceneNode &n) { return n._interpApplied; }
    static glm::mat4 &local(SceneNode &n) { return n._localTransform; }
    static void recompute(SceneNode &n) { n.computeAbsoluteTransforms(); }
};

namespace {

struct Entry {
    SceneNode *node {nullptr}; /**< null once the node is gone or a presentation write took it */
    glm::mat4 previous {1.0f};
    glm::mat4 current {1.0f};
    bool snap {false};   /**< born this tick, or snapped explicitly while in the tick */
    bool active {false}; /**< holds a blended transform now */
};

struct State {
    uint64_t serial {0};
    bool inTick {false};
    bool applied {false};
    bool blending {false};
    std::vector<Entry> entries;
    std::vector<SceneNode *> roots;
    Stats stats;
};

State &state() {
    static State s;
    return s;
}

bool interpolates(SceneNodeType type) {
    // What is drawn from its node transform. Particles are simulated and drawn
    // in world space (the emitter blends them), cameras are placed per frame,
    // and walkmeshes, triggers, grass and sounds are not drawn as models.
    switch (type) {
    case SceneNodeType::Dummy:
    case SceneNodeType::Model:
    case SceneNodeType::Mesh:
    case SceneNodeType::Light:
    case SceneNodeType::Emitter:
        return true;
    default:
        return false;
    }
}

void drop(State &s, SceneNode &node) {
    auto &serial = Access::serial(node);
    if (serial != s.serial || s.serial == 0) {
        return;
    }
    auto idx = Access::index(node);
    if (idx < s.entries.size() && s.entries[idx].node == &node) {
        s.entries[idx].node = nullptr;
    }
    serial = 0;
    Access::applied(node) = false;
}

struct TRS {
    glm::vec3 translation;
    glm::quat rotation;
    glm::vec3 scale;
};

TRS decompose(const glm::mat4 &m) {
    TRS out;
    out.translation = glm::vec3(m[3]);
    glm::vec3 c0(m[0]), c1(m[1]), c2(m[2]);
    out.scale = glm::vec3(glm::length(c0), glm::length(c1), glm::length(c2));
    if (glm::dot(glm::cross(c0, c1), c2) < 0.0f) {
        // Mirrored: fold the flip into one axis so the rest is a rotation.
        out.scale.x = -out.scale.x;
    }
    glm::mat3 r(
        out.scale.x != 0.0f ? c0 / out.scale.x : glm::vec3(1.0f, 0.0f, 0.0f),
        out.scale.y != 0.0f ? c1 / out.scale.y : glm::vec3(0.0f, 1.0f, 0.0f),
        out.scale.z != 0.0f ? c2 / out.scale.z : glm::vec3(0.0f, 0.0f, 1.0f));
    out.rotation = glm::normalize(glm::quat_cast(r));
    return out;
}

bool snappedByAncestor(const State &s, SceneNode &node) {
    for (SceneNode *p = node.parent(); p; p = p->parent()) {
        if (Access::snapTree(*p) == s.serial || Access::snapBelow(*p) == s.serial) {
            return true;
        }
    }
    return false;
}

bool hasActiveAncestor(SceneNode &node) {
    for (SceneNode *p = node.parent(); p; p = p->parent()) {
        if (Access::applied(*p)) {
            return true;
        }
    }
    return false;
}

} // namespace

glm::mat4 blend(const glm::mat4 &a, const glm::mat4 &b, float alpha) {
    TRS ta = decompose(a);
    TRS tb = decompose(b);
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::mix(ta.translation, tb.translation, alpha));
    m *= glm::mat4_cast(glm::slerp(ta.rotation, tb.rotation, alpha));
    m *= glm::scale(glm::mat4(1.0f), glm::mix(ta.scale, tb.scale, alpha));
    return m;
}

void beginTick() {
    auto &s = state();
    restore();
    for (auto &entry : s.entries) {
        if (entry.node) {
            Access::serial(*entry.node) = 0;
        }
    }
    s.entries.clear();
    ++s.serial;
    s.inTick = true;
}

void endTick() {
    auto &s = state();
    s.inTick = false;
    s.stats.registered = 0;
    for (const auto &entry : s.entries) {
        if (entry.node) {
            ++s.stats.registered;
        }
    }
}

bool inTick() {
    return state().inTick;
}

uint64_t tickSerial() {
    return state().serial;
}

void onWrite(SceneNode &node, const glm::mat4 &before) {
    auto &s = state();
    if (!s.inTick) {
        // Presentation code placed the node itself this frame: it is drawn
        // where that code put it.
        drop(s, node);
        return;
    }
    if (!interpolates(node.type())) {
        return;
    }
    auto &serial = Access::serial(node);
    if (serial == s.serial) {
        return; // the first write of the tick holds the previous value
    }
    serial = s.serial;
    Access::index(node) = s.entries.size();
    Entry entry;
    entry.node = &node;
    entry.previous = before;
    entry.snap = Access::born(node) == s.serial;
    s.entries.push_back(entry);
}

void forget(SceneNode &node) {
    auto &s = state();
    auto serial = Access::serial(node);
    if (serial == 0 || serial != s.serial) {
        return;
    }
    auto idx = Access::index(node);
    if (idx < s.entries.size() && s.entries[idx].node == &node) {
        s.entries[idx].node = nullptr;
    }
    for (auto &root : s.roots) {
        if (root == &node) {
            root = nullptr;
        }
    }
}

static uint64_t snapTarget(const State &s) {
    // Between ticks a snap is for the tick about to run.
    return s.inTick ? s.serial : s.serial + 1;
}

void snapTree(SceneNode &node) {
    auto &s = state();
    Access::snapTree(node) = snapTarget(s);
    if (!s.inTick) {
        drop(s, node);
    }
}

void snapBelow(SceneNode &node) {
    auto &s = state();
    Access::snapBelow(node) = snapTarget(s);
}

void snapAll() {
    auto &s = state();
    restore();
    for (auto &entry : s.entries) {
        if (entry.node) {
            Access::serial(*entry.node) = 0;
        }
        entry.node = nullptr;
    }
    s.entries.clear();
}

using Clock = std::chrono::steady_clock;

static double microsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

void apply(float alpha) {
    auto &s = state();
    restore();
    auto start = Clock::now();
    s.stats.blended = 0;
    s.stats.snapped = 0;
    s.stats.roots = 0;
    if (!(alpha < 1.0f) || s.inTick) {
        return;
    }
    alpha = glm::clamp(alpha, 0.0f, 1.0f);
    float snap2 = kSnapDistance * kSnapDistance;
    for (auto &entry : s.entries) {
        entry.active = false;
        if (!entry.node) {
            continue;
        }
        SceneNode &node = *entry.node;
        const glm::mat4 &current = Access::local(node);
        if (current == entry.previous) {
            continue;
        }
        bool snap = entry.snap ||
                    Access::snapTree(node) == s.serial ||
                    glm::distance2(glm::vec3(current[3]), glm::vec3(entry.previous[3])) > snap2 ||
                    snappedByAncestor(s, node);
        if (snap) {
            ++s.stats.snapped;
            continue;
        }
        entry.current = current;
        Access::local(node) = blend(entry.previous, entry.current, alpha);
        Access::applied(node) = true;
        entry.active = true;
        ++s.stats.blended;
    }
    if (s.stats.blended == 0) {
        return;
    }
    // Recompute each blended subtree once, from its topmost blended node.
    s.roots.clear();
    for (auto &entry : s.entries) {
        if (entry.active && entry.node && !hasActiveAncestor(*entry.node)) {
            s.roots.push_back(entry.node);
        }
    }
    // Set first: nodes that cache their simulated transform
    // (onAbsoluteTransformChanged) tell the two apart by it.
    s.applied = true;
    s.blending = true;
    for (auto *root : s.roots) {
        Access::recompute(*root);
    }
    s.blending = false;
    s.stats.roots = static_cast<int>(s.roots.size());
    ++s.stats.frames;
    s.stats.microseconds += microsSince(start);
}

void restore() {
    auto &s = state();
    if (!s.applied) {
        return;
    }
    auto start = Clock::now();
    s.applied = false;
    for (auto &entry : s.entries) {
        if (entry.active && entry.node && Access::applied(*entry.node)) {
            Access::local(*entry.node) = entry.current;
        }
    }
    s.roots.clear();
    for (auto &entry : s.entries) {
        if (entry.active && entry.node && Access::applied(*entry.node) && !hasActiveAncestor(*entry.node)) {
            s.roots.push_back(entry.node);
        }
    }
    for (auto &entry : s.entries) {
        if (entry.active && entry.node) {
            Access::applied(*entry.node) = false;
        }
        entry.active = false;
    }
    for (auto *root : s.roots) {
        if (root) {
            Access::recompute(*root);
        }
    }
    s.roots.clear();
    s.stats.microseconds += microsSince(start);
}

bool applied() {
    return state().applied;
}

bool blending() {
    return state().blending;
}

const Stats &stats() {
    return state().stats;
}

void resetForTest() {
    auto &s = state();
    restore();
    for (auto &entry : s.entries) {
        if (entry.node) {
            Access::serial(*entry.node) = 0;
        }
    }
    s = State();
}

} // namespace interp

} // namespace scene

} // namespace reone
