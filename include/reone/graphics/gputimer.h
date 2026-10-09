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

namespace graphics {

/**
 * GPU time per render pass, from GL timestamp queries (the `gfxperf`
 * console command). Off by default: when off, a scope issues no GL calls.
 * Results are read a few frames late so measuring does not stall the GPU.
 * Times are summed per name within a frame and averaged over frames.
 */
namespace gputimer {

void setEnabled(bool enabled);
bool enabled();

void begin(const char *name);
void end(const char *name);

/** Close the frame: collect finished results. Main thread, with a GL context. */
void endFrame();

void reset();

/** {"frames":N,"<name>":ms,...}, the mean milliseconds per frame. */
std::string json();

class Scope {
public:
    explicit Scope(const char *name) :
        _name(name) {
        begin(_name);
    }
    ~Scope() { end(_name); }

    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

private:
    const char *_name;
};

} // namespace gputimer

} // namespace graphics

} // namespace reone
