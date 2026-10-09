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

#include <cstdint>

namespace reone {

/**
 * Fixed-step accumulator. Each rendered frame adds its (scaled) wall time and
 * learns how many whole simulation steps are due; the fraction left over is
 * carried to the next frame and doubles as the render interpolation factor.
 *
 * A frame never owes more than maxSteps steps: past that, the overdue time is
 * dropped rather than carried, so a long stall (a breakpoint, a synchronous
 * load, a slow machine) cannot snowball into ever longer catch-up frames.
 */
class FixedStepClock {
public:
    static constexpr int kDefaultTickRate = 30;
    static constexpr int kDefaultMaxSteps = 5;

    explicit FixedStepClock(int tickRate = kDefaultTickRate, int maxSteps = kDefaultMaxSteps);

    /** Changes the rate; the accumulated fraction is discarded. */
    void setTickRate(int tickRate);

    void setMaxSteps(int maxSteps);

    /**
     * Adds frame time, in seconds, and returns the number of steps to run
     * now. Non-finite and negative times count as zero.
     */
    int advance(double seconds, int maxSteps = -1);

    /** Clears the accumulated fraction. */
    void reset();

    int tickRate() const { return _tickRate; }
    int maxSteps() const { return _maxSteps; }

    /** Simulation step length in seconds, 1 / tickRate. */
    float stepSeconds() const { return _stepSeconds; }

    /** Accumulated fraction of a step, in [0, 1): the render interpolation factor. */
    float alpha() const;

    /** Steps handed out so far. */
    uint64_t steps() const { return _steps; }

    /** Steps dropped by the per-frame clamp so far. */
    uint64_t droppedSteps() const { return _droppedSteps; }

private:
    int _tickRate {kDefaultTickRate};
    int _maxSteps {kDefaultMaxSteps};
    float _stepSeconds {1.0f / kDefaultTickRate};
    double _step {1.0 / kDefaultTickRate};
    double _accumulator {0.0};
    uint64_t _steps {0};
    uint64_t _droppedSteps {0};
};

} // namespace reone
