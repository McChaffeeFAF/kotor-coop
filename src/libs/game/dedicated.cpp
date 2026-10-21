/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/dedicated.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cctype>
#include <sstream>

#include <boost/algorithm/string.hpp>
#include <boost/json.hpp>

#include "reone/system/logutil.h"

namespace reone {

namespace game {

// Settings

std::string validateDedicatedSettings(const DedicatedSettings &settings) {
    if (settings.port == 0) {
        return "the dedicated host needs a UDP port (--host <port>, default 27015)";
    }
    if (settings.maxPlayers < kDedicatedMinPlayers || settings.maxPlayers > kDedicatedMaxPlayers) {
        return "coop-max-players must be between 1 and 8";
    }
    if (settings.password.size() > 100) {
        return "coop-password must be at most 100 characters";
    }
    if (!(settings.autosaveSeconds >= 0.0 && settings.autosaveSeconds <= 86400.0)) {
        return "dedicated-autosave must be between 0 and 86400 seconds";
    }
    if (settings.saveSlot > 999999) {
        return "dedicated-save-slot must be between 0 and 999999";
    }
    // Slot 0 and 1 are the quick and auto saves, which other parts of the game overwrite.
    if (settings.saveSlot < 2) {
        return "dedicated-save-slot 0 and 1 are the quick and auto saves; use 2 or more";
    }
    return std::string();
}

// StoryDriverTracker

bool StoryDriverTracker::update(const std::vector<uint32_t> &present) {
    uint32_t before = driver();
    _line.erase(std::remove_if(_line.begin(), _line.end(), [&present](uint32_t id) {
                    return std::find(present.begin(), present.end(), id) == present.end();
                }),
                _line.end());
    // Newcomers in id order: ids are handed out in the order clients connected.
    auto sorted = present;
    std::sort(sorted.begin(), sorted.end());
    for (uint32_t id : sorted) {
        if (std::find(_line.begin(), _line.end(), id) == _line.end()) {
            _line.push_back(id);
        }
    }
    return driver() != before;
}

// ConversationFallback

bool ConversationFallback::update(double dt, bool replyListShown, bool driverPresent) {
    if (!replyListShown) {
        _idle = 0.0;
        return false;
    }
    _idle += dt;
    if (_idle >= (driverPresent ? kDriverDelay : kNoDriverDelay)) {
        _idle = 0.0;
        return true;
    }
    return false;
}

// AutosaveScheduler

void AutosaveScheduler::advance(double dt) {
    if (!enabled() || _saving) {
        return;
    }
    _remaining -= dt;
}

void AutosaveScheduler::finished(bool ok) {
    _saving = false;
    if (ok) {
        ++_saves;
        _remaining = _interval;
    } else {
        ++_failures;
        _remaining = std::min(_interval, _retry);
    }
}

void AutosaveScheduler::refused() {
    _saving = false;
    _remaining = std::min(_interval, _retry);
}

// Signals

namespace {

volatile std::sig_atomic_t g_shutdownSignal = 0;
std::atomic<bool> g_shutdownRequested {false};

extern "C" void dedicatedSignalHandler(int signal) {
    // Only async-signal-safe work: a flag the main loop reads.
    g_shutdownSignal = signal;
    g_shutdownRequested.store(true);
}

} // namespace

void installDedicatedSignalHandlers() {
    std::signal(SIGINT, dedicatedSignalHandler);
    std::signal(SIGTERM, dedicatedSignalHandler);
}

bool dedicatedShutdownRequested() {
    return g_shutdownRequested.load();
}

int dedicatedShutdownSignal() {
    return static_cast<int>(g_shutdownSignal);
}

void requestDedicatedShutdown(int signal) {
    g_shutdownSignal = signal;
    g_shutdownRequested.store(true);
}

void clearDedicatedShutdown() {
    g_shutdownSignal = 0;
    g_shutdownRequested.store(false);
}

// DedicatedHost

const char *dedicatedStateName(DedicatedState state) {
    switch (state) {
    case DedicatedState::Boot:
        return "boot";
    case DedicatedState::Loading:
        return "loading";
    case DedicatedState::Running:
        return "running";
    case DedicatedState::ShuttingDown:
        return "shutting_down";
    case DedicatedState::Done:
        return "done";
    case DedicatedState::Failed:
        return "failed";
    }
    return "unknown";
}

DedicatedHost::DedicatedHost(DedicatedSettings settings, IDedicatedBackend &backend) :
    _settings(std::move(settings)),
    _backend(backend),
    _autosave(_settings.autosaveSeconds) {
}

void DedicatedHost::fail(const std::string &message) {
    _error = message;
    _state = DedicatedState::Failed;
    reone::error("Dedicated host: " + message);
}

void DedicatedHost::update(double dt) {
    if (finished()) {
        return;
    }
    _uptime += dt;
    _stateTime += dt;
    // A signal or an admin shutdown wins over whatever else is going on.
    if (dedicatedShutdownRequested() && !shuttingDown() && _state != DedicatedState::Boot) {
        info("Dedicated host: shutting down (" +
             (dedicatedShutdownSignal() ? "signal " + std::to_string(dedicatedShutdownSignal()) : std::string("requested")) + ")");
        shutdown();
    }
    switch (_state) {
    case DedicatedState::Boot:
        boot();
        break;
    case DedicatedState::Loading:
        updateLoading(dt);
        break;
    case DedicatedState::Running:
        updateRunning(dt);
        break;
    case DedicatedState::ShuttingDown:
        updateShutdown(dt);
        break;
    default:
        break;
    }
}

void DedicatedHost::boot() {
    if (auto problem = validateDedicatedSettings(_settings); !problem.empty()) {
        fail(problem);
        return;
    }
    std::string problem;
    if (!_backend.startHost(_settings, problem)) {
        fail("cannot host: " + problem);
        return;
    }
    if (_backend.saveExists(_settings.saveSlot)) {
        info("Dedicated host: resuming from save slot " + std::to_string(_settings.saveSlot));
        if (!_backend.loadSave(_settings.saveSlot, problem)) {
            fail("cannot load save slot " + std::to_string(_settings.saveSlot) + ": " + problem);
            return;
        }
        _resumed = true;
    } else {
        info("Dedicated host: no save in slot " + std::to_string(_settings.saveSlot) + "; starting a new game" +
             (_settings.startModule.empty() ? std::string() : " in " + _settings.startModule));
        if (!_backend.startNewGame(_settings.startModule, problem)) {
            fail("cannot start a new game: " + problem);
            return;
        }
    }
    _state = DedicatedState::Loading;
    _stateTime = 0.0;
}

void DedicatedHost::updateLoading(double dt) {
    if (_backend.worldReady()) {
        info("Dedicated host: the world is up (" + _backend.currentModule() + "); accepting players");
        _state = DedicatedState::Running;
        _stateTime = 0.0;
        _autosave = AutosaveScheduler(_settings.autosaveSeconds);
        return;
    }
    if (_stateTime > kLoadTimeoutSeconds) {
        fail("the world did not come up within " + std::to_string(static_cast<int>(kLoadTimeoutSeconds)) + " seconds");
    }
}

bool DedicatedHost::requestSaveNow(const std::string &name, std::string &problem) {
    switch (_backend.requestSave(_settings.saveSlot, name)) {
    case SaveOutcome::Accepted:
        return true;
    case SaveOutcome::Busy:
        problem = "another save is already running";
        return false;
    case SaveOutcome::NotAllowed:
        problem = "the game is not at a point where it can save (loading or in a transition)";
        return false;
    }
    problem = "save refused";
    return false;
}

void DedicatedHost::updateRunning(double dt) {
    _autosave.advance(dt);
    if (_autosave.saving() || _manualSaveRunning) {
        if (!_backend.saveBusy()) {
            bool ok = _backend.lastSaveSucceeded();
            if (_autosave.saving()) {
                _autosave.finished(ok);
            }
            _manualSaveRunning = false;
            _lastSaveMessage = ok ? "saved" : "save failed";
            if (!ok) {
                warn("Dedicated host: the save to slot " + std::to_string(_settings.saveSlot) + " failed");
            }
        }
        return;
    }
    if (_autosave.due()) {
        std::string problem;
        if (requestSaveNow("Dedicated autosave", problem)) {
            _autosave.started();
        } else {
            _autosave.refused();
        }
    }
}

void DedicatedHost::updateShutdown(double dt) {
    if (!_shutdownSaveRequested) {
        // Wait for a save already running, then take the final one.
        if (_backend.saveBusy() && _stateTime < kShutdownSaveTimeoutSeconds) {
            return;
        }
        _shutdownSaveRequested = true;
        std::string problem;
        if (_backend.worldReady() && requestSaveNow("Dedicated shutdown", problem)) {
            _shutdownSaveRunning = true;
        } else if (!problem.empty()) {
            warn("Dedicated host: no shutdown save: " + problem);
        }
        _stateTime = 0.0;
        if (!_shutdownSaveRunning) {
            _backend.stopHost();
            _state = DedicatedState::Done;
        }
        return;
    }
    if (_shutdownSaveRunning && _backend.saveBusy() && _stateTime < kShutdownSaveTimeoutSeconds) {
        return;
    }
    if (_shutdownSaveRunning) {
        if (_backend.saveBusy()) {
            warn("Dedicated host: the shutdown save did not finish in time");
        } else if (!_backend.lastSaveSucceeded()) {
            warn("Dedicated host: the shutdown save failed");
        } else {
            info("Dedicated host: saved to slot " + std::to_string(_settings.saveSlot));
        }
    }
    _backend.stopHost();
    _state = DedicatedState::Done;
}

std::vector<DedicatedPlayer> DedicatedHost::players() {
    auto list = _backend.players();
    uint32_t driver = _backend.storyDriver();
    for (auto &player : list) {
        player.driver = driver != 0 && player.id == driver;
    }
    return list;
}

bool DedicatedHost::kick(uint32_t id, std::string &problem) {
    if (!_backend.kick(id)) {
        problem = "no player with id " + std::to_string(id);
        return false;
    }
    info("Dedicated host: kicked player " + std::to_string(id));
    return true;
}

bool DedicatedHost::kickByName(const std::string &name, std::string &problem) {
    for (const auto &player : _backend.players()) {
        if (boost::iequals(player.name, name)) {
            return kick(player.id, problem);
        }
    }
    problem = "no player named " + name;
    return false;
}

bool DedicatedHost::saveNow(std::string &problem) {
    if (_state != DedicatedState::Running) {
        problem = "the host is not running";
        return false;
    }
    if (_autosave.saving() || _manualSaveRunning) {
        problem = "another save is already running";
        return false;
    }
    if (!requestSaveNow("Dedicated save", problem)) {
        return false;
    }
    _manualSaveRunning = true;
    return true;
}

void DedicatedHost::shutdown() {
    if (shuttingDown() || finished()) {
        return;
    }
    if (_state == DedicatedState::Boot || _state == DedicatedState::Loading) {
        // Nothing worth saving yet.
        _backend.stopHost();
        _state = DedicatedState::Done;
        return;
    }
    _state = DedicatedState::ShuttingDown;
    _stateTime = 0.0;
    _shutdownSaveRequested = false;
}

std::string DedicatedHost::statusJson() {
    boost::json::object status;
    status["state"] = dedicatedStateName(_state);
    status["uptime"] = _uptime;
    status["module"] = _state == DedicatedState::Running || _state == DedicatedState::ShuttingDown ? _backend.currentModule() : std::string();
    status["players"] = static_cast<int64_t>(players().size());
    status["maxPlayers"] = static_cast<int64_t>(_settings.maxPlayers);
    status["port"] = static_cast<int64_t>(_settings.port);
    status["passwordSet"] = !_settings.password.empty();
    status["saveSlot"] = static_cast<int64_t>(_settings.saveSlot);
    status["resumed"] = _resumed;
    status["storyDriver"] = static_cast<int64_t>(_backend.storyDriver());
    boost::json::object autosave;
    autosave["enabled"] = _autosave.enabled();
    autosave["intervalSeconds"] = _settings.autosaveSeconds;
    autosave["nextInSeconds"] = _autosave.secondsUntilDue();
    autosave["saves"] = static_cast<int64_t>(_autosave.saves());
    autosave["failures"] = static_cast<int64_t>(_autosave.failures());
    autosave["saving"] = _autosave.saving() || _manualSaveRunning;
    status["autosave"] = std::move(autosave);
    if (!_error.empty()) {
        status["error"] = _error;
    }
    return boost::json::serialize(status);
}

// Admin commands

std::string dedicatedJsonString(const std::string &text) {
    return boost::json::serialize(boost::json::value(text));
}

namespace {

AdminReply failure(const std::string &message) {
    AdminReply reply;
    reply.ok = false;
    boost::json::object obj;
    obj["ok"] = false;
    obj["error"] = message;
    reply.lines.push_back(boost::json::serialize(obj));
    return reply;
}

AdminReply success(boost::json::object obj = {}) {
    AdminReply reply;
    obj["ok"] = true;
    reply.lines.push_back(boost::json::serialize(obj));
    return reply;
}

bool parseId(const std::string &text, uint32_t &id) {
    if (text.empty() || text.size() > 9 || !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return false;
    }
    id = static_cast<uint32_t>(std::stoul(text));
    return true;
}

} // namespace

AdminReply executeDedicatedAdmin(DedicatedHost &host, const std::string &line) {
    std::string text = boost::trim_copy(line);
    // `dedicated players` on the control port is the same as `players`.
    if (boost::istarts_with(text, "dedicated ")) {
        text = boost::trim_copy(text.substr(10));
    }
    if (text.empty()) {
        return failure("empty command; try help");
    }
    auto space = text.find_first_of(" \t");
    std::string verb = boost::to_lower_copy(text.substr(0, space));
    std::string rest = space == std::string::npos ? std::string() : boost::trim_copy(text.substr(space + 1));

    if (verb == "help") {
        boost::json::object obj;
        boost::json::array commands;
        for (const char *command : {"help", "status", "players", "kick <id|name>", "save", "shutdown"}) {
            commands.emplace_back(command);
        }
        obj["commands"] = std::move(commands);
        return success(std::move(obj));
    }
    if (verb == "status") {
        AdminReply reply;
        reply.lines.push_back(host.statusJson());
        return reply;
    }
    if (verb == "players" || verb == "list") {
        boost::json::array list;
        for (const auto &player : host.players()) {
            boost::json::object entry;
            entry["id"] = static_cast<int64_t>(player.id);
            entry["name"] = player.name;
            entry["companion"] = player.companion;
            entry["waiting"] = player.waiting;
            entry["storyDriver"] = player.driver;
            list.emplace_back(std::move(entry));
        }
        boost::json::object obj;
        obj["count"] = static_cast<int64_t>(list.size());
        obj["players"] = std::move(list);
        return success(std::move(obj));
    }
    if (verb == "kick") {
        if (rest.empty()) {
            return failure("usage: kick <id|name>");
        }
        std::string problem;
        uint32_t id = 0;
        // A number is an id; a name that is all digits can be kicked by id only.
        bool kicked = parseId(rest, id) ? host.kick(id, problem) : host.kickByName(rest, problem);
        if (!kicked) {
            return failure(problem);
        }
        boost::json::object obj;
        obj["kicked"] = rest;
        return success(std::move(obj));
    }
    if (verb == "save") {
        std::string problem;
        if (!host.saveNow(problem)) {
            return failure(problem);
        }
        boost::json::object obj;
        obj["saving"] = true;
        obj["slot"] = static_cast<int64_t>(host.settings().saveSlot);
        return success(std::move(obj));
    }
    if (verb == "shutdown" || verb == "stop" || verb == "quit" || verb == "exit") {
        host.shutdown();
        boost::json::object obj;
        obj["shuttingDown"] = true;
        return success(std::move(obj));
    }
    return failure("unknown command '" + verb + "'; try help");
}

} // namespace game

} // namespace reone
