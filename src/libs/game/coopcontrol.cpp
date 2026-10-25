/* Copyright (c) 2026 The reone project contributors */

// Guest control (playtest 2026-10-22, design note coop_playtest_1022 rows
// 1, 2/3, 12, 13): a guest's movement keys steer its creature directly on
// the host, the guest sees its own creature from the newest state the host
// sent (with a short, presentation-only prediction while the keys are held),
// and input with nothing to drive says so instead of vanishing.

#include "reone/system/portablemath.h"
#include "reone/game/coopsession.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include "reone/game/action.h"
#include "reone/game/coopreplication.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/player.h"
#include "reone/system/logutil.h"

using namespace reone::net;

namespace reone {

namespace game {

namespace {

/** Changes of direction smaller than this ride on the next keepalive. */
constexpr float kSteerFacingStep = 0.035f; // about 2 degrees
/** At most this often for a change; a held direction repeats this often. */
constexpr float kSteerMinInterval = 1.0f / 20.0f;
constexpr float kSteerKeepalive = 0.1f;
/** The prediction looks ahead by the round trip and one tick, never further than this. */
constexpr float kMaxPredictionLead = 0.15f;
constexpr float kMaxPredictionOffset = 1.0f;
constexpr float kPredictionEase = 0.08f; // seconds: how fast the drawn offset follows its target
constexpr float kNoControlNoticeEvery = 5.0f;

glm::vec2 steerDirection(float facing) {
    return glm::vec2(-portable::sinf(facing), portable::cosf(facing));
}

float angleBetween(float a, float b) {
    float d = std::fmod(std::fabs(a - b), 2.0f * glm::pi<float>());
    return d > glm::pi<float>() ? 2.0f * glm::pi<float>() - d : d;
}

} // namespace

const char *coopNoControlNotice(uint32_t controlled, bool hasActor) {
    if (hasActor) {
        return nullptr;
    }
    return controlled ? "Your character is on its way" : "You have no character to control yet";
}

// Host

void CoopSession::hostHandleSteer(HostClient &client, const Steer &steer) {
    ++client.steersReceived;
    uint64_t tick = _game.tickCount();
    if (client.steerSeen && steer.seq <= client.steer.seq) {
        // Updates travel unreliably: an older one arriving late is spent.
        ++client.steersDropped;
        ++client.steersStale;
        return;
    }
    // A stop always goes through; moving updates are held to the rate.
    if (steer.moving && !client.steerLimiter.allow(tick, _game.tickRate(), kMaxSteersPerSecond)) {
        ++client.steersDropped;
        ++client.steersRateLimited;
        return;
    }
    uint32_t controlled = hostControlledId(client);
    bool mine = controlled != 0 && steer.actor == controlled;
    if (!mine && controlled != 0 && controlled == client.standIn &&
        std::find(client.pastStandIns.begin(), client.pastStandIns.end(), steer.actor) != client.pastStandIns.end()) {
        // Still naming the stand-in a conversation took away: the same
        // player's stand-in, back under a new id.
        mine = true;
    }
    if (!mine || steer.actor != controlled) {
        // Tell the client again which creature is its own (at most twice a second).
        if (tick >= client.slotResendTick + static_cast<uint64_t>(_game.tickRate() / 2) || client.slotResendTick == 0) {
            client.slotResendTick = tick;
            hostSendSlot(client, true);
        }
    }
    if (!mine) {
        ++client.steersDropped;
        ++client.steersForeign;
        client.steer.seq = steer.seq;
        client.steer.moving = false;
        client.steerSeen = true;
        return;
    }
    client.steer = steer;
    client.steer.actor = controlled;
    client.steerSeen = true;
    if (steer.moving) {
        client.steerTick = tick;
    }
}

void CoopSession::hostApplySteering() {
    auto module = _game.module();
    if (!module || !module->area()) {
        return;
    }
    auto &area = *module->area();
    uint64_t tick = _game.tickCount();
    float dt = _game.tickSeconds();
    auto timeout = static_cast<uint64_t>(std::ceil(kSteerTimeoutSeconds * static_cast<float>(_game.tickRate())));
    for (auto &[connection, client] : _clients) {
        if (!client.welcomed || !client.steerSeen) {
            continue;
        }
        uint32_t controlled = hostControlledId(client);
        if (client.steer.moving && tick > client.steerTick + timeout) {
            // The updates stopped coming (a lost link, a frozen client): stop.
            client.steer.moving = false;
        }
        bool moved = false;
        if (client.steer.moving && controlled && client.steer.actor == controlled && !_game.isPaused()) {
            // The same checks a command gets: the player's own live creature,
            // in the area, with no conversation, cutscene or load running.
            Command probe;
            probe.type = CommandType::Cancel;
            probe.actor = controlled;
            CoopCommandTargets targets;
            bool own = controlled == client.standIn || controlled == client.guestId;
            auto reason = validateCoopCommand(_game, probe, controlled, targets, own, freeMode());
            if (reason == CommandReject::None && targets.actor && targets.actor->isMovementRestricted() &&
                !targets.actor->actions().empty() && !targets.actor->actions().front()->isUserAction() &&
                !targets.actor->isKnockedDown() && targets.actor->getHeldPoseAnimation().empty()) {
                // An attack the player did not order (the round's automatic
                // continuation, an idle engage) holds the creature mid-swing:
                // the player's keys win, as they would on its own machine.
                targets.actor->clearAllActions();
                targets.actor->setMovementRestricted(false);
                ++client.steerRefused["cancelled_auto_action"];
            }
            if (reason == CommandReject::None && targets.actor && !targets.actor->isMovementRestricted()) {
                // What the host's own keys do (Player::update): the creature's
                // own walk or run speed, so a client cannot ask for more.
                auto &actor = targets.actor;
                actor->noteDirectControl();
                actor->clearAllActions();
                actor->clearPath();
                area.moveCreature(actor, glm::normalize(steerDirection(client.steer.facing)), client.steer.run, dt, FLT_MAX);
                actor->setMovementType(client.steer.run ? Creature::MovementType::Run : Creature::MovementType::Walk);
                moved = true;
                ++client.steerTicks;
            } else if (reason != CommandReject::None) {
                ++client.steerRefused[commandRejectName(reason)];
                client.steer.moving = false;
            } else if (targets.actor) {
                auto &a = *targets.actor;
                a.noteDirectControl(); // held mid-swing: still the player's, not idle
                const char *why = a.isKnockedDown()                     ? "knocked_down"
                                  : !a.getHeldPoseAnimation().empty()   ? "held_pose"
                                  : !a.actions().empty()                ? "action_in_progress"
                                                                        : "restricted_or_stunned";
                std::string reasonText(why);
                if (!a.actions().empty()) {
                    reasonText += ":" + std::string(actionTypeName(a.actions().front()->type()));
                }
                if (client.steerRefused[reasonText] == 0) {
                    auto target = a.getAttackTarget();
                    info("Co-op: " + client.name + "'s steering waits on " + reasonText + (a.actions().front()->isUserAction() ? " (a player order)" : " (not a player order)") +
                         (target ? " at " + target->tag() : std::string()));
                }
                ++client.steerRefused[reasonText];
            }
        } else if (client.steer.moving) {
            ++client.steerRefused[!controlled ? "no_creature" : (_game.isPaused() ? "paused" : "stale_actor")];
        }
        if (!moved && client.steerApplied && controlled) {
            if (auto creature = _game.getObjectById<Creature>(controlled); creature && creature->actions().empty()) {
                creature->setMovementType(Creature::MovementType::None);
            }
        }
        client.steerApplied = moved;
    }
}

// Client

void CoopSession::clientSendSteer(bool moving, float facing, bool run, bool reliable) {
    if (!isClient() || _clientState != ClientState::Joined || !_hostConnection || !_transport) {
        return;
    }
    Steer steer;
    steer.seq = ++_steerSeq;
    steer.actor = controlledHostId();
    steer.moving = moving;
    steer.facing = std::remainder(facing, 2.0f * glm::pi<float>());
    steer.run = run;
    // Starts and stops go reliably; the updates in between are superseded
    // by the next one anyway (the host keeps the newest seq).
    _transport->send(_hostConnection, encode(steer), reliable ? SendMode::Reliable : SendMode::Unreliable);
    _steerFacing = facing;
    _steerRun = run;
    _steerTimer = 0.0f;
    ++_steerCommands;
}

void CoopSession::clientResolveControlled() {
    // The stand-in is this player's whatever id the host gave it this time
    // (a conversation takes it away; it comes back as a new object). Its
    // tag is the stable name: coop_standin_<client id>.
    if (_slot.status != SlotStatus::StandIn || !_welcome.clientId) {
        return;
    }
    if (_slot.creatureId && localObjectForHost(_slot.creatureId)) {
        return;
    }
    std::string tag = "coop_standin_" + std::to_string(_welcome.clientId);
    uint32_t found = 0;
    for (const auto &[id, replica] : _replicas) {
        if (replica.localId && replica.state.tag == tag && id > found && _game.getObjectById(replica.localId)) {
            found = id; // the newest one, should an old one still be on its way out
        }
    }
    if (found && found != _slot.creatureId) {
        info("Co-op: the stand-in is host id " + std::to_string(found) + " now (the slot said " + std::to_string(_slot.creatureId) + ")");
        _slot.creatureId = found;
        _lastSlotCreature = found;
        clientMirrorParty();
    }
}

void CoopSession::clientSteer(float dt) {
    // The movement keys (WASD, or the mouse-look walk) steer the player's
    // creature on the host directly, as the host's own keys steer its PC:
    // a direction and walk/run, then a stop. The client never moves the
    // creature in the world itself; it only draws a short prediction.
    _noControlNoticeTimer = std::max(0.0f, _noControlNoticeTimer - dt);
    auto module = _game.module();
    clientResolveControlled();
    uint32_t controlled = controlledHostId();
    auto actor = controlled ? localObjectForHost(controlled) : nullptr;
    float facing = 0.0f;
    bool walk = false;
    bool inGame = module && _moduleLoaded && _game.currentScreen() == Game::Screen::InGame && !_world.paused &&
                  _clientState == ClientState::Joined;
    bool keys = inGame && module->hasPlayer() && module->player().requestedMove(facing, walk);
    if (keys && !actor) {
        // Nothing to drive: say so rather than drop the input silently.
        ++_inputDropped;
        _inputDroppedWhy = controlled ? "not_arrived" : (_slot.status == SlotStatus::Waiting ? "waiting" : "no_slot");
        if (_noControlNoticeTimer <= 0.0f) {
            _noControlNoticeTimer = kNoControlNoticeEvery;
            showNotice(NoticeKind::Info, coopNoControlNotice(controlled, false));
        }
    }
    bool moving = keys && actor;
    if (!moving) {
        if (_steering) {
            _steering = false;
            clientSendSteer(false, _steerFacing, _steerRun, true);
        }
        _steerHeld = 0.0f;
        return;
    }
    bool run = !walk;
    _steerTimer += dt;
    _steerHeld += dt;
    bool start = !_steering;
    bool changed = run != _steerRun || angleBetween(facing, _steerFacing) > kSteerFacingStep;
    if (start || (changed && _steerTimer >= kSteerMinInterval) || _steerTimer >= kSteerKeepalive) {
        _steering = true;
        clientSendSteer(true, facing, run, start);
    }
}

void CoopSession::clientApplyOwnState() {
    uint32_t id = controlledHostId();
    if (id != _ownId) {
        _ownId = id;
        _ownTick = 0;
        _ownSpeed = 0.0f;
        _ownSpeedTick = 0;
    }
    if (!id || !_moduleLoaded || !_applied || _recon.empty()) {
        return;
    }
    // The newest state received, ahead of the jitter buffer: the player sees
    // its own creature as soon as the host's answer to its keys arrives.
    const auto &newest = _recon.rbegin()->second;
    uint32_t shown = std::max(_ownTick, _appliedTick);
    if (!newest || newest->tick <= shown) {
        return;
    }
    auto state = newest->entities.find(id);
    auto replica = _replicas.find(id);
    if (state == newest->entities.end() || replica == _replicas.end() || !replica->second.localId) {
        return;
    }
    uint16_t fields = exactDiffEntity(replica->second.state, state->second) & (kFieldPosition | kFieldFacing | kFieldAnim);
    if (fields) {
        EntityRecord record;
        record.fields = fields;
        record.state = state->second;
        record.state.id = id;
        clientApplyEntity(record);
    }
    _ownTick = newest->tick;
    ++_ownEarly;
}

void CoopSession::clientUpdatePrediction(float dt) {
    uint32_t id = controlledHostId();
    auto replica = id ? _replicas.find(id) : _replicas.end();
    auto creature = replica != _replicas.end() && replica->second.localId
                        ? _game.getObjectById<Creature>(replica->second.localId)
                        : nullptr;
    // How fast the host actually moves it, from the states shown.
    uint32_t shownTick = std::max(_ownTick, _appliedTick);
    if (creature && shownTick > _ownSpeedTick) {
        const auto &p = replica->second.state.position;
        if (_ownSpeedTick) {
            float seconds = static_cast<float>(shownTick - _ownSpeedTick) * _game.tickSeconds();
            float v = seconds > 0.0f ? glm::length(glm::vec2(p.x - _ownSpeedFrom.x, p.y - _ownSpeedFrom.y)) / seconds : 0.0f;
            _ownSpeed += (v - _ownSpeed) * 0.5f;
        }
        _ownSpeedFrom = p;
        _ownSpeedTick = shownTick;
    }
    glm::vec2 target(0.0f);
    if (creature && _steering) {
        float speed = _steerRun ? creature->runSpeed() : creature->walkSpeed();
        int ping = _transport && _hostConnection ? _transport->pingMs(_hostConnection) : -1;
        float lead = std::clamp((ping > 0 ? ping / 1000.0f : 0.0f) + _game.tickSeconds(), 0.0f, kMaxPredictionLead);
        // Blocked on the host (a wall, a door): nothing to run ahead of.
        bool blocked = _steerHeld > lead + 0.2f && _ownSpeed < 0.3f * speed;
        if (!blocked) {
            target = steerDirection(_steerFacing) * speed * lead;
        }
    }
    float k = std::min(1.0f, dt / kPredictionEase);
    _predictOffset += (target - _predictOffset) * k;
    float length = glm::length(_predictOffset);
    if (length > kMaxPredictionOffset) {
        _predictOffset *= kMaxPredictionOffset / length;
    }
    if (!creature || (!_steering && glm::length(_predictOffset) < 0.005f)) {
        _predictOffset = glm::vec2(0.0f);
    }
}

} // namespace game

} // namespace reone
