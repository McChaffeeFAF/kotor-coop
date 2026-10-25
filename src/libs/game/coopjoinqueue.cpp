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

#include "reone/game/coopjoinqueue.h"

#include <algorithm>

namespace reone {

namespace game {

bool CoopJoinQueue::request(uint32_t client, double now, uint64_t order) {
    if (client == 0 || contains(client)) {
        return false;
    }
    Request request {client, now, now, order};
    auto at = _requests.end();
    if (order != 0) {
        // Before the first later arrival, but never ahead of the prompt that is up.
        at = std::find_if(_requests.begin(), _requests.end(), [this, order](const Request &r) {
            return r.order > order && r.client != _prompted;
        });
    }
    _requests.insert(at, request);
    return true;
}

bool CoopJoinQueue::remove(uint32_t client) {
    auto it = std::find_if(_requests.begin(), _requests.end(), [client](const Request &r) { return r.client == client; });
    if (it != _requests.end()) {
        _requests.erase(it);
    }
    if (client != 0 && _prompted == client) {
        _prompted = 0;
        return true;
    }
    return false;
}

bool CoopJoinQueue::contains(uint32_t client) const {
    return std::any_of(_requests.begin(), _requests.end(), [client](const Request &r) { return r.client == client; });
}

uint32_t CoopJoinQueue::next() const {
    if (_prompted != 0 || _requests.empty()) {
        return 0;
    }
    return _requests.front().client;
}

void CoopJoinQueue::shown(uint32_t client, uint64_t serial) {
    if (!contains(client)) {
        return;
    }
    _prompted = client;
    _serial = serial;
}

uint32_t CoopJoinQueue::checkPrompt(bool popupVisible, uint64_t popupSerial) {
    if (_prompted == 0 || (popupVisible && popupSerial == _serial)) {
        return 0;
    }
    uint32_t client = _prompted;
    _prompted = 0;
    ++_lost;
    // Asked again first: the prompt the host never got to answer keeps its turn.
    auto it = std::find_if(_requests.begin(), _requests.end(), [client](const Request &r) { return r.client == client; });
    if (it != _requests.end() && it != _requests.begin()) {
        Request request = *it;
        _requests.erase(it);
        _requests.push_front(request);
    }
    return client;
}

std::vector<uint32_t> CoopJoinQueue::overdue(double now, double after) {
    std::vector<uint32_t> result;
    for (auto &request : _requests) {
        if (now - request.reported >= after) {
            request.reported = now;
            result.push_back(request.client);
        }
    }
    return result;
}

double CoopJoinQueue::waited(uint32_t client, double now) const {
    for (const auto &request : _requests) {
        if (request.client == client) {
            return now - request.since;
        }
    }
    return 0.0;
}

std::vector<uint32_t> CoopJoinQueue::clients() const {
    std::vector<uint32_t> result;
    result.reserve(_requests.size());
    for (const auto &request : _requests) {
        result.push_back(request.client);
    }
    return result;
}

} // namespace game

} // namespace reone
