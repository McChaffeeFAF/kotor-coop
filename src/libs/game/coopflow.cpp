/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/coopflow.h"

#include <algorithm>
#include <cmath>

namespace reone {

namespace game {

// Ready check

void CoopReadyCheck::start(uint32_t id, uint32_t initiator, const std::string &destination, float now,
                           std::vector<Player> players, float timeout) {
    reset();
    _active = true;
    _id = id;
    _destination = destination;
    _deadline = now + timeout;
    _players = std::move(players);
    if (auto *player = find(initiator)) {
        player->answer = net::ReadyAnswer::Ready;
        _initiatorName = player->name;
    }
    _lastState = net::ReadyState::Active;
}

CoopReadyCheck::Player *CoopReadyCheck::find(uint32_t key) {
    auto it = std::find_if(_players.begin(), _players.end(), [key](const Player &player) {
        return player.key == key;
    });
    return it != _players.end() ? &*it : nullptr;
}

bool CoopReadyCheck::answer(uint32_t key, bool ready) {
    auto *player = _active ? find(key) : nullptr;
    if (!player) {
        return false;
    }
    player->answer = ready ? net::ReadyAnswer::Ready : net::ReadyAnswer::NotReady;
    return true;
}

void CoopReadyCheck::setBusy(uint32_t key, bool busy) {
    if (auto *player = find(key)) {
        player->busy = busy;
    }
}

void CoopReadyCheck::addPlayer(Player player) {
    if (!_active || find(player.key)) {
        return;
    }
    _players.push_back(std::move(player));
}

void CoopReadyCheck::removePlayer(uint32_t key) {
    _players.erase(std::remove_if(_players.begin(), _players.end(), [key](const Player &player) {
                       return player.key == key;
                   }),
                   _players.end());
}

CoopReadyCheck::Outcome CoopReadyCheck::evaluate(float now) {
    if (!_active) {
        return Outcome::Pending;
    }
    size_t ready = std::count_if(_players.begin(), _players.end(), [](const Player &player) {
        return player.answer == net::ReadyAnswer::Ready;
    });
    Outcome outcome = Outcome::Pending;
    if (_forced || (!_players.empty() && ready == _players.size())) {
        outcome = Outcome::Passed;
    } else if (now >= _deadline) {
        // Past the timeout a majority carries the vote; otherwise nobody moves.
        outcome = ready * 2 > _players.size() ? Outcome::Passed : Outcome::Cancelled;
    }
    if (outcome != Outcome::Pending) {
        _active = false;
        _lastState = outcome == Outcome::Passed ? net::ReadyState::Passed : net::ReadyState::Cancelled;
    }
    return outcome;
}

uint16_t CoopReadyCheck::secondsLeft(float now) const {
    if (!_active) {
        return 0;
    }
    return static_cast<uint16_t>(std::clamp(std::ceil(_deadline - now), 0.0f, 3600.0f));
}

net::ReadyCheck CoopReadyCheck::message(float now) const {
    net::ReadyCheck message;
    message.id = _id;
    message.state = _active ? net::ReadyState::Active : _lastState;
    message.initiator = _initiatorName.substr(0, net::kMaxStringBytes);
    message.destination = _destination.substr(0, net::kMaxStringBytes);
    message.secondsLeft = secondsLeft(now);
    for (const auto &player : _players) {
        if (message.players.size() >= net::kMaxReadyPlayers) {
            break;
        }
        message.players.push_back({player.name.substr(0, net::kMaxStringBytes), player.answer, player.busy});
    }
    return message;
}

// Enemy scaling

CoopEnemyScale coopEnemyScale(int players, float strength) {
    CoopEnemyScale scale;
    if (!(strength > 0.0f) || players <= 1) {
        return scale;
    }
    int players8 = std::min(players, 8);
    int early = std::min(players8, 3) - 1; // players 2-3
    int late = std::max(players8 - 3, 0);  // players 4-8
    // Owner, 2026-09-26: x2.0 at 3 players, x2.25 at 8 (+5% for each of players 4-8).
    scale.hp = 1.0f + strength * (0.5f * early + 0.05f * late);
    scale.damage = 1.0f + strength * (0.05f * early + 0.025f * late);
    return scale;
}

// Pause cooldown

bool CoopPauseCooldown::mayPause(uint32_t key, float now) const {
    if (!(_seconds > 0.0f)) {
        return true;
    }
    for (const auto &[who, when] : _last) {
        if (who == key) {
            return now - when >= _seconds;
        }
    }
    return true;
}

void CoopPauseCooldown::recordPause(uint32_t key, float now) {
    for (auto &[who, when] : _last) {
        if (who == key) {
            when = now;
            return;
        }
    }
    _last.emplace_back(key, now);
}

} // namespace game

} // namespace reone
