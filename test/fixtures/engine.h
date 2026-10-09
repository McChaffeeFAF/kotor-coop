/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#include "reone/system/logger.h"

#include "audio.h"
#include "game.h"
#include "graphics.h"
#include "gui.h"
#include "movie.h"
#include "resource.h"
#include "scene.h"
#include "script.h"
#include "system.h"

namespace reone {

class TestEngine : boost::noncopyable {
public:
    void init() {
        ++_generation;
        static bool loggerInitialized = false;
        if (!loggerInitialized) {
            Logger::instance.init(LogSeverity::Error, {}, std::nullopt);
            loggerInitialized = true;
        }

        _gameModule = std::make_unique<game::TestGameModule>();
        _movieModule = std::make_unique<movie::TestMovieModule>();
        _audioModule = std::make_unique<audio::TestAudioModule>();
        _graphicsModule = std::make_unique<graphics::TestGraphicsModule>();
        _sceneModule = std::make_unique<scene::TestSceneModule>();
        _guiModule = std::make_unique<gui::TestGUIModule>();
        _scriptModule = std::make_unique<script::TestScriptModule>();
        _resourceModule = std::make_unique<resource::TestResourceModule>();
        _systemModule = std::make_unique<TestSystemModule>();

        _gameModule->init();
        _movieModule->init();
        _audioModule->init();
        _graphicsModule->init();
        _sceneModule->init();
        _guiModule->init();
        _scriptModule->init();
        _resourceModule->init();
        _systemModule->init();

        // Scene graphs are looked up by name all over the game code. A test that
        // does not care which graph it gets uses this one; it lives as long as
        // the mocks that hand it out (a test's own ON_CALL still overrides it).
        _defaultSceneGraph = std::make_unique<testing::NiceMock<scene::MockSceneGraph>>();
        ON_CALL(_sceneModule->graphs(), get(testing::_)).WillByDefault(testing::ReturnRef(*_defaultSceneGraph));

        _services = std::make_unique<game::ServicesView>(
            _gameModule->services(),
            _movieModule->services(),
            _audioModule->services(),
            _graphicsModule->services(),
            _sceneModule->services(),
            _guiModule->services(),
            _scriptModule->services(),
            _resourceModule->services(),
            _systemModule->services());
    }

    /** Fresh mocks, services and options, as a newly constructed engine has after init(). */
    void reset() {
        _gameOpt = makeTestGameOptions();
        _graphicsOpt = graphics::GraphicsOptions();
        _audioOpt = audio::AudioOptions();
        init();
    }

    /** Counts init() calls: tells a fixture that cached something on the mocks they were reset. */
    unsigned generation() const { return _generation; }

    game::OptionsView &options() {
        return _options;
    }

    game::TestGameModule &gameModule() {
        return *_gameModule;
    }

    movie::TestMovieModule &movieModule() {
        return *_movieModule;
    }

    audio::TestAudioModule &audioModule() {
        return *_audioModule;
    }

    graphics::TestGraphicsModule &graphicsModule() {
        return *_graphicsModule;
    }

    scene::TestSceneModule &sceneModule() {
        return *_sceneModule;
    }

    gui::TestGUIModule &guiModule() {
        return *_guiModule;
    }

    script::TestScriptModule &scriptModule() {
        return *_scriptModule;
    }

    resource::TestResourceModule &resourceModule() {
        return *_resourceModule;
    }

    TestSystemModule &systemModule() {
        return *_systemModule;
    }

    game::GameOptions &gameOptions() {
        return _gameOpt;
    }

    game::ServicesView &services() {
        return *_services;
    }

private:
    unsigned _generation {0};

    // Unit tests drive Game::update frame by frame and expect each call to
    // simulate exactly that frame, so they run the game in variable-step mode.
    game::GameOptions _gameOpt {makeTestGameOptions()};

    static game::GameOptions makeTestGameOptions() {
        game::GameOptions options;
        options.variableStep = true;
        return options;
    }
    graphics::GraphicsOptions _graphicsOpt;
    audio::AudioOptions _audioOpt;
    game::OptionsView _options {_gameOpt, _graphicsOpt, _audioOpt};

    std::unique_ptr<game::TestGameModule> _gameModule;
    std::unique_ptr<movie::TestMovieModule> _movieModule;
    std::unique_ptr<audio::TestAudioModule> _audioModule;
    std::unique_ptr<graphics::TestGraphicsModule> _graphicsModule;
    std::unique_ptr<scene::TestSceneModule> _sceneModule;
    std::unique_ptr<gui::TestGUIModule> _guiModule;
    std::unique_ptr<script::TestScriptModule> _scriptModule;
    std::unique_ptr<resource::TestResourceModule> _resourceModule;
    std::unique_ptr<TestSystemModule> _systemModule;

    std::unique_ptr<testing::NiceMock<scene::MockSceneGraph>> _defaultSceneGraph;
    std::unique_ptr<game::ServicesView> _services;
};

// TestEngine initializes the Logger singleton, which only tolerates a single
// init per process.
//
// The shared engine is reset after every test (a gtest listener registered by
// engine.cpp), so expectations, default actions and options one test sets on
// it cannot reach the next one: the suite does not depend on test order.
TestEngine &testEngine();

} // namespace reone
