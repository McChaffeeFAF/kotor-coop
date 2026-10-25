/* Copyright (c) 2026 The reone project contributors */

// Free Mode guests in the host's scenes, and the factions every guest sees
// (docs/guest_scenes.md).
//
// Scenes: a script that jumps the host's leader in a shared scene (a
// conversation, a cutscene, a fade it started) takes the guests standing in
// the area along, to free spots around the leader's destination, on the same
// tick and so under the same fade. When a scene ends, a guest the leader can
// no longer be reached from (behind a door the scene sealed) is brought to the
// leader. Guests that left for the scene come back beside the leader anyway.
//
// Factions: a guest's client runs no scripts, so the host replicates each
// creature's faction (EntityState::faction) and its reputation table
// (ReputationTable); the client's own hostility tests then agree with the
// host's for the guest's character.


// The shared area map: the explored grid is one grid per area, filled by every
// player's characters on the host (Area::revealAroundParty) and saved with the
// area. The host sends it, with the enabled map notes, to every client when it
// changes; a client only draws it and never reveals cells of its own.

#include "reone/game/coopsession.h"

#include <algorithm>
#include <set>

#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/module.h"
#include "reone/game/object/waypoint.h"

namespace reone {

namespace game {

using namespace net;

static constexpr uint64_t kMapCheckTicks = 10;

MapState coopMapState(Area &area) {
    MapState state;
    state.area = area.name().substr(0, kMaxStringBytes);
    const auto &exploration = area.mapExploration();
    state.columns = static_cast<uint16_t>(std::min<int>(exploration.columns(), kMaxMapColumns));
    state.rows = static_cast<uint16_t>(std::min<int>(exploration.rows(), kMaxMapRows));
    if (state.columns != exploration.columns() || state.rows != exploration.rows()) {
        state.columns = state.rows = 0; // too large to send: the clients keep their own
        return state;
    }
    state.cells = exploration.bytes();
    std::set<std::string> tags;
    for (const auto &object : area.getObjectsByType(ObjectType::Waypoint)) {
        auto waypoint = std::static_pointer_cast<Waypoint>(object);
        if (waypoint->hasMapNote() && waypoint->isMapNoteEnabled() && !waypoint->mapNote().empty() &&
            !waypoint->tag().empty() && waypoint->tag().size() <= kMaxStringBytes) {
            tags.insert(waypoint->tag());
        }
    }
    for (const auto &tag : tags) {
        if (state.notes.size() < kMaxMapNotes) {
            state.notes.push_back(tag);
        }
    }
    return state;
}

bool coopApplyMapState(Area &area, const MapState &state) {
    if (state.area != area.name().substr(0, kMaxStringBytes) || state.columns == 0) {
        return false;
    }
    if (!area.mapExploration().setBytes(state.columns, state.rows, state.cells)) {
        return false;
    }
    std::set<std::string> enabled(state.notes.begin(), state.notes.end());
    for (const auto &object : area.getObjectsByType(ObjectType::Waypoint)) {
        auto waypoint = std::static_pointer_cast<Waypoint>(object);
        if (waypoint->hasMapNote()) {
            waypoint->setMapNoteEnabled(enabled.count(waypoint->tag()) > 0);
        }
    }
    return true;
}

void CoopSession::hostSendMap() {
    uint64_t tick = _game.tickCount();
    bool due = _mapBytes.empty() || tick >= _mapCheckTick + kMapCheckTicks;
    if (due) {
        _mapCheckTick = tick;
        auto module = _game.module();
        auto area = module ? module->area() : nullptr;
        _mapBytes = area && area->mapExploration().columns() > 0 ? encode(coopMapState(*area)) : std::vector<uint8_t>();
    }
    if (_mapBytes.empty()) {
        return;
    }
    for (auto &[connection, client] : _clients) {
        if (client.welcomed && client.mapSent != _mapBytes) {
            client.mapSent = _mapBytes;
            send(connection, _mapBytes);
        }
    }
}

void CoopSession::clientApplyMap() {
    if (!isClient() || !_hostMap) {
        return;
    }
    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) {
        return;
    }
    if (coopApplyMapState(*area, *_hostMap)) {
        ++_mapsApplied;
    }
}

} // namespace game

} // namespace reone
