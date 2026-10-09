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

#include "controlprotocol.h"

#include "reone/system/stringutil.h"

#include <boost/json.hpp>

namespace reone {

std::string formatControlReply(const ControlReply &reply, uint64_t frame, double time,
                               std::optional<uint64_t> tick) {
    boost::json::array output;
    output.reserve(reply.output.size());
    for (auto &line : reply.output) {
        output.emplace_back(line);
    }
    boost::json::object obj;
    obj["id"] = reply.id;
    obj["cmd"] = reply.command;
    obj["ok"] = reply.ok;
    obj["output"] = std::move(output);
    obj["frame"] = frame;
    obj["time"] = time;
    if (tick) {
        obj["tick"] = *tick;
    }
    return boost::json::serialize(obj);
}

void ControlLineSplitter::append(std::string_view data) {
    _buffer.append(data);
}

std::optional<std::string> ControlLineSplitter::next() {
    while (true) {
        auto newline = _buffer.find('\n');
        if (newline == std::string::npos) {
            if (_buffer.size() > kMaxLineLength) {
                // A client that never sends a newline must not grow this
                // buffer without bound; the partial line is discarded.
                _buffer.clear();
            }
            return std::nullopt;
        }
        std::string line(string_strip(std::string_view(_buffer).substr(0, newline)));
        _buffer.erase(0, newline + 1);
        if (!line.empty()) {
            return line;
        }
    }
}

uint64_t ControlQueue::push(std::string command) {
    std::lock_guard<std::mutex> lock(_mutex);
    uint64_t id = _nextId++;
    _requests.push_back(ControlRequest {id, std::move(command)});
    return id;
}

std::optional<ControlRequest> ControlQueue::pop() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_requests.empty()) {
        return std::nullopt;
    }
    auto request = std::move(_requests.front());
    _requests.pop_front();
    return request;
}

bool ControlQueue::empty() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _requests.empty();
}

} // namespace reone
