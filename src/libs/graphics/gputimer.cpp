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

#include "reone/graphics/gputimer.h"

#include <iomanip>
#include <map>
#include <sstream>
#include <vector>

namespace reone {

namespace graphics {

namespace gputimer {

namespace {

constexpr int kLatency = 4; // frames between issuing a query and reading it

struct Mark {
    std::string name;
    GLuint begin {0};
    GLuint end {0};
};

struct State {
    bool enabled {false};
    std::vector<GLuint> pool;
    std::vector<Mark> open;               // begun, not yet ended this frame
    std::vector<std::vector<Mark>> frames; // ring of closed frames
    int frameIndex {0};
    std::map<std::string, double> totals; // ms over the counted frames
    int counted {0};
};

State &state() {
    static State s;
    return s;
}

GLuint acquire() {
    auto &s = state();
    if (s.pool.empty()) {
        GLuint ids[16];
        glGenQueries(16, ids);
        s.pool.insert(s.pool.end(), ids, ids + 16);
    }
    GLuint id = s.pool.back();
    s.pool.pop_back();
    return id;
}

void collect(std::vector<Mark> &marks) {
    auto &s = state();
    if (marks.empty()) {
        return;
    }
    std::map<std::string, double> frame;
    for (auto &mark : marks) {
        GLuint64 t0 = 0;
        GLuint64 t1 = 0;
        glGetQueryObjectui64v(mark.begin, GL_QUERY_RESULT, &t0);
        glGetQueryObjectui64v(mark.end, GL_QUERY_RESULT, &t1);
        frame[mark.name] += (t1 > t0 ? static_cast<double>(t1 - t0) : 0.0) * 1e-6;
        s.pool.push_back(mark.begin);
        s.pool.push_back(mark.end);
    }
    marks.clear();
    for (auto &[name, ms] : frame) {
        s.totals[name] += ms;
    }
    ++s.counted;
}

} // namespace

void setEnabled(bool enabled) {
    auto &s = state();
    s.enabled = enabled;
}

bool enabled() {
    return state().enabled;
}

void begin(const char *name) {
    auto &s = state();
    if (!s.enabled) {
        return;
    }
    Mark mark;
    mark.name = name;
    mark.begin = acquire();
    glQueryCounter(mark.begin, GL_TIMESTAMP);
    s.open.push_back(std::move(mark));
}

void end(const char *name) {
    auto &s = state();
    if (!s.enabled) {
        return;
    }
    for (auto it = s.open.rbegin(); it != s.open.rend(); ++it) {
        if (it->name == name && it->end == 0) {
            it->end = acquire();
            glQueryCounter(it->end, GL_TIMESTAMP);
            return;
        }
    }
}

void endFrame() {
    auto &s = state();
    if (s.frames.empty()) {
        s.frames.resize(kLatency);
    }
    // Marks that never ended are dropped.
    std::vector<Mark> closed;
    for (auto &mark : s.open) {
        if (mark.end != 0) {
            closed.push_back(std::move(mark));
        } else {
            s.pool.push_back(mark.begin);
        }
    }
    s.open.clear();
    auto &slot = s.frames[s.frameIndex];
    collect(slot); // issued kLatency frames ago
    slot = std::move(closed);
    s.frameIndex = (s.frameIndex + 1) % kLatency;
}

void reset() {
    auto &s = state();
    s.totals.clear();
    s.counted = 0;
}

std::string json() {
    auto &s = state();
    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    out << "{\"enabled\":" << (s.enabled ? 1 : 0) << ",\"frames\":" << s.counted;
    for (auto &[name, ms] : s.totals) {
        out << ",\"" << name << "\":" << (s.counted > 0 ? ms / s.counted : 0.0);
    }
    out << "}";
    return out.str();
}

} // namespace gputimer

} // namespace graphics

} // namespace reone
