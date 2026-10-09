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

#include "engine.h"

#include "SDL3/SDL.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"
#include "imgui.h"

#include "reone/game/coopsession.h"
#include "reone/graphics/format/tgawriter.h"
#include "reone/graphics/gputimer.h"
#include "reone/graphics/preset.h"
#include "reone/graphics/window.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/gameprobe.h"
#include "reone/resource/presentationresources.h"
#include "reone/system/frameperf.h"
#include "reone/system/renderdiag.h"
#include "reone/system/stream/fileoutput.h"
#include "reone/system/stringutil.h"

#include <iomanip>
#include <set>
#include <sstream>

using namespace reone::audio;
using namespace reone::game;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::movie;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

static const std::string kMainThreadName {"main"};

static constexpr int kProfilerInputTimeIndex = 0;
static constexpr int kProfilerUpdateTimeIndex = 1;
static constexpr int kProfilerRenderGraphicsTimeIndex = 2;
static constexpr int kProfilerRenderAudioTimeIndex = 3;

static void imguiInit() {
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::GetStyle().FontScaleMain = 1.5f;
}

static void imguiInitWindow(Window &w) {
    ImGui_ImplSDL3_InitForOpenGL(w.sdlWindow(), w.sdlContext());
    ImGui_ImplOpenGL3_Init();
}

static bool imguiHandle(SDL_Event &ev) {
    ImGuiIO &io = ImGui::GetIO();
    ImGui_ImplSDL3_ProcessEvent(&ev);
    return io.WantCaptureMouse || io.WantCaptureKeyboard;
}

static void imguiNewFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if (!ImGui::GetIO().WantCaptureMouse) {
        // Switch to software cursor when it leaves ImGui windows.
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }
}

static void imguiRender() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

static void imguiShutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void Engine::init() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error("SDL_Init failed: " + std::string(SDL_GetError()));
    }

    if (_options.graphics.headless) {
        _options.graphics.winScale = 100;
        _options.audio.muted = true;
    }
    _window = std::make_unique<Window>(_options.graphics);
    _window->init();

    imguiInit();
    imguiInitWindow(*_window);

    _optionsView = _options.toView();
    GameProbe probe {_options.game.path};
    auto gameId = probe.probe();

    _clock = std::make_unique<Clock>();
    _clock->init();

    _systemModule = std::make_unique<SystemModule>(*_clock);
    _graphicsModule = std::make_unique<GraphicsModule>(_options.graphics);
    _audioModule = std::make_unique<AudioModule>(_options.audio);
    _movieModule = std::make_unique<MovieModule>();
    _scriptModule = std::make_unique<ScriptModule>();
    _resourceModule = std::make_unique<ResourceModule>(
        gameId,
        _options.game.path,
        _options.graphics,
        _options.audio,
        *_graphicsModule,
        *_audioModule,
        *_scriptModule);
    configureHudPresentation(gameId);
    configureK2Merge(gameId);
    _sceneModule = std::make_unique<SceneModule>(
        _options.graphics,
        *_resourceModule,
        *_graphicsModule,
        *_audioModule);
    _guiModule = std::make_unique<GUIModule>(
        _options.graphics,
        *_sceneModule,
        *_graphicsModule,
        *_resourceModule);
    _gameModule = std::make_unique<GameModule>(
        gameId,
        *_optionsView,
        *_resourceModule,
        *_graphicsModule,
        *_audioModule,
        *_sceneModule,
        *_scriptModule);
    _systemModule->init();
    _graphicsModule->init();
    _audioModule->init();
    _movieModule->init();
    _scriptModule->init();
    _resourceModule->init();
    _sceneModule->init();
    _guiModule->init();
    _gameModule->init();

    _services = std::make_unique<ServicesView>(
        _gameModule->services(),
        _movieModule->services(),
        _audioModule->services(),
        _graphicsModule->services(),
        _sceneModule->services(),
        _guiModule->services(),
        _scriptModule->services(),
        _resourceModule->services(),
        _systemModule->services());

    _profiler = std::make_unique<Profiler>(
        _options.graphics,
        _services->graphics,
        _services->resource,
        _services->system);
    _profiler->init();
    _profiler->reserveThread(
        kMainThreadName,
        {glm::vec3 {0.0f, 1.0f, 1.0f},
         glm::vec3 {0.0f, 1.0f, 0.0f},
         glm::vec3 {1.0f, 0.0f, 0.0f},
         glm::vec3 {1.0f, 1.0f, 0.0f}});

    _console = std::make_unique<Console>(
        _options.graphics,
        _services->graphics,
        _services->resource);
    _console->init();
    initAutomationCommands();

    _pointer = std::make_shared<PresentationPointer>(_services->resource.cursors);
    _game = std::make_unique<Game>(
        gameId,
        _options.game.path,
        *_optionsView,
        *_services,
        *_console,
        _pointer);
    _game->init();

    if (_options.controlPort > 0) {
        _channel = std::make_unique<ControlChannel>();
        _channel->start(_options.controlPort);
    }

    // Co-op networking stays off unless asked for: --host / --join here, or
    // Host / Join on the Co-op screen. What sessions share is set up now.
    CoopLaunch defaults;
    defaults.port = static_cast<uint16_t>(_options.coop.hostPort);
    defaults.mode = _options.coop.mode;
    defaults.playerName = _options.coop.name;
    defaults.preferredTag = _options.coop.slot;
    defaults.hostOnlyTransitions = _options.coop.hostOnlyTransitions;
    defaults.hostOnlyTrading = _options.coop.hostOnlyTrading;
    defaults.noPause = _options.coop.noPause;
    defaults.pauseCooldown = _options.coop.pauseCooldown;
    defaults.enemyScaling = _options.coop.enemyScaling;
    defaults.gamePath = _options.game.path;
    defaults.k2Path = _options.game.k2Path;
    defaults.executablePath = _options.executablePath;
    defaults.service = _options.coop.server;
    defaults.relayOnly = _options.coop.relayOnly;
    defaults.askToJoin = _options.coop.askJoin;
    _game->setCoopDefaults(defaults);
    if (!_options.coop.recordPath.empty()) {
        _game->setCoopRecordPath(_options.coop.recordPath);
    }
    if (_options.coop.simLagMs || _options.coop.simLossPercent > 0.0f || _options.coop.simJitterMs) {
        net::NetworkSimulation simulation;
        simulation.lagMs = _options.coop.simLagMs;
        simulation.lossPercent = _options.coop.simLossPercent;
        simulation.jitterMs = _options.coop.simJitterMs;
        net::setNetworkSimulation(simulation);
    }
    bool joining = !_options.coop.joinAddress.empty() || !_options.coop.joinCode.empty();
    if (_options.coop.hostPort > 0 || joining) {
        CoopLaunch launch = defaults;
        launch.role = _options.coop.hostPort > 0 ? CoopRole::Host : CoopRole::Client;
        launch.address = _options.coop.joinAddress;
        launch.code = _options.coop.joinCode;
        if (launch.role == CoopRole::Client) {
            launch.port = 0;
        }
        std::string error;
        if (!startCoopSession(*_game, launch, error)) {
            throw std::runtime_error("co-op: " + error);
        }
    }
}

/**
 * A K2 HUD in a K1 session reads K2's GUI resources from a second install.
 * Asking for it without a usable install is an error at startup, not a HUD
 * that half-loads with missing art.
 */
void Engine::configureHudPresentation(resource::GameID gameId) {
    auto hud = _options.game.hud;
    if (hud == game::HudProfile::K1 && gameId == resource::GameID::TSL) {
        throw std::runtime_error("--hud k1 is not supported in a K2 session");
    }
    if (hud != game::HudProfile::K2 || gameId != resource::GameID::KotOR) {
        return;
    }
    const auto &k2Path = _options.game.k2Path;
    if (k2Path.empty()) {
        throw std::runtime_error("--hud k2 in a K1 session needs --k2game <K2 install directory>");
    }
    if (!resource::isK2Install(k2Path)) {
        throw std::runtime_error("--k2game is not a K2 install (chitin.key and swkotor2.exe expected): " + k2Path.string());
    }
    _resourceModule->setK2PresentationPath(k2Path);
}

/**
 * The K2 systems merge (design note k2_merge) needs a K2 install; it is on
 * whenever a K1 session names one, unless --k2merge 0. A named install that
 * is not a K2 install is an error at startup, not a half-merged game.
 */
void Engine::configureK2Merge(resource::GameID gameId) {
    const auto &k2Path = _options.game.k2Path;
    if (gameId != resource::GameID::KotOR || k2Path.empty() || !_options.game.k2Merge) {
        return;
    }
    if (!resource::isK2Install(k2Path)) {
        throw std::runtime_error("--k2game is not a K2 install (chitin.key and swkotor2.exe expected): " + k2Path.string());
    }
    _resourceModule->setK2DataPath(k2Path);
}

void Engine::deinit() {
    _channel.reset();
    _console.reset();
    _profiler.reset();
    _game.reset();
    _pointer.reset();
    _services.reset();

    _gameModule.reset();
    _guiModule.reset();
    _sceneModule.reset();
    _resourceModule.reset();
    _scriptModule.reset();
    _movieModule.reset();
    _audioModule.reset();
    _graphicsModule.reset();
    _systemModule.reset();
    _clock.reset();

    _optionsView.reset();
    _window.reset();

    SDL_Quit();
}

int Engine::run() {
    auto &clock = _services->system.clock;
    // Sampled in microseconds every frame below, so the baseline has to be
    // microseconds too. Seeding it from millis() made the first frame time
    // come out as very nearly the whole clock epoch - the counter runs from
    // system boot, so it was routinely days.
    _ticks = clock.micros();

    bool quit = false;
    while (!quit) {
        frameperf::beginFrame(_frame + 1);
        {
            frameperf::Scope inputScope(frameperf::Phase::Input);
            processEvents(quit);
        }
        if (quit) {
            break;
        }
        if (!_window->isInFocus() && !_options.graphics.headless && !_options.runUnfocused) {
            std::this_thread::sleep_for(std::chrono::milliseconds {100});
            continue;
        }
        uint64_t ticks = clock.micros();
        if (_game->consumeTimingDiscontinuity()) {
            // A synchronous load blocked somewhere in the last frame. Rebase
            // onto now so that interval is not charged to the world as elapsed
            // gameplay time: this frame opens a new epoch and starts at zero.
            _ticks = ticks;
            frameperf::markLastFrameLoad();
        }
        auto frameTime = (ticks - _ticks) / 10e5f;
        _ticks = ticks;
        if (_options.graphics.headless) {
            frameTime = _options.headlessFrameTime;
        }
        ++_frame;
        _engineTime += frameTime;
        processScriptedCommands(quit);
        if (quit) {
            break;
        }
        _profiler->measure(kMainThreadName, kProfilerInputTimeIndex, [this, &quit]() {
            frameperf::Scope inputScope(frameperf::Phase::Input);
            while (!_events.empty()) {
                auto event = _events.front();
                _events.pop();
                if (_profiler->handle(event)) {
                    continue;
                }
                if (_console->handle(event)) {
                    continue;
                }
                if (_game->handle(event)) {
                    if (_game->isQuitRequested()) {
                        quit = true;
                        break;
                    }
                    continue;
                }
            }
        });
        if (quit) {
            break;
        }
        _profiler->measure(kMainThreadName, kProfilerUpdateTimeIndex, [this, &frameTime]() {
            imguiNewFrame();
            _game->update(frameTime);
            bool showcur = _game->cursorType() == CursorType::None;
            bool relmouse = _game->relativeMouseMode();
            showCursor(showcur);
            setRelativeMouseMode(relmouse);
            _profiler->update(frameTime);
        });
        _profiler->measure(kMainThreadName, kProfilerRenderGraphicsTimeIndex, [this, &quit]() {
            frameperf::Scope renderScope(frameperf::Phase::Render);
            _services->graphics.statistic.resetDrawCalls();
            if (_options.graphics.pbr) {
                _services->graphics.pbrTextures.refresh();
            }
            _services->graphics.context.clearColorDepth();
            graphics::gputimer::begin("frame");
            _game->render();
            _profiler->render();
            _console->render();
            imguiRender();
            captureIfRequested(quit);
            graphics::gputimer::end("frame");
            graphics::gputimer::endFrame();
            frameperf::Scope swapScope(frameperf::Phase::Swap);
            _window->swap();
        });
        _profiler->measure(kMainThreadName, kProfilerRenderAudioTimeIndex, [this]() {
            frameperf::Scope audioScope(frameperf::Phase::Audio);
            _services->audio.mixer.render();
        });
        frameperf::endFrame();
    }

    imguiShutdown();
    return 0;
}

void Engine::initAutomationCommands() {
    auto pause = [this](const game::ConsoleArgs &args) {
        auto frames = args.get<int>(1);
        if (!frames || *frames < 1) {
            throw std::invalid_argument("usage: pause <frames>, where frames is positive");
        }
        _scriptPauseFrames = *frames;
    };
    _console->registerCommand("pause", "pause command-file execution for a number of rendered frames", pause);
    _console->registerCommand("frames", "alias of pause", pause);
    _console->registerCommand("ping", "reply pong", [this](const auto &) {
        _console->printLine("pong");
    });
    _console->registerCommand("perf", "frame-time statistics and hitches as one line of JSON: perf [window|reset]", [this](const auto &args) {
        auto mode = args[1];
        if (mode && *mode == "reset") {
            frameperf::resetWindow();
            _console->printLine("{}");
            return;
        }
        // "perf window" reports and starts a new window, and hands over the
        // hitches since the last such call: the playtest recorder's poll.
        bool window = mode && *mode == "window";
        _console->printLine(frameperf::json(window));
        if (window) {
            frameperf::resetWindow();
        }
    });
    // Rendering only: none of this reaches the simulation.
    _console->registerCommand("gfx", "graphics options: gfx [preset retail|enhanced | <option> <value>], as one line of JSON", [this](const auto &args) {
        auto name = args[1];
        if (name) {
            auto value = args[2];
            if (!value) {
                throw std::invalid_argument("usage: gfx <option> <value>");
            }
            int anisotropy = _options.graphics.anisotropicFiltering;
            graphics::setGraphicsOption(_options.graphics, std::string(*name), std::string(*value));
            if (_options.graphics.anisotropicFiltering != anisotropy) {
                _services->resource.textures.applyAnisotropy();
            }
        }
        _console->printLine(graphics::graphicsOptionsJson(_options.graphics));
    });
    _console->registerCommand("surfacedetail", "surface detail (gfx normalmaps): folder, maps found and missed, tangent cost, as one line of JSON", [this](const auto &) {
        _console->printLine(_services->resource.textures.surfaceDetailJson());
    });
    _console->registerCommand("gfxperf", "GPU milliseconds per render pass as one line of JSON: gfxperf [on|off|reset]", [this](const auto &args) {
        auto mode = args[1];
        if (mode && *mode == "on") {
            graphics::gputimer::setEnabled(true);
            graphics::gputimer::reset();
        } else if (mode && *mode == "off") {
            graphics::gputimer::setEnabled(false);
        } else if (mode && *mode == "reset") {
            graphics::gputimer::setEnabled(true);
            graphics::gputimer::reset();
        }
        _console->printLine(graphics::gputimer::json());
    });
    _console->registerCommand("rdiag", "print the render diagnostics summary as one line of JSON", [this](const auto &) {
        _console->printLine(renderdiag::summaryJson());
    });
    _console->registerCommand("capture", "capture one or more rendered frames", [this](const auto &args) {
        auto path = args[1];
        auto count = args.template get<int>(2).value_or(1);
        if (!path || count < 1) {
            throw std::invalid_argument("usage: capture <path> [frames], where frames is positive");
        }
        _captureRequest = CaptureRequest {std::filesystem::path(std::string(*path)), count, 0};
    });
    _console->registerCommand("capturedepth", "capture the window's depth buffer as a grey image, with its range as JSON: capturedepth <path>", [this](const auto &args) {
        auto path = args[1];
        if (!path) {
            throw std::invalid_argument("usage: capturedepth <path>");
        }
        _captureRequest = CaptureRequest {std::filesystem::path(std::string(*path)), 1, 0, true};
    });
    _console->registerCommand("quit", "end the engine run", [this](const auto &) {
        _scriptQuitRequested = true;
    });

    if (_options.commandsFile.empty()) {
        return;
    }
    std::ifstream file(_options.commandsFile);
    if (!file.good()) {
        throw std::runtime_error("Failed to open commands file: " + _options.commandsFile);
    }
    for (std::string line; std::getline(file, line);) {
        std::string_view command = string_strip(line);
        if (!command.empty()) {
            _scriptedCommands.emplace_back(command);
        }
    }
}

void Engine::processScriptedCommands(bool &quit) {
    if (_scriptQuitRequested) {
        quit = true;
        return;
    }
    if (_captureRequest) {
        return;
    }
    if (_scriptPauseFrames > 0) {
        --_scriptPauseFrames;
        if (_scriptPauseFrames > 0) {
            return;
        }
    }
    // Multi-frame game commands (waitidle) hold the queue until satisfied.
    if (_game->pollCommandBlocker()) {
        return;
    }
    if (_pendingReply) {
        sendReply(*_pendingReply);
        _pendingReply.reset();
    }

    while (!_scriptedCommands.empty()) {
        std::string command(std::move(_scriptedCommands.front()));
        _scriptedCommands.pop_front();
        _console->execute(command);

        if (_scriptQuitRequested) {
            quit = true;
            return;
        }
        if (_captureRequest) {
            return;
        }
        if (_scriptPauseFrames > 0) {
            return;
        }
        if (_game->hasCommandBlocker()) {
            return;
        }
    }

    // Channel commands run only once the commands file is exhausted, and one
    // at a time behind any pause or capture that is still in progress.
    if (!_channel) {
        return;
    }
    while (auto request = _channel->poll()) {
        executeChannelCommand(*request);
        if (_scriptQuitRequested) {
            quit = true;
            return;
        }
        if (_captureRequest || _scriptPauseFrames > 0 || _game->hasCommandBlocker()) {
            return;
        }
    }
}

void Engine::executeChannelCommand(const ControlRequest &request) {
    _console->beginCapture();
    bool ok = _console->execute(request.command);
    ControlReply reply {request.id, request.command, ok, _console->endCapture()};
    if (_captureRequest || _scriptPauseFrames > 0 || _game->hasCommandBlocker()) {
        // Multi-frame command: reply when it has finished.
        _pendingReply = std::move(reply);
        return;
    }
    sendReply(reply);
    if (_scriptQuitRequested) {
        _channel->stop();
    }
}

void Engine::sendReply(const ControlReply &reply) {
    if (_channel) {
        _channel->send(formatControlReply(reply, _frame, _engineTime, _game->tickCount()));
    }
}

void Engine::captureIfRequested(bool &quit) {
    if (_captureRequest) {
        if (_captureRequest->depth) {
            captureDepth(_captureRequest->path);
        } else {
            captureFrame(numberedCapturePath(*_captureRequest));
        }
        ++_captureRequest->index;
        if (_captureRequest->index >= _captureRequest->count) {
            _captureRequest.reset();
        }
    }
}

void Engine::captureDepth(const std::filesystem::path &path) {
    int w = _options.graphics.width;
    int h = _options.graphics.height;
    auto depth = _services->graphics.context.captureScreenDepth(w, h);
    if (depth.empty()) {
        warn("capturedepth: the window's depth buffer cannot be read");
        if (_pendingReply) {
            _pendingReply->output.push_back("{\"error\":\"depth unreadable\"}");
        }
        return;
    }
    // Stretch the range below the clear value (1.0) to grey, near is white.
    float lo = 1.0f;
    float hi = 0.0f;
    size_t cleared = 0;
    std::set<int> levels;
    for (float d : depth) {
        if (d >= 1.0f) {
            ++cleared;
            continue;
        }
        lo = std::min(lo, d);
        hi = std::max(hi, d);
        levels.insert(static_cast<int>(d * 65536.0f));
    }
    auto pixels = std::make_shared<ByteBuffer>(static_cast<size_t>(3) * w * h);
    float range = std::max(hi - lo, 1e-6f);
    for (size_t i = 0; i < depth.size(); ++i) {
        float t = depth[i] >= 1.0f ? 0.0f : 1.0f - (depth[i] - lo) / range;
        auto v = static_cast<uint8_t>(std::clamp(t, 0.0f, 1.0f) * 235.0f + (depth[i] >= 1.0f ? 0.0f : 20.0f));
        (*pixels)[3 * i] = (*pixels)[3 * i + 1] = (*pixels)[3 * i + 2] = v;
    }
    auto texture = std::make_shared<graphics::Texture>("depthshot", graphics::TextureType::TwoDim, graphics::Texture::Properties {});
    texture->setPixels(w, h, graphics::PixelFormat::RGB8, graphics::Texture::Layer {pixels});
    auto stream = FileOutputStream(path);
    TgaWriter(texture).save(stream);
    std::ostringstream json;
    json << "{\"path\":\"" << path.generic_string() << "\",\"min\":" << (levels.empty() ? 1.0f : lo)
         << ",\"max\":" << (levels.empty() ? 1.0f : hi)
         << ",\"cleared\":" << static_cast<double>(cleared) / depth.size()
         << ",\"levels\":" << levels.size() << "}";
    info("Wrote depth: " + json.str());
    if (_pendingReply) {
        _pendingReply->output.push_back(json.str());
    }
}

void Engine::captureFrame(const std::filesystem::path &path) {
    auto screenshot = _services->graphics.context.captureScreen(
        _options.graphics.width, _options.graphics.height);
    auto stream = FileOutputStream(path);
    TgaWriter(screenshot).save(stream);
    info("Wrote screenshot: " + path.string());
    if (_pendingReply) {
        _pendingReply->output.push_back("Wrote screenshot: " + path.string());
    }
}

std::filesystem::path Engine::numberedCapturePath(const CaptureRequest &request) const {
    if (request.count == 1) {
        return request.path;
    }
    std::ostringstream suffix;
    suffix << '-' << std::setfill('0') << std::setw(4) << (request.index + 1);
    return request.path.parent_path() /
           (request.path.stem().string() + suffix.str() + request.path.extension().string());
}

void Engine::processEvents(bool &quit) {
    std::queue<input::Event> unhandled;
    SDL_Event sdlEvent;
    while (SDL_PollEvent(&sdlEvent)) {
        if (sdlEvent.type == SDL_EVENT_QUIT) {
            quit = true;
            break;
        }
        if (_options.graphics.headless) {
            if (_window->isAssociatedWith(sdlEvent) && _window->handle(sdlEvent) && _window->isCloseRequested()) {
                quit = true;
                break;
            }
            continue;
        }
        if (imguiHandle(sdlEvent)) {
            continue;
        }
        if (!_window->isAssociatedWith(sdlEvent)) {
            imguiHandle(sdlEvent);
            continue;
        }
        if (_window->handle(sdlEvent)) {
            if (_window->isCloseRequested()) {
                quit = true;
                break;
            }
            continue;
        }
        auto event = eventFromSDLEvent(sdlEvent);
        if (!event) {
            continue;
        }
        if (_profiler->handle(*event)) {
            continue;
        }
        unhandled.push(*event);
    }
    while (!unhandled.empty()) {
        _events.push(std::move(unhandled.front()));
        unhandled.pop();
    }
}

void Engine::showCursor(bool show) {
    if (_showCursor == show) {
        return;
    }
    if (show) {
        SDL_ShowCursor();
    } else {
        SDL_HideCursor();
    }
    _showCursor = show;
}

void Engine::setRelativeMouseMode(bool relative) {
    if (_relativeMouseMode == relative) {
        return;
    }
    _window->setRelativeMouseMode(relative);
    _relativeMouseMode = relative;
}

static constexpr int scaleWinCoord(int coord, int winScale) {
    return coord * 100 / winScale;
}

std::optional<input::Event> Engine::eventFromSDLEvent(const SDL_Event &sdlEvent) const {
    switch (sdlEvent.type) {
    case SDL_EVENT_KEY_DOWN:
        return input::Event::newKeyDown(input::KeyEvent {
            sdlEvent.key.down,
            static_cast<input::KeyCode>(sdlEvent.key.key),
            sdlEvent.key.mod,
            sdlEvent.key.repeat});
    case SDL_EVENT_KEY_UP:
        return input::Event::newKeyUp(input::KeyEvent {
            sdlEvent.key.down,
            static_cast<input::KeyCode>(sdlEvent.key.key),
            sdlEvent.key.mod,
            sdlEvent.key.repeat});
    case SDL_EVENT_MOUSE_MOTION:
        return input::Event::newMouseMotion(input::MouseMotionEvent {
            scaleWinCoord(sdlEvent.motion.x, _options.graphics.winScale),
            scaleWinCoord(sdlEvent.motion.y, _options.graphics.winScale),
            sdlEvent.motion.xrel,
            sdlEvent.motion.yrel});
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        return input::Event::newMouseButtonDown(input::MouseButtonEvent {
            static_cast<input::MouseButton>(sdlEvent.button.button),
            sdlEvent.button.down,
            sdlEvent.button.clicks,
            scaleWinCoord(sdlEvent.button.x, _options.graphics.winScale),
            scaleWinCoord(sdlEvent.button.y, _options.graphics.winScale)});
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return input::Event::newMouseButtonUp(input::MouseButtonEvent {
            static_cast<input::MouseButton>(sdlEvent.button.button),
            sdlEvent.button.down,
            sdlEvent.button.clicks,
            scaleWinCoord(sdlEvent.button.x, _options.graphics.winScale),
            scaleWinCoord(sdlEvent.button.y, _options.graphics.winScale)});
    case SDL_EVENT_MOUSE_WHEEL: {
        return input::Event::newMouseWheel(input::MouseWheelEvent {
            sdlEvent.wheel.x,
            sdlEvent.wheel.y,
            static_cast<input::MouseWheelDirection>(sdlEvent.wheel.direction)});
    default:
        return std::nullopt;
    }
    }
}

} // namespace reone
