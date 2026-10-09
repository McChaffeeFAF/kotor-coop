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

#include "reone/system/fixedstep.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace reone {

// Wall time is measured in float seconds; a frame that is a whole number of
// steps long must not come out one step short because 1/60 + 1/60 lands a
// hair under 1/30. Anything this close to a step boundary counts as reaching it.
static constexpr double kBoundaryEpsilon = 1e-7;

FixedStepClock::FixedStepClock(int tickRate, int maxSteps) {
    setTickRate(tickRate);
    setMaxSteps(maxSteps);
}

void FixedStepClock::setTickRate(int tickRate) {
    if (tickRate < 1 || tickRate > 1000) {
        throw std::invalid_argument("tick rate must be between 1 and 1000");
    }
    _tickRate = tickRate;
    _step = 1.0 / tickRate;
    _stepSeconds = static_cast<float>(_step);
    _accumulator = 0.0;
}

void FixedStepClock::setMaxSteps(int maxSteps) {
    _maxSteps = std::max(1, maxSteps);
}

int FixedStepClock::advance(double seconds, int maxSteps) {
    if (!std::isfinite(seconds) || seconds < 0.0) {
        seconds = 0.0;
    }
    int limit = maxSteps > 0 ? maxSteps : _maxSteps;
    _accumulator += seconds;
    double due = std::floor((_accumulator + kBoundaryEpsilon * _step) / _step);
    int steps;
    if (due > limit) {
        _droppedSteps += static_cast<uint64_t>(due) - limit;
        steps = limit;
        // Keep only the sub-step fraction: the overdue whole steps are gone.
        _accumulator -= due * _step;
    } else {
        steps = static_cast<int>(due);
        _accumulator -= steps * _step;
    }
    if (_accumulator < 0.0) {
        _accumulator = 0.0;
    }
    _steps += steps;
    return steps;
}

void FixedStepClock::reset() {
    _accumulator = 0.0;
}

float FixedStepClock::alpha() const {
    return static_cast<float>(std::clamp(_accumulator / _step, 0.0, 1.0));
}

} // namespace reone
