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

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "exception/validation.h"

namespace reone {

// Helpers for text-format readers: untrusted text must be rejected with a
// ValidationException, never an out-of-range index or a std::sto* exception.

inline const std::string &tokenAt(const std::vector<std::string> &tokens, size_t index) {
    if (index >= tokens.size()) {
        throw ValidationException("Missing token " + std::to_string(index) + ", line has " + std::to_string(tokens.size()));
    }
    return tokens[index];
}

inline int parseInt(const std::string &s) {
    try {
        return std::stoi(s);
    } catch (const std::logic_error &) {
        throw ValidationException("Invalid integer: '" + s.substr(0, 32) + "'");
    }
}

inline float parseFloat(const std::string &s) {
    try {
        return std::stof(s);
    } catch (const std::logic_error &) {
        throw ValidationException("Invalid number: '" + s.substr(0, 32) + "'");
    }
}

inline double parseDouble(const std::string &s) {
    try {
        return std::stod(s);
    } catch (const std::logic_error &) {
        throw ValidationException("Invalid number: '" + s.substr(0, 32) + "'");
    }
}

/// Reserves for a count read from the file without letting it size an allocation.
template <class Vector>
inline void reserveBounded(Vector &v, long long count, size_t limit = 4096) {
    if (count > 0) {
        v.reserve(std::min(static_cast<size_t>(count), limit));
    }
}

} // namespace reone
