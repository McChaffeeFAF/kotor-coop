/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "reone/net/protocol.h"

namespace reone {

namespace game {

/** Seconds a ready check waits before a majority is enough (design item 8). */
constexpr float kCoopReadyTimeout = 30.0f;

/**
 * The area-exit ready check (co-op design notes, "Area-exit ready check").
 *
 * The party moves when every player is ready, or once the timeout passes if
 * more than half are ready; the host can force it. Time is host simulation
 * time, so a gated test host sees the same outcome at the same tick.
 * Players are keyed: 0 is the host, client ids otherwise.
 */
class CoopReadyCheck {
public:
    enum class Outcome {
        Pending,
        Passed,
        Cancelled
    };

    struct Player {
        uint32_t key {0};
        std::string name;
        net::ReadyAnswer answer {net::ReadyAnswer::Pending};
        bool busy {false};
    };

    /** A new check; the initiator counts as ready. */
    void start(uint32_t id, uint32_t initiator, const std::string &destination, float now,
               std::vector<Player> players, float timeout = kCoopReadyTimeout);

    bool active() const { return _active; }
    uint32_t id() const { return _id; }
    const std::string &destination() const { return _destination; }
    const std::string &initiatorName() const { return _initiatorName; }
    const std::vector<Player> &players() const { return _players; }

    /** False when the player is not in this check. */
    bool answer(uint32_t key, bool ready);
    void setBusy(uint32_t key, bool busy);
    /** A player joined or dropped mid-check. */
    void addPlayer(Player player);
    void removePlayer(uint32_t key);
    /** The host moves the party now. */
    void force() { _forced = true; }

    /** Passed or Cancelled end the check (active() turns false). */
    Outcome evaluate(float now);

    uint16_t secondsLeft(float now) const;

    net::ReadyCheck message(float now) const;

    void reset() { *this = CoopReadyCheck(); }

private:
    bool _active {false};
    bool _forced {false};
    uint32_t _id {0};
    std::string _destination;
    std::string _initiatorName;
    float _deadline {0.0f};
    std::vector<Player> _players;
    net::ReadyState _lastState {net::ReadyState::Idle};

    Player *find(uint32_t key);
};

/** Enemy hit point and damage multipliers for a party of `players` (design, "Enemy scaling"). */
struct CoopEnemyScale {
    float hp {1.0f};
    float damage {1.0f};
};

/**
 * +50% HP and +5% damage for each of players 2-3, then +25% HP and +2.5%
 * damage for each of players 4-8 (8 players: x3.25 HP, x1.225 damage;
 * the design note's "x2.25" does not follow from its own steps).
 * `strength` is the host's adjustment: 1 is the design curve, 0 turns
 * scaling off, 2 doubles every step.
 */
CoopEnemyScale coopEnemyScale(int players, float strength = 1.0f);

/** A per-player cooldown between pauses; 0 seconds is no cooldown. */
class CoopPauseCooldown {
public:
    explicit CoopPauseCooldown(float seconds = 0.0f) :
        _seconds(seconds) {
    }

    bool mayPause(uint32_t key, float now) const;
    void recordPause(uint32_t key, float now);
    void setSeconds(float seconds) { _seconds = seconds; }

private:
    float _seconds {0.0f};
    std::vector<std::pair<uint32_t, float>> _last;
};

} // namespace game

} // namespace reone
