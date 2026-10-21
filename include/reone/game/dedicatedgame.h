/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <functional>

#include "dedicated.h"

namespace reone {

namespace game {

class Game;

/**
 * IDedicatedBackend over a real Game. Console commands (newgame, warp) run
 * through `runCommand`, the engine's console, so they take the same path
 * a harness script does.
 */
class DedicatedGameBackend : public IDedicatedBackend {
public:
    using CommandRunner = std::function<bool(const std::string &)>;

    DedicatedGameBackend(Game &game, CommandRunner runCommand) :
        _game(game), _runCommand(std::move(runCommand)) {
    }

    bool startHost(const DedicatedSettings &settings, std::string &error) override;
    bool saveExists(uint32_t slot) override;
    bool loadSave(uint32_t slot, std::string &error) override;
    bool startNewGame(const std::string &module, std::string &error) override;
    bool worldReady() override;
    SaveOutcome requestSave(uint32_t slot, const std::string &name) override;
    bool saveBusy() override;
    bool lastSaveSucceeded() override;
    std::vector<DedicatedPlayer> players() override;
    bool kick(uint32_t id) override;
    uint32_t storyDriver() override;
    std::string currentModule() override;
    void stopHost() override;

private:
    Game &_game;
    CommandRunner _runCommand;
    std::string _pendingWarp;
};

} // namespace game

} // namespace reone
