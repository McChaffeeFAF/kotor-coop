/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/coopidmap.h"

#include <cctype>
#include <cmath>

namespace reone {

namespace game {

void NetIdMap::bind(uint32_t hostId, uint32_t localId) {
    unbindHost(hostId);
    auto previousHost = _localToHost.find(localId);
    if (previousHost != _localToHost.end()) {
        _hostToLocal.erase(previousHost->second);
        _localToHost.erase(previousHost);
    }
    _hostToLocal[hostId] = localId;
    _localToHost[localId] = hostId;
}

void NetIdMap::unbindHost(uint32_t hostId) {
    auto it = _hostToLocal.find(hostId);
    if (it == _hostToLocal.end()) {
        return;
    }
    _localToHost.erase(it->second);
    _hostToLocal.erase(it);
}

void NetIdMap::clear() {
    _hostToLocal.clear();
    _localToHost.clear();
}

std::optional<uint32_t> NetIdMap::local(uint32_t hostId) const {
    auto it = _hostToLocal.find(hostId);
    if (it == _hostToLocal.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<uint32_t> NetIdMap::host(uint32_t localId) const {
    auto it = _localToHost.find(localId);
    if (it == _localToHost.end()) {
        return std::nullopt;
    }
    return it->second;
}

static std::string lowerCopy(const std::string &text) {
    std::string result(text);
    for (auto &c : result) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

std::vector<std::pair<uint32_t, std::string>> assignLoadKeys(const std::vector<LoadEntry> &entries) {
    std::vector<std::pair<uint32_t, std::string>> result;
    result.reserve(entries.size());
    std::map<std::string, int> seen;
    for (const auto &entry : entries) {
        std::string key = std::to_string(entry.type) + ":" + lowerCopy(entry.tag) + ":" + lowerCopy(entry.resref) + ":" +
                          std::to_string(std::lround(entry.x * 10.0f)) + ":" + std::to_string(std::lround(entry.y * 10.0f));
        int repeat = seen[key]++;
        if (repeat > 0) {
            key += "#" + std::to_string(repeat);
        }
        result.emplace_back(entry.id, std::move(key));
    }
    return result;
}

} // namespace game

} // namespace reone
