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

#include "reone/game/coopsession.h"

#include <algorithm>

#include <glm/gtx/norm.hpp>

#include "reone/game/coopstandin.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/game/pathfinder.h"
#include "reone/game/reputes.h"
#include "reone/system/logutil.h"

using namespace reone::net;

namespace reone {

namespace game {

namespace {

/** A jump this soon after a scene was running still belongs to it (a fade-out, then the move, then the dialog). */
constexpr float kSceneJumpSeconds = 3.0f;
/** Scripted moves shorter than this stage a scene in place; they are not travel. */
constexpr float kMinTravelJump = 2.0f;
/** Ticks between checking the host's reputation table for changes. */
constexpr uint64_t kReputationCheckTicks = 10;
/** A conversation starting this soon after a module load waits for guests still loading it, at most this long. */
constexpr float kConversationHoldSeconds = 10.0f;

/** A leader this far from where it stood when a player's character went away was moved by the scene: the party goes with it. */
constexpr float kPartyMovedDistance = 4.0f;
/** A remembered spot the walkmesh moves a character further than this from is no longer the spot. */
constexpr float kAwaySpotSlack = 1.5f;

/** Can a character standing at from walk to to? Same walkmesh region, and no closed locked door between them when a plan can be made. */
bool reachable(Area &area, const glm::vec3 &from, const glm::vec3 &to) {
    auto &pathfinder = area.pathfinder();
    if (pathfinder.uni.regions.empty()) {
        return true; // no walkmesh to ask (a test area)
    }
    if (!pathExists(pathfinder, from, to)) {
        return false;
    }
    area.refreshPathBarriers();
    bool slot = std::any_of(pathfinder.paths.begin(), pathfinder.paths.end(), [](const AStarPath &path) { return !path.active; });
    if (!slot) {
        return true;
    }
    if (auto path = createPath(pathfinder, from, to)) {
        releasePath(pathfinder, *path);
        return true;
    }
    return false;
}

} // namespace

bool CoopSession::hostSharedScene() const {
    // A workbench's own talk is the host's personal screen, not a scene.
    bool personal = coopPersonalConversation(_game);
    if (_game._movie || (!personal && _game.isConversationActive())) {
        return true;
    }
    if (_game._globalFade.opacity() > 0.0f || _game._globalFade.fading() || _game._globalFade.dialogPending()) {
        return true;
    }
    auto leader = _game.party().getLeader();
    return leader && ((leader->isInConversation() && !personal) || !leader->isCommandable());
}

void CoopSession::hostUpdateScenes() {
    if (_inModuleSnapshot) {
        return;
    }
    bool on = hostSharedScene();
    if (on) {
        _sceneTick = _game.tickCount();
    }
    if (_sceneWasOn && !on) {
        if (auto leader = _game.party().getLeader()) {
            hostPullAlongSealedGuests(*leader);
        }
    }
    _sceneWasOn = on;
}

void CoopSession::onScriptedJump(const Object &actor, const glm::vec3 &from, const glm::vec3 &to) {
    if (!isHost() || !freeMode() || _inModuleSnapshot) {
        return;
    }
    auto leader = _game.party().getLeader();
    if (!leader || leader->id() != actor.id()) {
        // Scripts move "the party" member by member; the leader's jump moves the guests.
        return;
    }
    if (glm::distance2(glm::vec2(from), glm::vec2(to)) < kMinTravelJump * kMinTravelJump) {
        return;
    }
    uint64_t grace = static_cast<uint64_t>(kSceneJumpSeconds * static_cast<float>(std::max(1, _game.tickRate())));
    bool recent = _sceneTick != 0 && _game.tickCount() <= _sceneTick + grace;
    bool scene = hostSharedScene();
    size_t present = std::count_if(_clients.begin(), _clients.end(), [](const auto &entry) { return entry.second.guestId != 0; });
    info("Co-op: a script jumps the leader to (" + std::to_string(to.x) + ", " + std::to_string(to.y) + ")" +
         (scene ? " in a scene" : recent ? " just after a scene" : " in free play") + "; " + std::to_string(present) +
         " guest characters in the world");
    if (!scene && !recent) {
        return;
    }
    hostMoveGuestsWith(*leader, to);
}

void CoopSession::hostMoveGuestsWith(const Creature &leader, const glm::vec3 &destination) {
    auto module = _game.module();
    if (!module || !module->area()) {
        return;
    }
    auto &area = *module->area();
    int index = 0;
    for (auto &[connection, client] : _clients) {
        auto creature = client.guestId ? _game.getObjectById<Creature>(client.guestId) : nullptr;
        if (!creature || !creature->isRuntimeLive()) {
            continue;
        }
        // The stand-ins' formation around the leader, each spot made free of
        // other creatures; a spot the destination does not join (the far side
        // of a wall) is passed over for one around the destination itself.
        glm::vec3 spot = area.findFreeCreaturePosition(*creature, destination);
        for (const auto &offset : coopStandInOffsets(leader.getFacing(), index)) {
            glm::vec3 candidate = area.findFreeCreaturePosition(*creature, destination + glm::vec3(offset, 0.0f));
            if (pathExists(area.pathfinder(), destination, candidate)) {
                spot = candidate;
                break;
            }
        }
        ++index;
        creature->clearAllActions();
        creature->setPosition(spot);
        creature->setFacing(leader.getFacing());
        creature->snapPresentation();
        area.determineObjectRoom(*creature);
        ++_guestSceneJumps;
        info("Co-op: " + client.name + "'s character goes with the scene to (" + std::to_string(spot.x) + ", " +
             std::to_string(spot.y) + ")");
    }
}

void CoopSession::hostPullAlongSealedGuests(const Creature &leader) {
    auto module = _game.module();
    if (!module || !module->area() || !freeMode()) {
        return;
    }
    auto &area = *module->area();
    area.refreshPathBarriers();
    auto &pathfinder = area.pathfinder();
    for (auto &[connection, client] : _clients) {
        auto creature = client.guestId ? _game.getObjectById<Creature>(client.guestId) : nullptr;
        if (!creature || !creature->isRuntimeLive() || creature->isDead()) {
            continue;
        }
        if (glm::distance2(creature->position(), leader.position()) < kMinTravelJump * kMinTravelJump) {
            continue;
        }
        // Planning honours closed locked doors (path barriers): no plan from
        // the guest to the leader is a guest sealed away.
        bool slot = std::any_of(pathfinder.paths.begin(), pathfinder.paths.end(), [](const AStarPath &path) { return !path.active; });
        if (!slot) {
            continue;
        }
        if (auto path = createPath(pathfinder, creature->position(), leader.position())) {
            releasePath(pathfinder, *path);
            continue;
        }
        glm::vec3 spot = area.findFreeCreaturePosition(*creature, leader.position());
        creature->clearAllActions();
        creature->setPosition(spot);
        creature->setFacing(leader.getFacing());
        creature->snapPresentation();
        area.determineObjectRoom(*creature);
        ++_guestsPulledAlong;
        info("Co-op: " + client.name + "'s character could not reach the leader after the scene; brought along");
    }
}

bool CoopSession::hostGuestsLoading() const {
    return std::any_of(_clients.begin(), _clients.end(), [this](const auto &entry) {
        const auto &client = entry.second;
        return client.welcomed && client.moduleGeneration == _generation && client.loadedGeneration != _generation;
    });
}

bool CoopSession::holdConversation(const std::shared_ptr<Object> &owner, const std::string &resRef,
                                   const GlobalFade::DialogTicket &admission) {
    if (!isHost() || _releasingConversation || !hasHostModule() || !hostGuestsLoading()) {
        return false;
    }
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<float> sinceLoad = now - _moduleChangedAt;
    if (sinceLoad.count() > kConversationHoldSeconds) {
        return false; // not a module's opening conversation: a slow guest catches up as it can
    }
    if (auto placeable = std::dynamic_pointer_cast<Placeable>(owner);
        placeable && !placeable->getOnUsed().empty() && coopScriptBench(_game, placeable->getOnUsed()) != CoopBench::None) {
        return false; // a workbench's own talk is nobody else's
    }
    bool first = !_heldConversation.active;
    _heldConversation.active = true;
    _heldConversation.hadOwner = owner != nullptr;
    _heldConversation.owner = owner;
    _heldConversation.resRef = resRef;
    _heldConversation.admission = admission;
    if (first) {
        _heldConversation.since = now;
        ++_conversationsHeld;
    }
    info("Co-op: conversation " + resRef + " waits for players still loading " + _game.module()->name());
    return true;
}

void CoopSession::hostUpdateHeldConversation() {
    if (!_heldConversation.active) {
        return;
    }
    std::chrono::duration<float> waited = std::chrono::steady_clock::now() - _heldConversation.since;
    bool timedOut = waited.count() >= kConversationHoldSeconds;
    if (hostGuestsLoading() && !timedOut) {
        return;
    }
    auto held = std::move(_heldConversation);
    _heldConversation = HeldConversation();
    auto owner = held.owner.lock();
    if (held.hadOwner && (!owner || !owner->isRuntimeLive())) {
        info("Co-op: the held conversation " + held.resRef + " lost its owner; dropped");
        return;
    }
    info("Co-op: conversation " + held.resRef + (timedOut ? " starts: a player is still loading after " : " starts: every player has loaded, after ") +
         std::to_string(waited.count()) + " s");
    _releasingConversation = true;
    _game.startDialog(owner, held.resRef, held.admission);
    _releasingConversation = false;
}

void CoopSession::hostRememberAwaySpot(HostClient &client, const Creature &creature) {
    auto leader = _game.party().getLeader();
    if (!leader || creature.isDead()) {
        client.away = HostClient::AwaySpot();
        return;
    }
    client.away.valid = true;
    client.away.position = creature.position();
    client.away.facing = creature.getFacing();
    client.away.leader = leader->position();
}

bool CoopSession::hostPlaceAtAwaySpot(HostClient &client, Creature &creature, const Creature &leader) {
    auto away = client.away;
    client.away = HostClient::AwaySpot();
    auto module = _game.module();
    if (!away.valid || !module || !module->area()) {
        return false;
    }
    auto &area = *module->area();
    if (glm::distance(glm::vec2(leader.position()), glm::vec2(away.leader)) > kPartyMovedDistance) {
        info("Co-op: the scene moved the party; " + client.name + "'s character comes back beside the leader");
        return false;
    }
    creature.setPosition(away.position);
    if (!area.landObject(creature) ||
        glm::distance(glm::vec2(creature.position()), glm::vec2(away.position)) > kAwaySpotSlack) {
        info("Co-op: " + client.name + "'s spot is no longer walkable; back beside the leader");
        return false;
    }
    glm::vec3 spot = area.findFreeCreaturePosition(creature, creature.position());
    if (!reachable(area, spot, leader.position())) {
        info("Co-op: " + client.name + "'s spot is cut off from the leader now; back beside the leader");
        return false;
    }
    creature.setPosition(spot);
    creature.setFacing(away.facing);
    ++_guestsReturned;
    info("Co-op: " + client.name + "'s character comes back where it stood (" + std::to_string(spot.x) + ", " +
         std::to_string(spot.y) + ")");
    return true;
}

void CoopSession::hostSendReputation() {
    uint64_t tick = _game.tickCount();
    bool due = _reputationBytes.empty() || tick >= _reputationCheckTick + kReputationCheckTicks;
    if (due) {
        _reputationCheckTick = tick;
        auto state = _game._services.game.reputes.state();
        ReputationTable table;
        size_t count = std::min(state.values.size(), kMaxFactions);
        table.values.assign(count, std::vector<uint8_t>(count, 50));
        for (size_t source = 0; source < count; ++source) {
            const auto &row = state.values[source];
            for (size_t target = 0; target < count && target < row.size(); ++target) {
                table.values[source][target] = static_cast<uint8_t>(std::clamp(row[target], 0, 100));
            }
        }
        _reputationBytes = encode(table);
    }
    for (auto &[connection, client] : _clients) {
        if (client.welcomed && client.reputationSent != _reputationBytes) {
            client.reputationSent = _reputationBytes;
            send(connection, _reputationBytes);
        }
    }
}

void CoopSession::clientApplyReputation() {
    if (!isClient() || !_hostReputation) {
        return;
    }
    const auto &values = _hostReputation->values;
    auto &reputes = _game._services.game.reputes;
    bool same = true;
    for (size_t source = 0; source < values.size() && same; ++source) {
        for (size_t target = 0; target < values.size(); ++target) {
            if (reputes.getReputation(static_cast<Faction>(source), static_cast<Faction>(target)) != values[source][target]) {
                same = false;
                break;
            }
        }
    }
    if (same) {
        return;
    }
    auto state = reputes.state();
    size_t count = std::max(values.size(), state.values.size());
    state.values.resize(count);
    for (size_t source = 0; source < count; ++source) {
        state.values[source].resize(count, 50);
        if (source < values.size()) {
            for (size_t target = 0; target < values.size(); ++target) {
                state.values[source][target] = values[source][target];
            }
        }
    }
    while (state.factions.size() < count) {
        state.factions.push_back({});
    }
    while (state.labels.size() < count) {
        state.labels.emplace_back();
    }
    reputes.replace(std::move(state));
    ++_reputationTablesApplied;
}

} // namespace game

} // namespace reone
