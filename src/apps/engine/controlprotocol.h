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

namespace reone {

/**
 * One command received over the control channel. Ids are assigned in arrival
 * order, starting at 1, and are echoed back in the reply.
 */
struct ControlRequest {
    uint64_t id {0};
    std::string command;
};

/**
 * Result of executing one control command, in the form it is reported back.
 */
struct ControlReply {
    uint64_t id {0};
    std::string command;
    bool ok {true};
    std::vector<std::string> output;
};

/**
 * Formats a reply as a single JSON line (without the trailing newline):
 * {"id":n,"cmd":"...","ok":true,"output":["..."],"frame":n,"time":s,"tick":n}
 * "tick" (the simulation tick) is present when given.
 */
std::string formatControlReply(const ControlReply &reply, uint64_t frame, double time,
                               std::optional<uint64_t> tick = std::nullopt);

/**
 * Splits a byte stream into commands, one per line. Lines are stripped of
 * surrounding whitespace (including \r) and empty lines are dropped.
 */
class ControlLineSplitter {
public:
    static constexpr size_t kMaxLineLength = 64 * 1024;

    void append(std::string_view data);
    std::optional<std::string> next();

private:
    std::string _buffer;
};

/**
 * Thread-safe FIFO between the network thread (producer) and the main thread
 * (consumer).
 */
class ControlQueue : boost::noncopyable {
public:
    uint64_t push(std::string command);
    std::optional<ControlRequest> pop();
    bool empty() const;

private:
    mutable std::mutex _mutex;
    std::deque<ControlRequest> _requests;
    uint64_t _nextId {1};
};

} // namespace reone
