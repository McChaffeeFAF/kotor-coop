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

#include "reone/game/coopminigame.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/transform.hpp>

#include "reone/game/party.h"

namespace reone {

namespace game {

StoryState captureStory(Game &game) {
    StoryState state;
    state.strings = game.globalStrings();
    state.booleans = game.globalBooleans();
    state.numbers = game.globalNumbers();
    state.gold = game.party().gold();
    state.xp = game.party().xp();
    return state;
}

void restoreStory(Game &game, const StoryState &state) {
    for (const auto &[name, value] : game.globalStrings()) {
        auto it = state.strings.find(name);
        std::string wanted = it != state.strings.end() ? it->second : std::string();
        if (value != wanted) {
            game.setGlobalString(name, wanted);
        }
    }
    for (const auto &[name, value] : game.globalBooleans()) {
        auto it = state.booleans.find(name);
        bool wanted = it != state.booleans.end() && it->second;
        if (value != wanted) {
            game.setGlobalBoolean(name, wanted);
        }
    }
    for (const auto &[name, value] : game.globalNumbers()) {
        auto it = state.numbers.find(name);
        int wanted = it != state.numbers.end() ? it->second : 0;
        if (value != wanted) {
            game.setGlobalNumber(name, wanted);
        }
    }
    int gold = game.party().gold();
    if (gold > state.gold) {
        game.party().takeGold(gold - state.gold);
    } else if (gold < state.gold) {
        game.party().giveGold(state.gold - gold);
    }
    if (game.party().xp() != state.xp) {
        game.party().setXP(state.xp);
    }
}

void CoopMiniGame::begin(net::MiniGameKind kind, std::string module, uint32_t client, std::string owner, bool sandboxed) {
    *this = CoopMiniGame();
    _running = true;
    _kind = kind;
    _module = std::move(module);
    _client = client;
    _owner = std::move(owner);
    _sandboxed = sandboxed && client != 0;
}

void CoopMiniGame::handOver(uint32_t client, std::string owner) {
    if (!_running) {
        return;
    }
    _client = client;
    _owner = client ? std::move(owner) : std::string();
    _hasControl = false;
    _lastSeq = 0;
    _pendingPitch = _pendingYaw = 0.0f;
}

CoopMiniGame::Verdict CoopMiniGame::submit(uint32_t client, const net::MiniGameControl &control, uint64_t tick) {
    if (!remoteOwned()) {
        return Verdict::NotRunning;
    }
    if (client != _client) {
        return Verdict::NotOwner;
    }
    if (_hasControl && control.seq <= _lastSeq) {
        return Verdict::Stale;
    }
    _hasControl = true;
    _lastSeq = control.seq;
    _lastTick = tick;
    _control = control;
    // One-shot aim adds up until the next tick uses it; a flood cannot turn the turret more than half a turn.
    _pendingPitch = std::clamp(_pendingPitch + control.pitchDelta, -90.0f, 90.0f);
    _pendingYaw = std::clamp(_pendingYaw + control.yawDelta, -90.0f, 90.0f);
    return Verdict::Accepted;
}

CoopMiniGame::Verdict CoopMiniGame::quit(uint32_t client) {
    if (!remoteOwned()) {
        return Verdict::NotRunning;
    }
    if (client != _client) {
        return Verdict::NotOwner;
    }
    _quit = true;
    _client = 0;
    _owner.clear();
    _hasControl = false;
    _pendingPitch = _pendingYaw = 0.0f;
    return Verdict::Accepted;
}

void CoopMiniGame::apply(MiniGameInput &input, uint64_t tick) {
    if (!remoteOwned()) {
        return;
    }
    bool fresh = _hasControl && tick >= _lastTick && tick - _lastTick <= kMiniGameInputHoldTicks;
    input.steer = fresh ? _control.steer : 0.0f;
    input.fire = fresh && _control.fire;
    input.pitchRate = fresh ? _control.pitchRate : 0.0f;
    input.yawRate = fresh ? _control.yawRate : 0.0f;
    input.pitchDelta = _pendingPitch;
    input.yawDelta = _pendingYaw;
    _pendingPitch = 0.0f;
    _pendingYaw = 0.0f;
}

CoopMiniGame::Outcome CoopMiniGame::end(uint64_t ticks, float elapsed, bool playerDead, int racePrize) {
    Outcome outcome;
    outcome.client = _client;
    outcome.owner = _owner;
    outcome.kind = _kind;
    outcome.quit = _quit;
    outcome.finished = !_quit && !playerDead;
    outcome.elapsed = std::max(0.0f, elapsed);
    outcome.ticks = ticks;
    outcome.sandboxed = _sandboxed;
    outcome.prize = _client != 0 ? miniGamePrize(_kind, outcome.finished, outcome.quit, racePrize) : 0;
    _running = false;
    return outcome;
}

int miniGamePrize(net::MiniGameKind kind, bool finished, bool quit, int racePrize) {
    return kind == net::MiniGameKind::SwoopRace && finished && !quit ? std::max(0, racePrize) : 0;
}

net::MiniGameState captureMiniGameState(const MiniGameSession &session, const std::string &module, const std::string &owner,
                                        const MiniGameInput *input) {
    net::MiniGameState state;
    state.kind = session.type() == MinigameType::Turret ? net::MiniGameKind::Turret : net::MiniGameKind::SwoopRace;
    state.module = module.substr(0, 16);
    state.owner = owner.substr(0, 32);
    state.tick = static_cast<uint32_t>(std::min<uint64_t>(session.tickCount(), UINT32_MAX));
    state.elapsed = std::max(0.0f, session.elapsed());
    state.speed = session.playerSpeed();
    auto finite = [](float v) { return std::isfinite(v) ? std::clamp(v, -99999.0f, 99999.0f) : 0.0f; };
    state.offset = {finite(session.offset().x), finite(session.offset().y), finite(session.offset().z)};
    if (auto player = session.player()) {
        state.hp = static_cast<int16_t>(std::clamp(player->hitPoints, 0, 32767));
        state.maxHp = static_cast<int16_t>(std::clamp(player->maxHitPoints, 0, 32767));
        state.finished = player->dead;
    }
    for (const auto &actor : session.actors()) {
        if (state.actors.size() >= net::kMaxMiniGameActors) {
            break;
        }
        net::MiniGameActorState entry;
        entry.kind = actor.kind == MiniGameActorKind::Player     ? net::MiniGameRole::Player
                     : actor.kind == MiniGameActorKind::Obstacle ? net::MiniGameRole::Obstacle
                                                                 : net::MiniGameRole::Enemy;
        entry.hp = static_cast<int16_t>(std::clamp(actor.hitPoints, 0, 32767));
        entry.dead = actor.dead;
        entry.position = {finite(actor.position.x), finite(actor.position.y), finite(actor.position.z)};
        glm::quat rotation = glm::quat_cast(glm::mat3(actor.hook));
        float length = glm::length(rotation);
        if (std::isfinite(length) && length > 1e-4f) {
            rotation /= length;
            entry.rotation = {rotation.x, rotation.y, rotation.z, rotation.w};
        }
        state.actors.push_back(entry);
    }
    state.firing = input && input->fire && session.player() && !session.player()->dead;
    return state;
}

// Client playback

void MiniGameReplay::reset() {
    _states.clear();
    _clock = 0.0f;
    _clockSet = false;
}

void MiniGameReplay::push(const net::MiniGameState &state) {
    if (!_states.empty()) {
        const auto &last = _states.back();
        if (last.kind != state.kind || last.module != state.module || state.tick + kMiniGameTicksPerSecond < last.tick ||
            (!last.active && state.active)) {
            // Another minigame (or the same module started again): nothing before it applies.
            reset();
        } else if (state.tick < last.tick || (state.tick == last.tick && state.active == last.active)) {
            return;
        }
    }
    _states.push_back(state);
    // A second of states is far more than the clock ever needs.
    while (_states.size() > 2 && _states.size() > static_cast<size_t>(kMiniGameTicksPerSecond / kMiniGameStateEveryTicks) + 2) {
        _states.pop_front();
    }
}

void MiniGameReplay::advance(float dt) {
    if (_states.empty()) {
        return;
    }
    float newest = static_cast<float>(_states.back().tick);
    float oldest = static_cast<float>(_states.front().tick);
    float target = std::max(oldest, newest - kDelayTicks);
    if (!_clockSet || std::fabs(_clock - target) > kMiniGameTicksPerSecond) {
        // The first state, or a stall of a second or more: start at the target.
        _clock = target;
        _clockSet = true;
    } else {
        // Run at the host's rate, pulled a tenth of the way towards the target each step so
        // bunched or late updates neither jerk the picture nor let the delay grow.
        _clock += std::max(0.0f, dt) * kMiniGameTicksPerSecond;
        _clock += (target - _clock) * 0.1f;
    }
    _clock = std::clamp(_clock, oldest, newest);
    // Keep one state at or before the clock; older ones are done with.
    while (_states.size() > 2 && static_cast<float>(_states[1].tick) <= _clock) {
        _states.pop_front();
    }
}

net::MiniGameState MiniGameReplay::sample() const {
    if (_states.size() == 1 || _clock <= static_cast<float>(_states.front().tick)) {
        return _states.front();
    }
    for (size_t i = 0; i + 1 < _states.size(); ++i) {
        const auto &a = _states[i];
        const auto &b = _states[i + 1];
        if (_clock <= static_cast<float>(b.tick)) {
            float span = static_cast<float>(b.tick - a.tick);
            float t = span > 0.0f ? (_clock - static_cast<float>(a.tick)) / span : 1.0f;
            return blendMiniGameStates(a, b, t);
        }
    }
    return _states.back();
}

namespace {

glm::quat toQuat(const net::MiniGameRotation &r) {
    glm::quat q(r.w, r.x, r.y, r.z);
    float length = glm::length(q);
    return length > 1e-4f ? q / length : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

net::MiniGameRotation fromQuat(const glm::quat &q) {
    return {q.x, q.y, q.z, q.w};
}

net::Vec3 lerpVec(const net::Vec3 &a, const net::Vec3 &b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

// Degrees, the short way round.
float lerpAngle(float a, float b, float t) {
    float d = std::fmod(b - a, 360.0f);
    if (d > 180.0f) {
        d -= 360.0f;
    } else if (d < -180.0f) {
        d += 360.0f;
    }
    return a + d * t;
}

} // namespace

net::MiniGameState blendMiniGameStates(const net::MiniGameState &a, const net::MiniGameState &b, float t) {
    t = std::clamp(std::isfinite(t) ? t : 1.0f, 0.0f, 1.0f);
    net::MiniGameState out = t >= 1.0f ? b : a;
    if (t <= 0.0f || t >= 1.0f) {
        return out;
    }
    out.elapsed = a.elapsed + (b.elapsed - a.elapsed) * t;
    out.speed = a.speed + (b.speed - a.speed) * t;
    if (a.kind == net::MiniGameKind::Turret) {
        // The turret's aim in degrees: X pitch, Z yaw in [0, 360).
        out.offset.x = a.offset.x + (b.offset.x - a.offset.x) * t;
        out.offset.y = a.offset.y + (b.offset.y - a.offset.y) * t;
        out.offset.z = lerpAngle(a.offset.z, b.offset.z, t);
    } else {
        out.offset = lerpVec(a.offset, b.offset, t);
    }
    if (a.actors.size() == b.actors.size()) {
        for (size_t i = 0; i < out.actors.size(); ++i) {
            const auto &from = a.actors[i];
            const auto &to = b.actors[i];
            if (from.dead || to.dead) {
                continue; // a wreck stays where it died
            }
            out.actors[i].position = lerpVec(from.position, to.position, t);
            out.actors[i].rotation = fromQuat(glm::slerp(toQuat(from.rotation), toQuat(to.rotation), t));
        }
    }
    return out;
}

std::vector<size_t> applyMiniGameState(MiniGameSession &session, const net::MiniGameState &state) {
    std::vector<size_t> died;
    bool swoop = session.type() == MinigameType::SwoopRace;
    glm::vec3 offset(state.offset.x, state.offset.y, state.offset.z);
    // The swoop's offset from its track, or the turret's aim in degrees: the views read it from here.
    session.setPlayerAim(offset);
    session.setPlayerSpeed(state.speed);
    auto &actors = session.actors();
    size_t count = std::min(actors.size(), state.actors.size());
    for (size_t i = 0; i < count; ++i) {
        auto &actor = actors[i];
        const auto &entry = state.actors[i];
        glm::vec3 position(entry.position.x, entry.position.y, entry.position.z);
        glm::mat4 rotation = glm::mat4_cast(toQuat(entry.rotation));
        actor.drivenExternally = true; // nothing here samples a track any more
        actor.previousPosition = actor.position;
        actor.position = position;
        if (swoop && actor.kind == MiniGameActorKind::Player) {
            // The bike sits at its offset within the track frame: position = hook * offset.
            glm::vec3 origin = position - glm::vec3(rotation * glm::vec4(offset, 0.0f));
            actor.hook = glm::translate(origin) * rotation;
        } else {
            actor.hook = glm::translate(position) * rotation;
        }
        actor.hitPoints = entry.hp;
        if (entry.dead && !actor.dead) {
            actor.dead = true;
            died.push_back(i);
        }
    }
    if (auto player = session.player()) {
        player->hitPoints = state.hp;
        player->maxHitPoints = state.maxHp;
    }
    return died;
}

void MiniGameAimPrediction::sent(const net::MiniGameControl &control, float ratePitch, float rateYaw) {
    float pitch = control.pitchDelta + (std::isfinite(ratePitch) ? ratePitch : 0.0f);
    float yaw = control.yawDelta + (std::isfinite(rateYaw) ? rateYaw : 0.0f);
    if (pitch == 0.0f && yaw == 0.0f) {
        return;
    }
    _sent.push_back({control.seq, pitch, yaw});
    // Two seconds of inputs nobody acknowledged: the host is not using them.
    while (_sent.size() > 60) {
        _sent.pop_front();
    }
}

void MiniGameAimPrediction::acknowledge(uint32_t seq) {
    while (!_sent.empty() && _sent.front().seq <= seq) {
        _sent.pop_front();
    }
}

void MiniGameAimPrediction::apply(net::MiniGameState &state) const {
    if (state.kind != net::MiniGameKind::Turret) {
        return;
    }
    for (const auto &sent : _sent) {
        if (sent.seq > state.controlSeq) {
            state.offset.x += sent.pitch;
            state.offset.z += sent.yaw;
        }
    }
    state.offset.z = std::fmod(state.offset.z, 360.0f);
    if (state.offset.z < 0.0f) {
        state.offset.z += 360.0f;
    }
}

net::MiniGameControl miniGameControlFrom(const MiniGameInput &input) {
    auto clamp = [](float v, float limit) { return std::isfinite(v) ? std::clamp(v, -limit, limit) : 0.0f; };
    net::MiniGameControl control;
    control.steer = clamp(input.steer, 1.0f);
    control.fire = input.fire;
    control.pitchRate = clamp(input.pitchRate, 1.0f);
    control.yawRate = clamp(input.yawRate, 1.0f);
    control.pitchDelta = clamp(input.pitchDelta, net::kMaxMiniGameDelta);
    control.yawDelta = clamp(input.yawDelta, net::kMaxMiniGameDelta);
    return control;
}

std::string miniGameResultText(const net::MiniGameState &final, bool own) {
    int whole = static_cast<int>(final.elapsed);
    char time[32];
    std::snprintf(time, sizeof(time), "%d:%04.1f", whole / 60, final.elapsed - static_cast<float>(whole / 60 * 60));
    std::string who = own ? "Your" : (final.owner.empty() ? "The host's" : final.owner + "'s");
    if (final.kind == net::MiniGameKind::Turret) {
        int enemies = 0;
        int destroyed = 0;
        for (const auto &actor : final.actors) {
            if (actor.kind == net::MiniGameRole::Enemy) {
                ++enemies;
                destroyed += actor.dead ? 1 : 0;
            }
        }
        std::string text = who + " turret run is over: " + std::to_string(destroyed) + " of " + std::to_string(enemies) + " destroyed";
        if (final.quit) {
            text += " (left early)";
        } else if (final.hp <= 0 && final.maxHp > 0) {
            text += " (turret destroyed)";
        }
        return text;
    }
    std::string text = who + " race is over: ";
    if (final.quit) {
        text += "left early";
    } else if (!final.finished) {
        text += "the swoop was wrecked";
    } else {
        text += std::string("finished in ") + time;
    }
    if (final.prize > 0) {
        text += ", won " + std::to_string(final.prize) + " credits";
    }
    return text;
}

} // namespace game

} // namespace reone
