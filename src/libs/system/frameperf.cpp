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

#include "reone/system/frameperf.h"

#include "reone/system/logutil.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

namespace reone {

namespace frameperf {

namespace {

// Session percentiles come from a histogram: 0.1 ms bins up to 1 s.
constexpr int kBins = 10000;
constexpr float kBinMs = 0.1f;
constexpr size_t kMaxWindowFrames = 1 << 20;
constexpr size_t kMaxHitches = 32;

struct Accumulator {
    int frames {0};
    int loadFrames {0};
    double sumMs {0.0};
    float minMs {0.0f};
    float maxMs {0.0f};
    int over20 {0};
    int over33 {0};
    int over50 {0};

    void add(float ms) {
        if (frames == 0 || ms < minMs) {
            minMs = ms;
        }
        maxMs = std::max(maxMs, ms);
        ++frames;
        sumMs += ms;
        over20 += ms > 20.0f ? 1 : 0;
        over33 += ms > 33.3f ? 1 : 0;
        over50 += ms > kHitchMilliseconds ? 1 : 0;
    }
};

struct Hitch {
    FrameRecord record;
    std::string type;
};

std::mutex g_mutex;
std::thread::id g_thread;
std::atomic_bool g_threadSet {false};
std::chrono::steady_clock::time_point g_frameStart;
FrameRecord g_current;
FrameRecord g_last;
bool g_lastPending {false};
std::array<int, kPhaseCount> g_depth {};

Accumulator g_window;
std::vector<float> g_windowSamples;
Accumulator g_session;
std::vector<int> g_histogram(kBins + 1, 0);
std::deque<Hitch> g_hitches;
std::map<std::string, int> g_hitchTypes;

std::string classify(const FrameRecord &record) {
    if (record.load) {
        return "load";
    }
    auto at = [&record](Phase phase) { return record.phases[static_cast<int>(phase)]; };
    // Nested causes first: they explain the outer phase they ran in.
    Phase nested[] = {Phase::Resource, Phase::Shader, Phase::Script};
    Phase worstNested = Phase::Resource;
    for (Phase phase : nested) {
        if (at(phase) > at(worstNested)) {
            worstNested = phase;
        }
    }
    if (at(worstNested) >= 0.5f * kHitchMilliseconds) {
        return phaseName(worstNested);
    }
    Phase outer[] = {Phase::Input, Phase::Sim, Phase::Present, Phase::Render, Phase::Swap, Phase::Audio};
    Phase worst = Phase::Input;
    float accounted = 0.0f;
    for (Phase phase : outer) {
        accounted += at(phase);
        if (at(phase) > at(worst)) {
            worst = phase;
        }
    }
    if (record.milliseconds - accounted > at(worst)) {
        return "unaccounted";
    }
    std::string type = phaseName(worst);
    if (worst == Phase::Sim) {
        if (record.ticks > 1) {
            type += "-catchup";
        }
        Phase parts[] = {Phase::SimModule, Phase::SimCombat, Phase::SimEvents, Phase::SimAnimation, Phase::SimSave};
        Phase worstPart = Phase::SimModule;
        for (Phase part : parts) {
            if (at(part) > at(worstPart)) {
                worstPart = part;
            }
        }
        if (at(worstPart) >= 0.5f * at(Phase::Sim)) {
            type += std::string(":") + phaseName(worstPart);
        }
    }
    return type;
}

std::string recordJson(const FrameRecord &record) {
    std::ostringstream ss;
    ss.precision(2);
    ss << std::fixed;
    ss << "{\"frame\":" << record.frame
       << ",\"ms\":" << record.milliseconds
       << ",\"ticks\":" << record.ticks
       << ",\"resourceLoads\":" << record.resourceLoads
       << ",\"scriptRuns\":" << record.scriptRuns
       << ",\"shaderCompiles\":" << record.shaderCompiles
       << ",\"load\":" << (record.load ? "true" : "false")
       << ",\"slowest\":[";
    bool firstLoad = true;
    for (const NamedLoad &load : record.slowestLoads) {
        if (load.name.empty()) {
            continue;
        }
        ss << (firstLoad ? "" : ",") << "{\"name\":\"" << load.name << "\",\"ms\":" << load.milliseconds << "}";
        firstLoad = false;
    }
    ss << "],\"phases\":{";
    for (int i = 0; i < kPhaseCount; ++i) {
        if (i > 0) {
            ss << ",";
        }
        ss << "\"" << phaseName(static_cast<Phase>(i)) << "\":" << record.phases[i];
    }
    ss << "}}";
    return ss.str();
}

std::string profileLine(const FrameRecord &record) {
    std::ostringstream ss;
    ss.precision(1);
    ss << std::fixed;
    ss << record.milliseconds << " ms, " << record.ticks << " ticks";
    for (int i = 0; i < kPhaseCount; ++i) {
        ss << ", " << phaseName(static_cast<Phase>(i)) << " " << record.phases[i];
    }
    ss << "; " << record.resourceLoads << " resource loads, "
       << record.scriptRuns << " script runs, "
       << record.shaderCompiles << " shader compiles";
    for (const NamedLoad &load : record.slowestLoads) {
        if (!load.name.empty()) {
            ss << "; " << load.name << " " << load.milliseconds << " ms";
        }
    }
    return ss.str();
}

// Adds the previous frame to the statistics and the hitch log.
void finalizeLast() {
    if (!g_lastPending) {
        return;
    }
    g_lastPending = false;
    const FrameRecord &record = g_last;
    if (record.load) {
        ++g_window.loadFrames;
        ++g_session.loadFrames;
    } else {
        g_window.add(record.milliseconds);
        if (g_windowSamples.size() < kMaxWindowFrames) {
            g_windowSamples.push_back(record.milliseconds);
        }
        g_session.add(record.milliseconds);
        int bin = std::clamp(static_cast<int>(record.milliseconds / kBinMs), 0, kBins);
        ++g_histogram[bin];
    }
    if (record.milliseconds <= kHitchMilliseconds) {
        return;
    }
    std::string type = classify(record);
    int &seen = g_hitchTypes[type];
    ++seen;
    if (seen == 1) {
        info("Frame hitch (" + type + "), first of its kind: frame " + std::to_string(record.frame) + ": " + profileLine(record));
    } else {
        debug("Frame hitch (" + type + "): frame " + std::to_string(record.frame) + ": " + profileLine(record));
    }
    g_hitches.push_back(Hitch {record, type});
    while (g_hitches.size() > kMaxHitches) {
        g_hitches.pop_front();
    }
}

Stats toStats(const Accumulator &acc, float p95, float p99) {
    Stats stats;
    stats.frames = acc.frames;
    stats.loadFrames = acc.loadFrames;
    stats.minMs = acc.minMs;
    stats.avgMs = acc.frames > 0 ? static_cast<float>(acc.sumMs / acc.frames) : 0.0f;
    stats.p95Ms = p95;
    stats.p99Ms = p99;
    stats.maxMs = acc.maxMs;
    stats.over20 = acc.over20;
    stats.over33 = acc.over33;
    stats.over50 = acc.over50;
    stats.hitches = acc.over50;
    return stats;
}

float histogramPercentile(float p) {
    if (g_session.frames == 0) {
        return 0.0f;
    }
    int rank = std::max(1, static_cast<int>(std::ceil(p * g_session.frames)));
    int seen = 0;
    for (int bin = 0; bin <= kBins; ++bin) {
        seen += g_histogram[bin];
        if (seen >= rank) {
            return std::min(g_session.maxMs, (bin + 1) * kBinMs);
        }
    }
    return g_session.maxMs;
}

Stats windowStatsLocked() {
    std::vector<float> sorted(g_windowSamples);
    std::sort(sorted.begin(), sorted.end());
    int count = static_cast<int>(sorted.size());
    return toStats(g_window,
                   percentile(sorted.data(), count, 0.95f),
                   percentile(sorted.data(), count, 0.99f));
}

Stats sessionStatsLocked() {
    return toStats(g_session, histogramPercentile(0.95f), histogramPercentile(0.99f));
}

std::string statsJson(const Stats &stats) {
    std::ostringstream ss;
    ss.precision(2);
    ss << std::fixed;
    ss << "{\"frames\":" << stats.frames
       << ",\"loadFrames\":" << stats.loadFrames
       << ",\"min\":" << stats.minMs
       << ",\"avg\":" << stats.avgMs
       << ",\"p95\":" << stats.p95Ms
       << ",\"p99\":" << stats.p99Ms
       << ",\"max\":" << stats.maxMs
       << ",\"over20\":" << stats.over20
       << ",\"over33\":" << stats.over33
       << ",\"over50\":" << stats.over50 << "}";
    return ss.str();
}

} // namespace

const char *phaseName(Phase phase) {
    switch (phase) {
    case Phase::Input:
        return "input";
    case Phase::Sim:
        return "sim";
    case Phase::Present:
        return "present";
    case Phase::Render:
        return "render";
    case Phase::Swap:
        return "swap";
    case Phase::Audio:
        return "audio";
    case Phase::Resource:
        return "resource";
    case Phase::Script:
        return "script";
    case Phase::Shader:
        return "shader";
    case Phase::SimModule:
        return "module";
    case Phase::SimCombat:
        return "combat";
    case Phase::SimEvents:
        return "events";
    case Phase::SimAnimation:
        return "animation";
    case Phase::SimSave:
        return "save";
    default:
        return "?";
    }
}

bool isRecordingThread() {
    return g_threadSet && std::this_thread::get_id() == g_thread;
}

void beginFrame(uint64_t frame) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_threadSet) {
        g_thread = std::this_thread::get_id();
        g_threadSet = true;
    }
    g_current = FrameRecord {};
    g_current.frame = frame;
    g_frameStart = std::chrono::steady_clock::now();
}

const FrameRecord &endFrame() {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto elapsed = std::chrono::steady_clock::now() - g_frameStart;
    g_current.milliseconds = std::chrono::duration<float, std::milli>(elapsed).count();
    finalizeLast();
    g_last = g_current;
    g_lastPending = true;
    return g_last;
}

void markLastFrameLoad() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_lastPending) {
        g_last.load = true;
    }
}

bool enterPhase(Phase phase) {
    return g_depth[static_cast<int>(phase)]++ == 0;
}

void leavePhase(Phase phase) {
    int &depth = g_depth[static_cast<int>(phase)];
    depth = depth > 0 ? depth - 1 : 0;
}

void addPhaseTime(Phase phase, float milliseconds) {
    if (!isRecordingThread()) {
        return;
    }
    g_current.phases[static_cast<int>(phase)] += milliseconds;
}

void addTicks(int ticks) {
    if (isRecordingThread()) {
        g_current.ticks += ticks;
    }
}

void noteLoad(const std::string &name, float milliseconds) {
    if (!isRecordingThread()) {
        return;
    }
    auto &slowest = g_current.slowestLoads;
    for (int i = 0; i < kSlowestLoads; ++i) {
        if (milliseconds > slowest[i].milliseconds) {
            for (int j = kSlowestLoads - 1; j > i; --j) {
                slowest[j] = slowest[j - 1];
            }
            slowest[i] = NamedLoad {name, milliseconds};
            return;
        }
    }
}

void countResourceLoad() {
    if (isRecordingThread()) {
        ++g_current.resourceLoads;
    }
}

void countScriptRun() {
    if (isRecordingThread()) {
        ++g_current.scriptRuns;
    }
}

void countShaderCompile() {
    if (isRecordingThread()) {
        ++g_current.shaderCompiles;
    }
}

Stats windowStats() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return windowStatsLocked();
}

Stats sessionStats() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return sessionStatsLocked();
}

void resetWindow() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_window = Accumulator {};
    g_windowSamples.clear();
}

std::string json(bool consumeHitches) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::ostringstream ss;
    ss << "{\"window\":" << statsJson(windowStatsLocked())
       << ",\"session\":" << statsJson(sessionStatsLocked())
       << ",\"hitchTypes\":{";
    bool first = true;
    for (const auto &[type, count] : g_hitchTypes) {
        ss << (first ? "" : ",") << "\"" << type << "\":" << count;
        first = false;
    }
    ss << "},\"hitches\":[";
    first = true;
    for (const Hitch &hitch : g_hitches) {
        std::string record = recordJson(hitch.record);
        record.insert(1, "\"type\":\"" + hitch.type + "\",");
        ss << (first ? "" : ",") << record;
        first = false;
    }
    ss << "]}";
    if (consumeHitches) {
        g_hitches.clear();
    }
    return ss.str();
}

void reset() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_threadSet = false;
    g_depth.fill(0);
    g_current = FrameRecord {};
    g_last = FrameRecord {};
    g_lastPending = false;
    g_window = Accumulator {};
    g_windowSamples.clear();
    g_session = Accumulator {};
    std::fill(g_histogram.begin(), g_histogram.end(), 0);
    g_hitches.clear();
    g_hitchTypes.clear();
}

float percentile(const float *sorted, int count, float p) {
    if (count <= 0) {
        return 0.0f;
    }
    int rank = std::clamp(static_cast<int>(std::ceil(p * count)), 1, count);
    return sorted[rank - 1];
}

} // namespace frameperf

} // namespace reone
