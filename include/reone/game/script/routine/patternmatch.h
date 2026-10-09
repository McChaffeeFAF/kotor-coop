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

#include <string>
#include <vector>

namespace reone {

namespace game {

/**
 * NWScript string patterns (TestStringAgainstPattern, listen patterns):
 * `**` any run of characters (possibly none), `*w` one or more whitespace,
 * `*n` one or more digits, `*p` one or more punctuation, `*a` one or more
 * letters, `|` alternatives and `(` `)` blocks. Matching is case-insensitive
 * and anchored at the start of the text only: the documented example
 * "**(bash|open|unlock)**(chest|door)" matches "open the door please", so
 * text after the pattern's last piece is ignored. Each literal run, wildcard or block of the
 * pattern is one numbered substring, in pattern order, for GetMatchedSubstring.
 */
struct PatternMatch {
    bool matched {false};
    std::vector<std::string> substrings;
};

PatternMatch matchPattern(const std::string &pattern, const std::string &text);

} // namespace game

} // namespace reone
