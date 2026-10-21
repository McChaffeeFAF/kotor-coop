/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace reone {

namespace game {

/**
 * The dedicated co-op host (docs/dedicated_host.md): `engine
 * --dedicated` runs a Free Mode session with no local player. Everything
 * here is plain logic with no engine types, so the rules can be tested
 * without a game: the settings, who drives the story, when to autosave, the
 * shutdown signal, the admin commands and the controller that sequences them.
 * Game-facing pieces sit behind IDedicatedBackend (dedicatedgame.cpp).
 */

// Settings

constexpr int kDedicatedMinPlayers = 1;
constexpr int kDedicatedMaxPlayers = 8;
constexpr uint16_t kDedicatedDefaultPort = 27015;
constexpr uint32_t kDedicatedDefaultSaveSlot = 1000;
constexpr double kDedicatedDefaultAutosaveSeconds = 300.0;

struct DedicatedSettings {
    bool enabled {false};
    uint16_t port {kDedicatedDefaultPort};
    int maxPlayers {kDedicatedMaxPlayers}; // guests; the host seat is not a player
    std::string password;                  // empty: open
    std::string server;                    // join-code service host[:port]; empty: direct ip:port only
    uint32_t saveSlot {kDedicatedDefaultSaveSlot};
    double autosaveSeconds {kDedicatedDefaultAutosaveSeconds}; // 0 turns the interval off (shutdown still saves)
    std::string startModule;     // when no save exists: warp here after a quick new game; empty: the game's first module
    bool adminStdin {true};      // read admin commands from standard input
};

/** An empty string when the settings are usable, else what is wrong. */
std::string validateDedicatedSettings(const DedicatedSettings &settings);

// Who drives the story

/**
 * With no local leader somebody has to make the choices in conversations and
 * terminals. The rule (docs/dedicated_host.md): the guest who has been in the
 * session longest. When that guest leaves, the next-longest takes over; a guest
 * who comes back joins the back of the line. Pure bookkeeping over client ids.
 */
class StoryDriverTracker {
public:
    /**
     * The clients present now, in any order. New ones join the back of the
     * line, absent ones drop out. Returns true when the driver changed.
     */
    bool update(const std::vector<uint32_t> &present);

    /** 0 when nobody is present. */
    uint32_t driver() const { return _line.empty() ? 0 : _line.front(); }
    const std::vector<uint32_t> &line() const { return _line; }

private:
    std::vector<uint32_t> _line;
};

/**
 * A conversation waiting for a reply with nobody to answer it must not stall
 * the world forever. After `delay` seconds with the reply list shown the first
 * reply is taken; the delay is short when no driver is present and long when
 * a driver has the choice.
 */
class ConversationFallback {
public:
    static constexpr double kNoDriverDelay = 3.0;
    static constexpr double kDriverDelay = 90.0;

    /** True when the first reply should be taken now (and the timer starts over). */
    bool update(double dt, bool replyListShown, bool driverPresent);
    void reset() { _idle = 0.0; }
    double idle() const { return _idle; }

private:
    double _idle {0.0};
};

// Autosave

/**
 * Counts game time between saves. Not due while a save is running or
 * refused; a refused save (loading, a cutscene) is tried again after
 * `retrySeconds`, not after a whole interval.
 */
class AutosaveScheduler {
public:
    explicit AutosaveScheduler(double intervalSeconds = kDedicatedDefaultAutosaveSeconds, double retrySeconds = 10.0) :
        _interval(intervalSeconds), _retry(retrySeconds), _remaining(intervalSeconds) {
    }

    bool enabled() const { return _interval > 0.0; }
    void advance(double dt);
    /** An interval has passed and no save is running. */
    bool due() const { return enabled() && !_saving && _remaining <= 0.0; }
    /** A save was accepted. */
    void started() { _saving = true; }
    /** The save finished (ok) or failed; either way the clock starts over, a failure sooner. */
    void finished(bool ok);
    /** The request was refused (not at a saveable point, busy): try again shortly. */
    void refused();

    bool saving() const { return _saving; }
    double secondsUntilDue() const { return _remaining > 0.0 ? _remaining : 0.0; }
    uint64_t saves() const { return _saves; }
    uint64_t failures() const { return _failures; }

private:
    double _interval;
    double _retry;
    double _remaining;
    bool _saving {false};
    uint64_t _saves {0};
    uint64_t _failures {0};
};

// Shutdown signals

/** Install SIGINT and SIGTERM handlers that only set a flag; the main loop shuts down cleanly. */
void installDedicatedSignalHandlers();
/** True once a signal arrived (or requestDedicatedShutdown was called). */
bool dedicatedShutdownRequested();
/** The signal's number, 0 if none. */
int dedicatedShutdownSignal();
/** For tests and the admin command. */
void requestDedicatedShutdown(int signal = 0);
void clearDedicatedShutdown();

// The game side

struct DedicatedPlayer {
    uint32_t id {0};
    std::string name;
    std::string companion;
    bool waiting {false};
    bool driver {false}; // filled in by the controller
};

enum class SaveOutcome {
    Accepted,
    Busy,       // another save is pending
    NotAllowed, // not at a saveable point (loading, a transition, no module yet)
};

/** What the controller needs from the game. DedicatedGameBackend implements it over Game. */
class IDedicatedBackend {
public:
    virtual ~IDedicatedBackend() = default;

    /** Start hosting Free Mode. */
    virtual bool startHost(const DedicatedSettings &settings, std::string &error) = 0;
    virtual bool saveExists(uint32_t slot) = 0;
    /** Begin loading the save (it finishes over the next frames). False: unreadable. */
    virtual bool loadSave(uint32_t slot, std::string &error) = 0;
    /** Begin a new game; with a module name, in that module. */
    virtual bool startNewGame(const std::string &module, std::string &error) = 0;
    /** A module is running and the world is playable. */
    virtual bool worldReady() = 0;

    virtual SaveOutcome requestSave(uint32_t slot, const std::string &name) = 0;
    virtual bool saveBusy() = 0;
    /** Whether the most recent save request succeeded. Read after saveBusy() turns false. */
    virtual bool lastSaveSucceeded() = 0;

    virtual std::vector<DedicatedPlayer> players() = 0;
    virtual bool kick(uint32_t id) = 0;
    virtual uint32_t storyDriver() = 0;
    virtual std::string currentModule() = 0;

    /** Say goodbye to the guests and stop hosting. */
    virtual void stopHost() = 0;
};

/** Where the controller stands, for status. */
enum class DedicatedState {
    Boot,
    Loading,
    Running,
    ShuttingDown,
    Done,
    Failed
};

const char *dedicatedStateName(DedicatedState state);

/**
 * Sequences the dedicated host: start hosting, load the save (or start a new
 * game), run with autosave, then on shutdown save and stop. Call update() once
 * per frame with the frame's seconds.
 */
class DedicatedHost {
public:
    static constexpr double kLoadTimeoutSeconds = 600.0;
    static constexpr double kShutdownSaveTimeoutSeconds = 30.0;

    DedicatedHost(DedicatedSettings settings, IDedicatedBackend &backend);

    void update(double dt);

    DedicatedState state() const { return _state; }
    /** True once the process may exit (Done or Failed). */
    bool finished() const { return _state == DedicatedState::Done || _state == DedicatedState::Failed; }
    bool failed() const { return _state == DedicatedState::Failed; }
    const std::string &error() const { return _error; }
    const DedicatedSettings &settings() const { return _settings; }
    const AutosaveScheduler &autosave() const { return _autosave; }

    // The admin interface (also reachable through DedicatedAdmin)

    std::vector<DedicatedPlayer> players();
    /** Kick by client id. */
    bool kick(uint32_t id, std::string &error);
    /** Kick by exact name (case-insensitive). */
    bool kickByName(const std::string &name, std::string &error);
    /** Save now. False with error when it cannot be taken. */
    bool saveNow(std::string &error);
    /** Save, say goodbye and exit. */
    void shutdown();
    bool shuttingDown() const { return _state == DedicatedState::ShuttingDown || _state == DedicatedState::Done; }

    /** One JSON object describing the state. */
    std::string statusJson();

private:
    void boot();
    void updateLoading(double dt);
    void updateRunning(double dt);
    void updateShutdown(double dt);
    void fail(const std::string &message);
    bool requestSaveNow(const std::string &name, std::string &error);

    DedicatedSettings _settings;
    IDedicatedBackend &_backend;
    DedicatedState _state {DedicatedState::Boot};
    std::string _error;
    AutosaveScheduler _autosave;
    double _stateTime {0.0};
    double _uptime {0.0};
    bool _manualSaveRunning {false};
    bool _shutdownSaveRequested {false};
    bool _shutdownSaveRunning {false};
    bool _resumed {false};
    std::string _lastSaveMessage;
};

// Admin commands

/**
 * The admin commands: `help`, `status`, `players`, `kick <id|name>`, `save`,
 * `shutdown` (also `stop`, `quit`, `exit`). One JSON object per reply line,
 * the same on the control port (`dedicated <command>`) and standard input.
 */
struct AdminReply {
    bool ok {true};
    std::vector<std::string> lines; // JSON, one object each
};

AdminReply executeDedicatedAdmin(DedicatedHost &host, const std::string &line);

/** JSON string escaping for the replies (also used by tests). */
std::string dedicatedJsonString(const std::string &text);

} // namespace game

} // namespace reone
