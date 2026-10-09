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

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

namespace reone {

/**
 * Frame-time instrumentation (design note p2_timing): per-frame phase times,
 * rolling frame-time statistics and a hitch log.
 *
 * Only the thread that calls beginFrame (the main thread) records; scopes and
 * counters on other threads are ignored, so worker-thread loads never charge
 * the frame. Phases may nest: Resource, Script and Shader time is inclusive in
 * whichever outer phase was running.
 */
namespace frameperf {

enum class Phase {
    Input,
    Sim,      // fixed ticks: Game::tick
    Present,  // per-frame presentation in Game::update
    Render,   // scene and GUI draw calls
    Swap,     // buffer swap: GPU wait and vsync
    Audio,
    Resource, // resource lookups that reached an archive or folder
    Script,   // script virtual machine runs
    Shader,   // shader program compiles and links
    // Parts of Sim, for the hitch breakdown.
    SimModule,    // module and area update: objects, actions, perception, triggers
    SimCombat,    // combat rounds
    SimEvents,    // due script events
    SimAnimation, // model animation and skeletons
    SimSave,      // a pending save written at the tick's stable point
    Count
};

constexpr int kPhaseCount = static_cast<int>(Phase::Count);

/** Frames longer than this are hitches: logged with their breakdown. */
constexpr float kHitchMilliseconds = 50.0f;

/** A named resource load and how long it took, for the hitch breakdown. */
struct NamedLoad {
    std::string name;
    float milliseconds {0.0f};
};

constexpr int kSlowestLoads = 3;

struct FrameRecord {
    uint64_t frame {0};
    float milliseconds {0.0f};
    int ticks {0};
    int resourceLoads {0};
    int scriptRuns {0};
    int shaderCompiles {0};
    bool load {false}; // a synchronous module load or a movie start
    std::array<float, kPhaseCount> phases {};
    std::array<NamedLoad, kSlowestLoads> slowestLoads {}; // outermost named loads, slowest first
};

const char *phaseName(Phase phase);

/** Starts a frame on the calling thread, which becomes the recording thread. */
void beginFrame(uint64_t frame);

/** Ends the frame; returns its record. Frames over the hitch threshold are logged. */
const FrameRecord &endFrame();

/** Marks the frame just ended as a load frame: kept out of the statistics. */
void markLastFrameLoad();

void addPhaseTime(Phase phase, float milliseconds);
void addTicks(int ticks);
void countResourceLoad();
void countScriptRun();
void countShaderCompile();
void noteLoad(const std::string &name, float milliseconds);

bool isRecordingThread();

/** Enters a phase on the recording thread; true when it is the outermost entry. */
bool enterPhase(Phase phase);
void leavePhase(Phase phase);

/**
 * RAII phase timer. Only the outermost scope of a phase is timed, so a model
 * load that loads its textures is counted once.
 */
class Scope {
public:
    explicit Scope(Phase phase, const std::string *name = nullptr) :
        _phase(phase),
        _name(name),
        _recording(isRecordingThread()) {
        if (_recording) {
            _outermost = enterPhase(phase);
            if (_outermost) {
                _start = std::chrono::steady_clock::now();
            }
        }
    }

    ~Scope() {
        if (!_recording) {
            return;
        }
        if (_outermost) {
            auto elapsed = std::chrono::steady_clock::now() - _start;
            float ms = std::chrono::duration<float, std::milli>(elapsed).count();
            addPhaseTime(_phase, ms);
            if (_name) {
                noteLoad(*_name, ms);
            }
        }
        leavePhase(_phase);
    }

    /** A resource scope that names what it loads. */
    Scope(Phase phase, const std::string &name) :
        Scope(phase, &name) {
    }

    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

private:
    Phase _phase;
    const std::string *_name;
    bool _recording;
    bool _outermost {false};
    std::chrono::steady_clock::time_point _start;
};

struct Stats {
    int frames {0};
    int loadFrames {0};
    float minMs {0.0f};
    float avgMs {0.0f};
    float p95Ms {0.0f};
    float p99Ms {0.0f};
    float maxMs {0.0f};
    int over20 {0};
    int over33 {0};
    int over50 {0};
    int hitches {0};
};

/** Statistics over the frames since the last window reset (load frames excluded). */
Stats windowStats();

/** Statistics over the whole session (load frames excluded). */
Stats sessionStats();

/** Starts a new statistics window. */
void resetWindow();

/**
 * One line of JSON with the window and session statistics and the hitches
 * recorded since the last call with consume = true (at most 32).
 */
std::string json(bool consumeHitches);

/** Forgets everything (tests). */
void reset();

/** Percentile of a sorted sample, nearest rank; exposed for tests. */
float percentile(const float *sorted, int count, float p);

} // namespace frameperf

} // namespace reone
