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

#include "reone/game/coopsession.h"

#include <algorithm>
#include <cstdio>

#include <boost/algorithm/string.hpp>

#include "reone/game/game.h"
#include "reone/game/gui/confirmpopup.h"
#include "reone/game/party.h"
#include "reone/resource/dialog.h"
#include "reone/resource/provider/dialogs.h"
#include "reone/resource/provider/scripts.h"
#include "reone/script/program.h"
#include "reone/script/routines.h"
#include "reone/system/logutil.h"

using namespace reone::net;

namespace reone {

namespace game {

// Minigames. Whoever starts a minigame plays it and the others watch (design, "Minigames").
// The host's game runs the simulation as it always has; a guest who drives it sends its input
// as MiniGameControl messages, and the host writes that into the one input seam in place of
// its own keys. A race a guest starts has its story effects undone when it ends (unless the
// host allows them), and pays the guest, not the party.

namespace {

// The "*mg" module an NPC's conversation starts, read from the compiled scripts' constants.
std::string miniGameModuleOf(Game &game, const std::string &dialogResRef) {
    if (dialogResRef.empty()) {
        return std::string();
    }
    try {
        auto dialog = game.services().resource.dialogs.get(boost::to_lower_copy(dialogResRef));
        if (!dialog) {
            return std::string();
        }
        int startNewModule = game.routines().getIndexByName("StartNewModule");
        for (const auto *nodes : {&dialog->entries, &dialog->replies}) {
            for (const auto &node : *nodes) {
                for (const auto *name : {&node.script, &node.script2}) {
                    if (name->empty()) {
                        continue;
                    }
                    auto program = game.services().resource.scripts.get(boost::to_lower_copy(*name));
                    if (!program) {
                        continue;
                    }
                    std::string module;
                    for (const auto &instruction : program->instructions()) {
                        if (instruction.type == script::InstructionType::CONSTS) {
                            auto value = boost::to_lower_copy(instruction.strValue);
                            if (value.size() > 2 && value.size() <= 16 && value.compare(value.size() - 2, 2, "mg") == 0) {
                                module = value;
                            }
                        } else if (instruction.type == script::InstructionType::ACTION && instruction.routine == startNewModule &&
                                   !module.empty()) {
                            return module;
                        }
                    }
                }
            }
        }
    } catch (const std::exception &) {
    }
    return std::string();
}

std::string timeText(float seconds) {
    int whole = static_cast<int>(seconds);
    char text[32];
    std::snprintf(text, sizeof(text), "%d:%04.1f", whole / 60, seconds - static_cast<float>(whole / 60 * 60));
    return text;
}

} // namespace

bool CoopSession::handMiniGame(uint32_t clientId, bool on) {
    if (!isHost() || !_miniGame.running()) {
        return false;
    }
    if (!on) {
        if (_miniGame.client() == clientId || clientId == 0) {
            _miniGame.handOver(0, std::string());
            info("Co-op: the host took the minigame back");
            return true;
        }
        return false;
    }
    for (auto &[connection, client] : _clients) {
        if (client.clientId != clientId || !client.welcomed) {
            continue;
        }
        _miniGame.handOver(client.clientId, client.name);
        hostNotice(client, NoticeKind::Info, "The host handed you the controls");
        info("Co-op: the host handed the minigame to " + client.name);
        return true;
    }
    return false;
}

void CoopSession::hostPromptMiniGameHandOver() {
    if (!showsPrompts() || _game.currentScreen() == Game::Screen::Conversation) {
        return;
    }
    if (!_game._confirmPopup) {
        _game._confirmPopup = _game.tryLoadGUI<ConfirmPopup>();
    }
    if (!_game._confirmPopup || _game._confirmPopup->isVisible()) {
        return;
    }
    std::vector<ConfirmPopup::Choice> choices;
    for (auto &[connection, client] : _clients) {
        if (client.welcomed && choices.size() < 3) {
            uint32_t clientId = client.clientId;
            choices.push_back({"Hand to " + client.name, [this, clientId]() { handMiniGame(clientId, true); }});
        }
    }
    if (choices.empty()) {
        return;
    }
    choices.push_back({"Keep", []() {}});
    _game._confirmPopup->showChoices("Hand the controls to another player?", std::move(choices));
}

CommandReject CoopSession::hostRequestGuestMiniGame(HostClient &client) {
    auto npc = _game.getObjectById(client.offer.target);
    auto module = npc ? miniGameModuleOf(_game, npc->conversation()) : std::string();
    client.offer = Offer();
    if (!_config.guestMinigames || module.empty()) {
        return CommandReject::GuestsCannotRace;
    }
    if (_miniGame.running() || !_pendingMiniGame.module.empty() || _game.miniGame() ||
        _game.currentScreen() != Game::Screen::InGame) {
        return CommandReject::MiniGameBusy;
    }
    _pendingMiniGame.client = client.clientId;
    _pendingMiniGame.module = module;
    // The story as it is now: the minigame's scripts run in the host's game, and a guest's race changes nothing in it.
    _pendingMiniGame.story = captureStory(_game);
    info("Co-op: " + client.name + " starts " + module + " (nothing it does counts for the story)");
    _game.scheduleModuleTransition(module, std::string());
    return CommandReject::None;
}

void CoopSession::onMiniGameStarted(MinigameType type, const std::string &module) {
    if (!isHost()) {
        return;
    }
    auto kind = type == MinigameType::Turret ? MiniGameKind::Turret : MiniGameKind::SwoopRace;
    auto pending = std::move(_pendingMiniGame);
    _pendingMiniGame = PendingMiniGame();
    for (auto &[connection, client] : _clients) {
        if (pending.client != 0 && client.clientId == pending.client && client.welcomed &&
            boost::iequals(pending.module, module)) {
            bool sandbox = !_config.guestMinigamesAffectStory;
            _miniGame.begin(kind, module, client.clientId, client.name, sandbox);
            if (sandbox) {
                _miniGame.saved() = std::move(pending.story);
            }
            hostNotice(client, NoticeKind::Info, "Your " + std::string(kind == MiniGameKind::Turret ? "turret run" : "race") + " starts");
            hostBroadcastMiniGame();
            return;
        }
    }
    // The host's own minigame (or the one a guest asked for is gone): the host plays, the others watch.
    _miniGame.begin(kind, module, 0, std::string(), false);
    if (kind == MiniGameKind::Turret) {
        hostPromptMiniGameHandOver();
    }
    hostBroadcastMiniGame();
}

void CoopSession::applyMiniGameInput(MiniGameInput &input) {
    if (isHost()) {
        _miniGame.apply(input, _game.tickCount());
    }
}

void CoopSession::hostHandleMiniGameControl(HostClient &client, const MiniGameControl &control) {
    auto verdict = _miniGame.submit(client.clientId, control, _game.tickCount());
    if (verdict == CoopMiniGame::Verdict::Accepted) {
        ++_miniGameControlsAccepted;
    }
    if (verdict == CoopMiniGame::Verdict::NotOwner || verdict == CoopMiniGame::Verdict::NotRunning) {
        // A late message from a player who has just let go, or one that was never allowed: ignored, counted.
        ++client.miniGameIgnored;
    }
}

CommandReject CoopSession::hostMiniGameCommand(HostClient &client, const Command &command, bool &handled) {
    handled = true;
    if (command.type != CommandType::MiniGameQuit) {
        handled = false;
        return CommandReject::None;
    }
    switch (_miniGame.quit(client.clientId)) {
    case CoopMiniGame::Verdict::Accepted:
        // The controls go back to the host, who finishes it (a minigame ends when its own scripts leave the module).
        info("Co-op: " + client.name + " let go of the minigame");
        return CommandReject::None;
    case CoopMiniGame::Verdict::NotOwner:
        return CommandReject::NotYourMiniGame;
    default:
        return CommandReject::NotYourMiniGame;
    }
}

void CoopSession::onMiniGameAnimation(size_t actor, const std::string &name, uint8_t flags) {
    if (!isHost() || !_miniGame.running() || name.empty() || name.size() > 32 || actor >= kMaxMiniGameActors) {
        return;
    }
    if (_miniGameAnimations.size() >= kMaxMiniGameAnimations) {
        ++_miniGameAnimationsDropped; // more in a tenth of a second than a client could see
        return;
    }
    _miniGameAnimations.push_back({static_cast<uint16_t>(actor), name, flags});
}

void CoopSession::clientPredictMiniGame(MiniGameState &sample) {
    if (!clientOwnsMiniGame()) {
        return;
    }
    if (!_miniGameReplay.empty()) {
        // What the newest state shows the host used is never needed again.
        uint32_t newest = _miniGameReplay.newest().controlSeq;
        if (newest > 0) {
            _miniGameAim.acknowledge(std::min(newest, sample.controlSeq));
        }
    }
    _miniGameAim.apply(sample);
}

void CoopSession::onMiniGameSound(const std::string &resRef) {
    if (!isHost() || !_miniGame.running() || resRef.empty() || resRef.size() > 16 || _miniGameSounds.size() >= kMaxMiniGameSounds) {
        return;
    }
    _miniGameSounds.push_back(resRef);
}

std::vector<std::string> CoopSession::takeMiniGameSounds() {
    std::vector<std::string> out;
    out.swap(_miniGameSounds);
    if (isClient()) {
        _miniGameSoundsHeard += out.size();
    }
    return out;
}

std::vector<MiniGameAnimation> CoopSession::takeMiniGameAnimations() {
    std::vector<MiniGameAnimation> out;
    out.swap(_miniGameAnimations);
    return out;
}

void CoopSession::hostSendMiniGame(const MiniGameState &state) {
    auto bytes = encode(state);
    for (auto &[connection, client] : _clients) {
        if (client.welcomed) {
            send(connection, bytes);
        }
    }
}

void CoopSession::hostBroadcastMiniGame() {
    auto session = _game.miniGame();
    if (!isHost() || !session || !_miniGame.running()) {
        return;
    }
    auto state = captureMiniGameState(*session, _miniGame.module(), _miniGame.owner(), &_game.miniGameInput());
    state.animations = takeMiniGameAnimations();
    state.sounds = takeMiniGameSounds();
    state.controlSeq = _miniGame.remoteOwned() ? _miniGame.lastSeq() : 0;
    hostSendMiniGame(state);
}

void CoopSession::onMiniGameEnded(const MiniGameSession &session) {
    if (!isHost() || !_miniGame.running()) {
        return;
    }
    auto player = session.player();
    auto outcome = _miniGame.end(session.tickCount(), session.elapsed(), player && player->dead, _config.guestRacePrize);
    auto state = captureMiniGameState(session, _miniGame.module(), outcome.owner);
    state.active = false;
    state.animations = takeMiniGameAnimations();
    state.sounds = takeMiniGameSounds();
    state.finished = outcome.finished;
    state.quit = outcome.quit;
    state.prize = outcome.prize;
    if (outcome.sandboxed) {
        // A guest's race leaves the story as it was.
        restoreStory(_game, _miniGame.saved());
    }
    for (auto &[connection, client] : _clients) {
        if (client.clientId != outcome.client || outcome.client == 0) {
            continue;
        }
        std::string text = outcome.quit ? "You left the minigame" : outcome.finished ? "You finished in " + timeText(outcome.elapsed) : "The run is over";
        if (outcome.prize > 0) {
            if (freeMode()) {
                // Free Mode: the guest's own credits.
                if (auto record = _guests.find(client.name); record != _guests.end()) {
                    record->second.credits = static_cast<int32_t>(std::min<int64_t>(
                        static_cast<int64_t>(record->second.credits) + outcome.prize, 10000000));
                }
            } else {
                // Story Mode: the shared credits.
                _game.party().giveGold(outcome.prize);
            }
            text += " and won " + std::to_string(outcome.prize) + " credits";
        }
        hostNotice(client, NoticeKind::Info, text);
        info("Co-op: " + client.name + "'s minigame ended: " + text);
    }
    hostSendMiniGame(state);
}

void CoopSession::clientHandleMiniGameState(const MiniGameState &state) {
    bool own = !state.owner.empty() && state.owner == _config.playerName;
    bool wasActive = _miniGameState.active && !_miniGameState.module.empty();
    _miniGameState = state;
    // What is drawn plays these back, a little behind the newest (Game::tickMiniGameReplica).
    _miniGameReplay.push(state);
    if (state.active && !wasActive) {
        _miniGameResult.clear();
        _miniGameAnimations.clear();
        _miniGameSounds.clear();
        _miniGameAim.reset();
    }
    for (const auto &sound : state.sounds) {
        if (_miniGameSounds.size() < 4 * kMaxMiniGameSounds) {
            _miniGameSounds.push_back(sound);
        }
    }
    // Played once the minigame is drawn here (its module may still be loading).
    for (const auto &animation : state.animations) {
        if (_miniGameAnimations.size() >= 4 * kMaxMiniGameAnimations) {
            ++_miniGameAnimationsDropped;
            break;
        }
        _miniGameAnimations.push_back(animation);
    }
    if (!state.active) {
        _miniGameControlSeq = 0;
        // The end, for everyone watching: the time or the run, and the prize.
        _miniGameResult = miniGameResultText(state, own);
        showNotice(NoticeKind::Info, _miniGameResult);
        if (_game.miniGameReplica()) {
            // The race (or the turret) stays on screen with its result a moment before the
            // module the host moves on to is loaded.
            _miniGameResultHold = kMiniGameResultHoldSeconds;
        }
        info("Co-op: " + _miniGameResult);
    }
}

bool CoopSession::clientOwnsMiniGame() const {
    return isClient() && _miniGameState.active && !_miniGameState.owner.empty() && _miniGameState.owner == _config.playerName;
}

void CoopSession::clientMiniGameInput(MiniGameInput &input, glm::vec2 rateStep) {
    if (!clientOwnsMiniGame()) {
        return;
    }
    auto control = miniGameControlFrom(input);
    if (clientSendMiniGameControl(control)) {
        control.seq = _miniGameControlSeq; // as sent
        _miniGameAim.sent(control, rateStep.x, rateStep.y);
        ++_miniGameControlsSent;
        // A one-shot aim is used by exactly one host tick: it has been sent.
        input.pitchDelta = 0.0f;
        input.yawDelta = 0.0f;
    }
}

boost::json::object CoopSession::miniGameJson() const {
    boost::json::object root;
    if (isHost()) {
        root["role"] = "host";
        root["running"] = _miniGame.running();
        root["remote"] = _miniGame.remoteOwned();
        root["owner"] = _miniGame.owner();
        root["client"] = _miniGame.client();
        root["module"] = _miniGame.module();
        root["kind"] = _miniGame.kind() == MiniGameKind::Turret ? "turret" : "swoop";
        root["controls"] = _miniGameControlsAccepted;
        root["animationsDropped"] = _miniGameAnimationsDropped;
        root["pending"] = _pendingMiniGame.module;
        root["gold"] = _game.party().gold();
        return root;
    }
    root["role"] = isClient() ? "client" : "none";
    const auto &s = _miniGameState;
    root["active"] = s.active && !s.module.empty();
    root["module"] = s.module;
    root["owner"] = s.owner;
    root["own"] = clientOwnsMiniGame();
    root["tick"] = s.tick;
    root["prize"] = s.prize;
    root["finished"] = s.finished;
    root["quit"] = s.quit;
    root["buffered"] = _miniGameReplay.buffered();
    root["clock"] = _miniGameReplay.clock();
    root["newest"] = _miniGameReplay.empty() ? 0u : _miniGameReplay.newest().tick;
    root["sent"] = _miniGameControlsSent;
    root["animationsQueued"] = _miniGameAnimations.size();
    root["aimPending"] = _miniGameAim.pending();
    root["soundsQueued"] = _miniGameSounds.size();
    root["soundsHeard"] = _miniGameSoundsHeard;
    root["resultHold"] = _miniGameResultHold;
    root["moduleHeld"] = _moduleHeldForResult;
    root["drawn"] = _game.miniGameReplica();
    root["result"] = _miniGameResult;
    return root;
}

bool CoopSession::clientSendMiniGameControl(MiniGameControl control) {
    if (!isClient() || !_miniGameState.active || _miniGameState.owner != _config.playerName || _miniGameState.owner.empty()) {
        return false;
    }
    auto clamp = [](float v, float limit) { return std::isfinite(v) ? std::clamp(v, -limit, limit) : 0.0f; };
    control.seq = ++_miniGameControlSeq;
    control.steer = clamp(control.steer, 1.0f);
    control.pitchRate = clamp(control.pitchRate, 1.0f);
    control.yawRate = clamp(control.yawRate, 1.0f);
    control.pitchDelta = clamp(control.pitchDelta, kMaxMiniGameDelta);
    control.yawDelta = clamp(control.yawDelta, kMaxMiniGameDelta);
    if (_clientState != ClientState::Joined || !_hostConnection) {
        return false;
    }
    send(_hostConnection, encode(control));
    return true;
}

bool CoopSession::clientQuitMiniGame() {
    if (!isClient() || !_miniGameState.active || _miniGameState.owner != _config.playerName || _miniGameState.owner.empty()) {
        return false;
    }
    submitRequest(CommandType::MiniGameQuit, 0, 0);
    return true;
}

} // namespace game

} // namespace reone
