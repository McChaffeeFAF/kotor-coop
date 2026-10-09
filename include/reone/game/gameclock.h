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

#include <algorithm>
#include <cstdint>

namespace reone {

namespace game {

// The IFO game clock (design note c11_data, "Clock"). K1 shows no day and
// night; this is only the clock scripts read.

constexpr int kClockMonthsPerYear = 12;
constexpr int kClockDaysPerMonth = 28;

/** Days since year 0, month 1, day 1 (month and day are 1-based). */
inline uint64_t calendarDayIndex(uint32_t year, int month, int day) {
    month = std::clamp(month, 1, kClockMonthsPerYear);
    day = std::clamp(day, 1, kClockDaysPerMonth);
    return (static_cast<uint64_t>(year) * kClockMonthsPerYear + (month - 1)) * kClockDaysPerMonth + (day - 1);
}

struct DayState {
    bool day {false};
    bool night {false};
    bool dawn {false};
    bool dusk {false};
};

/**
 * Dawn is the dawn hour and dusk the dusk hour; day and night are split by
 * them. When the two hours are equal it is always day.
 */
inline DayState dayStateAt(int hour, int dawnHour, int duskHour) {
    DayState state;
    if (dawnHour == duskHour) {
        state.day = true;
        return state;
    }
    if (hour == dawnHour) {
        state.dawn = true;
        return state;
    }
    if (hour == duskHour) {
        state.dusk = true;
        return state;
    }
    bool day = dawnHour < duskHour
                   ? (hour > dawnHour && hour < duskHour)
                   : (hour > dawnHour || hour < duskHour);
    state.day = day;
    state.night = !day;
    return state;
}

} // namespace game

} // namespace reone
