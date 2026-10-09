/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace reone {

namespace game {

/**
 * Object identity across a co-op session.
 *
 * The host's object ids are the network ids. A client never allocates one:
 * it binds each host id to its own copy of the object. Module objects are
 * matched by a load key both sides compute from the GIT as the area loads
 * (before any script runs); objects created later arrive with their
 * template and are spawned by the client under the host's id.
 */
class NetIdMap {
public:
    void bind(uint32_t hostId, uint32_t localId);
    void unbindHost(uint32_t hostId);
    void clear();

    std::optional<uint32_t> local(uint32_t hostId) const;
    std::optional<uint32_t> host(uint32_t localId) const;
    size_t size() const { return _hostToLocal.size(); }
    const std::map<uint32_t, uint32_t> &hostToLocal() const { return _hostToLocal; }

private:
    std::map<uint32_t, uint32_t> _hostToLocal;
    std::map<uint32_t, uint32_t> _localToHost;
};

/** One object as the area's GIT built it, before scripts ran. */
struct LoadEntry {
    uint32_t id {0};
    int type {0};
    std::string tag;
    std::string resref;
    float x {0.0f};
    float y {0.0f};
};

/**
 * Stable load keys: "type:tag:template:x:y" with the position in decimetres,
 * and "#n" for the n-th repeat of an identical key, in load order. Both
 * installs load the same GIT, so both compute the same key for the same
 * object, whatever ids their registries handed out.
 */
std::vector<std::pair<uint32_t, std::string>> assignLoadKeys(const std::vector<LoadEntry> &entries);

/** The key of the player character: not in any GIT, bound by role. */
constexpr const char *kPlayerLoadKey = "pc";

} // namespace game

} // namespace reone
