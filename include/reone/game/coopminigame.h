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

#pragma once

#include <deque>
#include <map>
#include <string>
#include <vector>

#include "reone/net/protocol.h"

#include "game.h"
#include "minigame.h"

namespace reone {

namespace game {

/** Inputs older than this many ticks (half a second) stop counting: a stalled player lets go of the controls. */
constexpr uint64_t kMiniGameInputHoldTicks = 15;

/** A co-op client keeps a finished minigame and its result on screen this long before following the host. */
constexpr float kMiniGameResultHoldSeconds = 2.5f;

/** The host sends the running minigame about ten times a second (every third tick at 30 ticks a second). */
constexpr int kMiniGameStateEveryTicks = 3;

/** The story state a minigame's own scripts can change: globals, the party's credits and XP. */
struct StoryState {
    std::map<std::string, std::string, Game::GVCompare> strings;
    std::map<std::string, bool, Game::GVCompare> booleans;
    std::map<std::string, int, Game::GVCompare> numbers;
    int gold {0};
    int xp {0};
};

StoryState captureStory(Game &game);

/**
 * Put the story state back as captured. A global that did not exist then is reset to false,
 * 0 or empty, which reads the same as one that was never set.
 */
void restoreStory(Game &game, const StoryState &state);

/**
 * Who drives the running minigame, and how a remote player's input reaches it (design,
 * "Minigames": whoever starts a minigame plays it, the others watch; a guest's race never
 * changes the story). The host simulates everything; this only decides whose input the
 * simulation reads and what the end of the minigame means.
 *
 * Inputs from the owner are validated by their decoder (finite, in range); here they are
 * checked for the right owner and for order, held for a short time, and written to the single
 * input seam (MiniGameInput) once per tick, so a run is a function of the owner's inputs and
 * the tick they were applied on.
 */
class CoopMiniGame {
public:
    enum class Verdict {
        Accepted,
        NotRunning, ///< no minigame, or the host's own: nobody remote to listen to
        NotOwner,   ///< another player drives it
        Stale       ///< older than one already used
    };

    struct Outcome {
        uint32_t client {0};  ///< the owner's client id
        std::string owner;
        net::MiniGameKind kind {net::MiniGameKind::SwoopRace};
        bool finished {false}; ///< it ran to its own end with the player alive
        bool quit {false};
        float elapsed {0.0f};
        uint64_t ticks {0};
        int prize {0};
        bool sandboxed {false};
    };

    bool running() const { return _running; }
    /** A remote player drives it: the host's own input is ignored. */
    bool remoteOwned() const { return _running && _client != 0; }
    uint32_t client() const { return _client; }
    const std::string &owner() const { return _owner; }
    const std::string &module() const { return _module; }
    net::MiniGameKind kind() const { return _kind; }
    bool sandboxed() const { return _sandboxed; }
    /** The newest owner input accepted (0 when none since the hand-over): its aim is in the state sent next. */
    uint32_t lastSeq() const { return _hasControl ? _lastSeq : 0; }

    /**
     * A minigame starts. `client` is the remote player who drives it, 0 for the host. A
     * sandboxed one (a guest's own race) has its story effects undone when it ends.
     */
    void begin(net::MiniGameKind kind, std::string module, uint32_t client, std::string owner, bool sandboxed);

    /** Give the running minigame to a remote player (the turret hand-over), or back to the host with 0. */
    void handOver(uint32_t client, std::string owner);

    Verdict submit(uint32_t client, const net::MiniGameControl &control, uint64_t tick);

    /** The owner lets go: the controls go back to the host, who plays the minigame out. */
    Verdict quit(uint32_t client);

    /** Write the owner's input for this tick into the seam; does nothing for the host's own minigame. */
    void apply(MiniGameInput &input, uint64_t tick);

    /**
     * It ended. `playerDead` is whether the player's actor was destroyed; `racePrize` the
     * credits a finished swoop race pays.
     */
    Outcome end(uint64_t ticks, float elapsed, bool playerDead, int racePrize);

    /** The sandbox's saved state, taken when a sandboxed minigame begins. */
    StoryState &saved() { return _saved; }

private:
    bool _running {false};
    net::MiniGameKind _kind {net::MiniGameKind::SwoopRace};
    std::string _module;
    uint32_t _client {0};
    std::string _owner;
    bool _sandboxed {false};
    bool _quit {false};
    StoryState _saved;

    bool _hasControl {false};
    uint32_t _lastSeq {0};
    uint64_t _lastTick {0};
    net::MiniGameControl _control;
    float _pendingPitch {0.0f};
    float _pendingYaw {0.0f};
};

/**
 * What a client needs to draw the running minigame, from the simulation: every actor's place
 * and track frame, and (with \p input) whether the player's fire is held, for the shots.
 */
net::MiniGameState captureMiniGameState(const MiniGameSession &session, const std::string &module, const std::string &owner,
                                        const MiniGameInput *input = nullptr);

/** The credits a minigame paid: a swoop race the player finished alive pays the prize, nothing else does. */
int miniGamePrize(net::MiniGameKind kind, bool finished, bool quit, int racePrize);

/** The host's simulation rate: MiniGameState ticks are thirtieths of a second. */
constexpr float kMiniGameTicksPerSecond = 30.0f;

/**
 * Client: the host's minigame states, played back a little behind the newest so that what is
 * drawn moves smoothly between updates that arrive about ten times a second. The clock runs in
 * host ticks; it never runs ahead of the newest state (nothing is extrapolated) and is pulled
 * back towards its target delay when the network bunches or stalls the updates.
 */
class MiniGameReplay {
public:
    /** How far behind the newest state the clock aims to be: one and a half update intervals. */
    static constexpr float kDelayTicks = 1.5f * kMiniGameStateEveryTicks;

    void reset();

    /** A state from the host. An older or repeated one is dropped; another minigame starts over. */
    void push(const net::MiniGameState &state);

    /** Advance the clock by \p dt seconds of the client's time. */
    void advance(float dt);

    bool empty() const { return _states.empty(); }
    float clock() const { return _clock; }
    size_t buffered() const { return _states.size(); }
    const net::MiniGameState &newest() const { return _states.back(); }

    /** The state at the clock, blended between the two around it. Only call when not empty. */
    net::MiniGameState sample() const;

private:
    std::deque<net::MiniGameState> _states;
    float _clock {0.0f};
    bool _clockSet {false};
};

/**
 * The state \p t of the way from \p a to \p b (0..1): positions, rotations, speed, time and the
 * turret's aim blended (the aim's yaw the short way round), everything else from \p a until
 * \p t reaches 1. Actor lists of different lengths are not blended.
 */
net::MiniGameState blendMiniGameStates(const net::MiniGameState &a, const net::MiniGameState &b, float t);

/**
 * Client: put a session's actors where the host's state says, with nothing simulated and no
 * script run (the host is authoritative). The swoop's offset or the turret's aim, speed and hit
 * points follow too. Returns the indices of the actors that died since the last call, for the
 * view to play their deaths.
 */
std::vector<size_t> applyMiniGameState(MiniGameSession &session, const net::MiniGameState &state);

/**
 * Client: the owner's turret aim, ahead of the host's state by the one-shot aim it has sent and the
 * host has not used yet (mouse aim would otherwise lag by the playback delay and the trip to the host).
 */
class MiniGameAimPrediction {
public:
    void reset() { _sent.clear(); }
    /** A control just sent, and the keyboard aim (degrees) it gives the host's turret this tick. */
    void sent(const net::MiniGameControl &control, float ratePitch = 0.0f, float rateYaw = 0.0f);
    /** Move \p state's aim (offset X pitch, Z yaw, degrees) by what the host had not used when it was captured. */
    void apply(net::MiniGameState &state) const;
    /** The host has used everything up to \p seq. */
    void acknowledge(uint32_t seq);
    size_t pending() const { return _sent.size(); }

private:
    struct Sent {
        uint32_t seq {0};
        float pitch {0.0f};
        float yaw {0.0f};
    };
    std::deque<Sent> _sent;
};

/** The message an owner sends for its local input: rates and fire as they are, aim deltas clamped. */
net::MiniGameControl miniGameControlFrom(const MiniGameInput &input);

/** The line a client shows when its minigame ends: time, prize, or how the run went. */
std::string miniGameResultText(const net::MiniGameState &final, bool own);

} // namespace game

} // namespace reone
