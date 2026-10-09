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

#include <string>

namespace reone {

/**
 * Render diagnostics: problems that make a frame look wrong without stopping
 * it (a missing texture, a failed shader, a NaN transform, a GL error).
 *
 * Each problem is reported once per key, on the Render log channel, so a long
 * session never floods the log; every occurrence is still counted. Category
 * is a short fixed word ("gl", "texture", "model", "supermodel", "animation",
 * "shader", "transform", "mesh", "emitter", "lights"); key names the resource
 * or occurrence within it.
 */
namespace renderdiag {

/** Reports a problem. Returns true when this is the first report of the key. */
bool report(const std::string &category, const std::string &key, const std::string &message);

/** Whether this key was already reported. */
bool reported(const std::string &category, const std::string &key);

/** Total occurrences reported under a category, repeats included. */
int count(const std::string &category);

/** Distinct keys reported under a category. */
int distinct(const std::string &category);

/** One line of JSON: {"category":{"count":n,"distinct":n,"keys":[...]},...}, at most 20 keys each. */
std::string summaryJson();

/** Forgets everything reported (tests). */
void reset();

} // namespace renderdiag

} // namespace reone
