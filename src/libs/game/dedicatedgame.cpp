/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/dedicatedgame.h"

#include "reone/game/coopsession.h"
#include "reone/game/game.h"
#include "reone/game/object/module.h"
#include "reone/game/savedgame.h"
#include "reone/game/savegame.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

bool DedicatedGameBackend::startHost(const DedicatedSettings &settings, std::string &error) {
    if (_game.coop()) {
        error = "a co-op session is already running";
        return false;
    }
    CoopLaunch launch = _game.coopDefaults();
    launch.role = CoopRole::Host;
    launch.mode = "free";
    launch.port = settings.port;
    launch.service = settings.server;
    launch.dedicated = true;
    launch.maxPlayers = settings.maxPlayers;
    launch.password = settings.password;
    launch.askToJoin = 0; // nobody at the host to press Accept
    if (launch.playerName.empty()) {
        launch.playerName = "Server";
    }
    return startCoopSession(_game, launch, error) != nullptr;
}

bool DedicatedGameBackend::saveExists(uint32_t slot) {
    for (const auto &save : _game.savedGames()) {
        if (save.slot == slot) {
            return true;
        }
    }
    return false;
}

bool DedicatedGameBackend::loadSave(uint32_t slot, std::string &error) {
    for (const auto &save : _game.savedGames()) {
        if (save.slot == slot) {
            if (!_game.loadGame(save.descriptor)) {
                error = "the save could not be restored (see the log)";
                return false;
            }
            return true;
        }
    }
    error = "no such save";
    return false;
}

bool DedicatedGameBackend::startNewGame(const std::string &module, std::string &error) {
    try {
        // The same quick start a harness uses: class slot 2, no character screens.
        if (!_runCommand("newgame 2 Server")) {
            error = "the newgame command failed (see the log)";
            return false;
        }
    } catch (const std::exception &ex) {
        error = ex.what();
        return false;
    }
    _pendingWarp = module;
    return true;
}

bool DedicatedGameBackend::worldReady() {
    if (_game.saveEligibility(false) != SaveEligibilityReason::None || _game.currentScreen() != Game::Screen::InGame) {
        return false;
    }
    if (!_pendingWarp.empty()) {
        auto module = _pendingWarp;
        _pendingWarp.clear();
        info("Dedicated host: warping to " + module);
        _runCommand("warp " + module);
        return false; // the warp loads over the next frames
    }
    return true;
}

SaveOutcome DedicatedGameBackend::requestSave(uint32_t slot, const std::string &name) {
    auto result = _game.requestManualSave(slot, name);
    switch (result.status) {
    case SaveStatus::Accepted:
        return SaveOutcome::Accepted;
    case SaveStatus::Busy:
        return SaveOutcome::Busy;
    default:
        return SaveOutcome::NotAllowed;
    }
}

bool DedicatedGameBackend::saveBusy() {
    return _game.isSaveBusy();
}

bool DedicatedGameBackend::lastSaveSucceeded() {
    const auto &result = _game.lastSaveResult();
    return result && (result->status == SaveStatus::DurableSuccess || result->status == SaveStatus::DurableSuccessCleanupPending);
}

std::vector<DedicatedPlayer> DedicatedGameBackend::players() {
    std::vector<DedicatedPlayer> list;
    if (auto *coop = _game.coop()) {
        for (const auto &summary : coop->players()) {
            DedicatedPlayer player;
            player.id = summary.clientId;
            player.name = summary.name;
            player.companion = summary.companion;
            player.waiting = summary.waiting;
            list.push_back(std::move(player));
        }
    }
    return list;
}

bool DedicatedGameBackend::kick(uint32_t id) {
    auto *coop = _game.coop();
    return coop && coop->kick(id);
}

uint32_t DedicatedGameBackend::storyDriver() {
    auto *coop = _game.coop();
    return coop ? coop->storyDriver() : 0;
}

std::string DedicatedGameBackend::currentModule() {
    auto module = _game.module();
    return module ? module->name() : std::string();
}

void DedicatedGameBackend::stopHost() {
    _game.endCoopSession();
}

} // namespace game

} // namespace reone
