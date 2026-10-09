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

#include <chrono>
#include <deque>
#include <filesystem>
#include <iosfwd>
#include <functional>
#include <optional>

#include "presentationpointer.h"

#include "reone/audio/source.h"
#include "reone/graphics/cursor.h"
#include "reone/graphics/types.h"
#include "reone/input/event.h"
#include "reone/movie/movie.h"
#include "reone/script/routines.h"
#include "reone/system/fixedstep.h"
#include "reone/system/logutil.h"

#include "action.h"
#include "combat.h"
#include "regeneration.h"
#include "stringtokens.h"
#include "console.h"
#include "di/services.h"
#include "effect.h"
#include "event.h"
#include "floatingtext.h"
#include "visualeffectpresenter.h"
#include "globalfade.h"
#include "d20/prestige.h"
#include "gui/chargen.h"
#include "gui/computer.h"
#include "gui/confirmpopup.h"
#include "gui/container.h"
#include "gui/conversation.h"
#include "dialoglocks.h"
#include "gui/dialog.h"
#include "gui/galaxymap.h"
#include "gui/hud.h"
#include "gui/ingame.h"
#include "gui/loadscreen.h"
#include "gui/mainmenu.h"
#include "gui/map.h"
#include "gui/partyselect.h"
#include "gui/pazaak.h"
#include "gui/saveload.h"
#include "gui/credits.h"
#include "gui/store.h"
#include "gui/upgrade.h"
#include "hudpresentation.h"
#include "journal.h"
#include "keymap.h"
#include "location.h"
#include "messagelog.h"
#include "scripteventqueue.h"
#include "usersettings.h"
#include "object/area.h"
#include "object/camera/animated.h"
#include "object/camera/dialog.h"
#include "object/camera/firstperson.h"
#include "object/camera/static.h"
#include "object/camera/thirdperson.h"
#include "object/creature.h"
#include "object/door.h"
#include "object/encounter.h"
#include "object/module.h"
#include "object/placeable.h"
#include "object/sound.h"
#include "object/store.h"
#include "object/trigger.h"
#include "object/waypoint.h"
#include "options.h"
#include "party.h"
#include "pazaaksession.h"
#include "saveprovenance.h"
#include "savegame.h"
#include "script/runner.h"
#include "statussummary.h"
#include "storesession.h"
#include "swooprace.h"
#include "talent.h"
#include "turret.h"

#include <queue>
#include <vector>

namespace reone {

namespace gui {

class GUI;

}

namespace graphics {

class Font;
class Texture;

}

namespace resource {

class PreparedModuleLoad;
class SaveWorkingState;

}

namespace game {

enum class ModuleLoadContext {
    FreshModule,
    InitialTemplateRestore,
    InitialSaveRestore,
    SavedModuleTransition,
};

ModuleLoadContext resolveModuleLoadContext(
    bool initialSaveRestore,
    bool savedModuleSnapshot);

bool restoresSavedWorld(ModuleLoadContext context);
bool restoresSavedSession(ModuleLoadContext context);
bool preservesSavedPlacement(ModuleLoadContext context);

struct SavedObjectReference;
struct SerializedScriptSituation;
class SavedScriptContinuation;
class ModuleSnapshotBuilder;
class CoopSession;
struct CoopLaunch;

/** modulesave.2da autosaveonenter. */
enum class AutosaveOnEnter {
    Force,
    Yes,
    No,
    Missing
};

AutosaveOnEnter parseAutosaveOnEnter(const std::optional<std::string> &cell);
bool shouldAutosaveOnEnter(AutosaveOnEnter mode, bool autosaveOption, bool intervalElapsed);

/**
 * modulesave.2da deletesavegrouponenter: the modules whose cached states are
 * dropped on entering moduleName (every row of the named save group, other
 * than the module entered). Empty when the row names no group.
 */
std::vector<std::string> modulesDeletedOnEnter(const resource::TwoDA &modulesave, const std::string &moduleName);

/**
 * Load screen image for a module: its loadscreens.2da row's bmpresref, or
 * load_<module> when the table has no row for it.
 */
std::string loadScreenImage(const resource::TwoDA *loadscreens, const std::string &moduleName);

/** loadscreenhints.2da gameplayhint of a row, or -1. */
int loadScreenHint(const resource::TwoDA &hints, int row);

class Game : boost::noncopyable {
public:
    enum class Screen {
        None,
        MainMenu,
        Loading,
        CharacterGeneration,
        InGame,
        InGameMenu,
        Conversation,
        Container,
        PartySelection,
        SaveLoad,
        GalaxyMap,
        SwoopRace,
        PazaakWager,
        PazaakSetup,
        PazaakBoard,
        Turret,
        Store,
        Upgrade,
        Credits
    };

    Game(
        resource::GameID gameId,
        std::filesystem::path path,
        OptionsView &options,
        ServicesView &services,
        IConsole &console,
        std::shared_ptr<PresentationPointer> pointer = nullptr) :
        _gameId(gameId),
        _path(std::move(path)),
        _options(options),
        _services(services),
        _console(console),
        _party(*this),
        _combat(*this, services),
        _regeneration(*this, services),
        _swoopRace(*this, services),
        _turret(*this, services),
        _journal(services.resource.gffs, services.resource.strings),
        _floatingText(*this, services),
        _pointer(pointer ? std::move(pointer) : std::make_shared<PresentationPointer>(services.resource.cursors)) {
        initJournalNotifications();
    }

    ~Game();

    void init();

    bool handle(const input::Event &event);

    /**
     * One rendered frame. Adds frameTime (scaled by the developer game
     * speed) to the fixed-step clock, runs every simulation tick that is due,
     * then advances per-frame presentation: camera, GUI animation, fades,
     * music, and the scene graph's render preparation.
     */
    void update(float frameTime);
    void render();

    /**
     * Simulation ticks run so far. Every tick advances the world by exactly
     * tickSeconds(), whatever the frame rate.
     */
    uint64_t tickCount() const { return _tickCount; }
    int tickRate() const { return _simClock.tickRate(); }
    float tickSeconds() const { return _simClock.stepSeconds(); }
    void setTickRate(int tickRate) { _simClock.setTickRate(tickRate); }

    /**
     * How far the render frame is between the last tick and the next, in
     * [0, 1). The seam for interpolating presentation between ticks.
     */
    float tickAlpha() const { return _simClock.alpha(); }

    /**
     * The tick alpha the frame is drawn at (design note render_interp): 1,
     * the last tick as simulated, when interpolation is off, headless or the
     * tick gate is closed.
     */
    float presentationAlpha() const;

    /**
     * Co-op session (design note coop_net), or null in single-player. With
     * no session nothing below changes behaviour. A host session applies
     * client commands before each tick and sends snapshots after it; a
     * client session replaces the simulation tick with snapshot playback.
     */
    void setCoopSession(std::shared_ptr<CoopSession> session);
    CoopSession *coop() const { return _coop.get(); }

    /**
     * Co-op from the menus (design note coop_online): what every session
     * this engine starts shares (install paths, the join-code service, the
     * player name, test settings), then Host and Join. Both return false
     * with error set when the session cannot start; nothing is left open.
     */
    void setCoopDefaults(const CoopLaunch &launch);
    const CoopLaunch &coopDefaults() const;
    bool startCoopHost(std::string &error, const std::string &mode = std::string());
    bool startCoopJoin(const std::string &codeOrAddress, std::string &error);
    /** Leave co-op: the session closes (a client's world goes back to the main menu). */
    void endCoopSession();
    /**
     * The tester's recorder (--coop-record): once a second, one JSON line
     * with the screen, module, party and co-op state, for bug reports.
     */
    void setCoopRecordPath(const std::filesystem::path &path);
    ServicesView &services() { return _services; }

    /**
     * Tick gate. While closed, the simulation advances only by ticks granted
     * with grantTicks; frames keep rendering. Harness drivers close it so the
     * tick at which each command lands depends on the commands alone, not on
     * how long a round trip took.
     */
    void setTickGate(bool closed);
    bool isTickGateClosed() const { return _tickGate; }
    void grantTicks(uint64_t ticks) { _tickAllowance += ticks; }
    uint64_t tickAllowance() const { return _tickAllowance; }

    /**
     * Report, and clear, whether a break in gameplay time has occurred since
     * the last call.
     *
     * Loading a module blocks for as long as reading it takes, and that wall
     * time is not time the game world experienced. The caller owns the frame
     * clock, so it is the caller that has to open a new epoch; this only says
     * that one is due.
     */
    bool consumeTimingDiscontinuity();

    /**
     * Multi-frame console commands (waitidle) install a blocker here. The
     * scripted-command runner calls pollCommandBlocker once per frame before
     * running its next command and runs nothing further while it returns
     * true. A blocker returns true while it still blocks; once it returns
     * false it is dropped. Without a blocker this returns false.
     */
    bool pollCommandBlocker();
    bool hasCommandBlocker() const { return static_cast<bool>(_commandBlocker); }
    void setCommandBlocker(std::function<bool()> blocker) { _commandBlocker = std::move(blocker); }

    void playVideo(const std::string &name);

    bool isPaused() const { return _paused; }

    /**
     * Test harness escape hatch: a click on a transition door takes the exit
     * from wherever the leader stands, instead of the retail walk to the door.
     */
    bool instantDoorTransitions() const { return _instantDoorTransitions; }
    void setInstantDoorTransitions(bool instant) { _instantDoorTransitions = instant; }

    /** Number of Game::update calls so far, including movie frames. */
    uint64_t frameCount() const { return _frameCount; }

    /**
     * One-line compact JSON snapshot of the session for automated drivers:
     * screen, party, conversation, combat and objects within radius metres of
     * the party leader. Read-only: it never selects, mutates or draws random
     * numbers.
     */
    std::string dumpStateJson(float radius) const;

    /** One-line compact JSON description of a single object. */
    std::string dumpObjectJson(const Object &object, bool withLocals) const;
    bool isTSL() const { return _gameId == resource::GameID::TSL; }
    resource::GameID gameId() const { return _gameId; }

    /**
     * The ID the HUD family presents as: the --hud profile, or the running
     * game's ID by default. A K2 profile in a K1 session needs the K2 GUI set;
     * without it the HUD stays K1.
     */
    resource::GameID hudPresentationId() const;
    /** The presentation ID, GUI loader and GUI resources for the HUD family. */
    HudPresentationServices hudPresentation() const;

    Camera *getActiveCamera() const;

    OptionsView &options() { return _options; }
    const OptionsView &options() const { return _options; }
    Party &party() { return _party; }
    Combat &combat() { return _combat; }
    Regeneration &regeneration() { return _regeneration; }
    Journal &journal() { return _journal; }
    MessageLog &messageLog() { return _messageLog; }
    FloatingText &floatingText() { return _floatingText; }
    VisualEffectPresenter &visualEffectPresenter() { return _visualEffectPresenter; }
    ScriptRunner &scriptRunner() { return *_scriptRunner; }
    ScriptEventQueue &scriptEvents() { return _scriptEvents; }
    const ScriptEventQueue &scriptEvents() const { return _scriptEvents; }
    Map &map() { return *_map; }
    script::IRoutines &routines() { return *_routines; }

    std::shared_ptr<Module> module() const { return _module; }
    CameraType cameraType() const { return _cameraType; }
    const std::set<std::string> &moduleNames() const { return _moduleNames; }
    const std::set<std::string> &saveNames() const { return _saveNames; }

    void initLocalServices();
    void initK2Routines();

    /**
     * K2 systems merge (design note k2_merge): the prestige classes a
     * creature may take at its next level-up; empty unless unlocked.
     */
    std::vector<ClassType> prestigeOffersFor(const Creature &creature, bool asPlayerCharacter = false) const;

    /// The facts prestige choices depend on. `asPlayerCharacter` treats the
    /// creature as the player character (a co-op guest's own character is one).
    prestige::Facts prestigeFactsFor(const Creature &creature, bool asPlayerCharacter) const;
    void setSceneSurfaces();

    void setCursorType(resource::CursorType type);
    void setPaused(bool paused);

    /** NoClicksFor: ignore player clicks for the given world-clock seconds. */
    void blockClicksFor(float seconds);
    IConsole &console() { return _console; }
    bool areClicksBlocked() const { return _worldTimeMilliseconds < _clicksBlockedUntilMilliseconds; }
    void setRelativeMouseMode(bool relative);

    void openMainMenu();

    /**
     * Main-menu themes (design note p1_k2menus): available in a K1 session
     * presenting K2 menus. Mode is the `menu.theme` setting.
     */
    bool menuThemesAvailable() const;
    MenuThemeMode menuThemeMode() const;
    void openInGame();

    /**
     * End the running game and return to the main menu (EndGame, or the
     * party's defeat). Takes effect at the start of the next tick, never
     * inside the caller's stack.
     */
    void requestEndGame() { _endGameRequested = true; }

    /**
     * The whole party is down and no script raised anyone. The host decides
     * what follows: by default the death screen (Load Last Saved Game, Load
     * Game, Main Menu). A co-op host installs its own handler so that clients
     * follow the host's choice (co-op design notes item 10).
     */
    void onPartyDefeated() { _partyDefeatPending = true; }
    void setPartyDefeatHandler(std::function<void()> handler) { _partyDefeatHandler = std::move(handler); }
    void showDeathScreen();
    /** Load the most recently written save. False when there is none. */
    bool loadLastSave();

    /**
     * Party wipe (co-op design notes "Party wipe"): the autosave taken on
     * entering this area (slot 1, when its last module is this one).
     */
    bool canRetryArea() const;
    bool retryArea();
    /**
     * The death screen's choices, for its buttons and the harness:
     * "retry", "last", "load" or "menu". False when the choice cannot be
     * carried out (no save for it).
     */
    bool chooseDefeatOption(const std::string &choice);
    /** Write the area checkpoint at the next stable point (area entry does this; harness too). */
    void requestAreaCheckpoint() { _checkpointOnEnterPending = true; }
    bool partyDefeatPending() const { return _partyDefeatPending; }
    bool endGameRequested() const { return _endGameRequested; }

    bool hasPlayableRuntimeSession() const { return _runtimeSessionPlayable; }

    // Minigames (swoop race, turret). design note minigames

    /**
     * Start the minigame the current area declares. Runs when a module whose
     * area has a MiniGame struct finishes loading; the minigame's own scripts
     * end it with StartNewModule.
     */
    bool startMiniGame();

    /** Tear the running minigame down (module change, game end). */
    void endMiniGame();

    MiniGameSession *miniGame() { return _miniGame.get(); }
    const MiniGameSession *miniGame() const { return _miniGame.get(); }

    /**
     * The single input seam of the running minigame: local input handlers and
     * the harness autopilot write it, the tick reads it.
     */
    MiniGameInput &miniGameInput() { return _miniGameInput; }

    SwoopRace &swoopRace() { return _swoopRace; }
    Turret &turret() { return _turret; }

    // END Minigames

    void openInGameMenu(InGameMenuTab tab);
    void openLevelUp();
    /** Level up a given party member through the level-up screen. */
    void openLevelUp(const std::shared_ptr<Creature> &member);
    /**
     * ShowLevelUpGUI: true when the player character has a level-up
     * pending; the screen then opens on the next tick, once the world view
     * is showing (design note gui_screens, "Level-up").
     */
    bool requestLevelUpScreen();
    /**
     * Take one pending level through the level-up screen's steps, each with
     * its Recommended pick (or no picks), then Accept: the path the
     * character screen's Auto button and the harness `levelup` take.
     * Returns a one-line summary, empty when nothing was pending.
     */
    /**
     * Take a pending level through the level-up screen's steps. When the K2
     * prestige step comes first, `prestigeClass` picks one of the offered
     * classes; -1 keeps the Jedi class for this level.
     */
    /** The party member the level-up screen is open for, or null. */
    std::shared_ptr<Creature> levelUpInProgress() const;

    std::string levelUpThroughScreen(const std::shared_ptr<Creature> &member, bool recommended, int prestigeClass = -1);

    // Store (design note gui_screens, "Store")

    /** OpenStore: the store screen opens on the next tick. */
    void requestOpenStore(const std::shared_ptr<Store> &store, const std::shared_ptr<Creature> &customer,
                          int bonusMarkUp, int bonusMarkDown);
    /** Show the store screen now, running the store's OnOpenStore first. */
    void openStore(const std::shared_ptr<Store> &store, const std::shared_ptr<Creature> &customer,
                   int bonusMarkUp, int bonusMarkDown);
    void closeStore();
    /** The player's settings (options panels, bindings); see usersettings.h. */
    UserSettings &userSettings() { return _userSettings; }
    Keymap &keymap() { return _keymap; }
    /** Push the settings into the running options, then save them. */
    void applyUserSettings(bool save = true);
    /** Pick and cycle radius, metres from the camera (design note p1_ui, "Selection radius"). */
    float selectionRadius() const { return _selectionRadius; }
    /** The target's action choices dock at a fixed spot on the HUD (design note p1_ui). */
    bool fixedActionBar() const { return _fixedActionBar; }
    /** Q / E: select the previous / next selectable object in range. */
    void cycleSelection(bool previous);

    StoreSession &storeSession() { return _storeSession; }
    const StoreSession &storeSession() const { return _storeSession; }
    StoreGUI *storeGUI() { return _store.get(); }
    /** Buy / sell one unit, as the store screen's Accept does. */
    StoreSession::Result storeBuy(const std::shared_ptr<Item> &item);
    StoreSession::Result storeSell(const std::shared_ptr<Item> &item);

    // END Store

    // Upgrade bench (design note gui_screens, "Upgrade bench")

    /** ShowUpgradeScreen: opens on the next tick, on the item when one is given. */
    void requestUpgradeScreen(const std::shared_ptr<Item> &item);
    void openUpgradeScreen(const std::shared_ptr<Item> &item);
    void closeUpgradeScreen();
    UpgradeGUI *upgradeGUI() { return _upgrade.get(); }
    /** Apply the bench's staged slots, as the Assemble button does. */
    bool assembleUpgrades();

    // END Upgrade bench

    // Credits and movie queue

    /** StartCreditSequence: shows the credits; the world keeps ticking. */
    void startCreditSequence(bool transparentBackground);
    bool isCreditSequenceInProgress() const;
    void endCreditSequence();
    /** QueueMovie / PlayMovieQueue / IsMoviePlaying. */
    void queueMovie(const std::string &name, bool skippable);
    void playMovieQueue(bool allowSeparateSkips);
    bool isMoviePlaying() const { return static_cast<bool>(_movie); }

    // END Credits and movie queue

    void notifyLevelUpPending(const Creature &creature);
    void openContainer(const std::shared_ptr<Object> &container);
    void openPartySelection(const PartySelectionContext &ctx);

    /**
     * Whether the current Area already contains one coherent live runtime
     * representation for every requested logical NPC slot and no other
     * selected companion.
     */
    bool isPartySelectionRealized(
        const std::vector<int> &selectedNpcs) const;

    /**
     * Reconcile requested logical NPC slots with exact roster bindings and
     * current-Area residency. A coherent unchanged selection is a no-op.
     */
    bool reconcilePartySelection(
        const std::vector<int> &selectedNpcs);
    void openSaveLoad(SaveLoadMode mode);
    void openGalaxyMap(int initialPlanet);
    /** Whether the galaxy map may take the screen over from the given one. */
    static bool canOpenGalaxyMapFrom(Screen screen);

    // KotOR I Pazaak lifecycle

    bool playPazaak(
        int opponentDeck,
        std::string continuationScript,
        int maximumWager,
        bool tutorialRequested,
        const std::shared_ptr<Object> &opponent);

    PazaakSession *pazaakSession() { return _pazaakSession.get(); }
    const PazaakSession *pazaakSession() const { return _pazaakSession.get(); }
    const std::optional<PazaakCompletedResult> &lastPazaakResult() const { return _lastPazaakResult; }

    void showPazaakSetup();
    void showPazaakBoard();
    void cancelPazaak();
    void abortPazaak();
    void completePazaakIfReady();

    // END KotOR I Pazaak lifecycle

    void startCharacterGeneration();
    void startDialog(const std::shared_ptr<Object> &owner, const std::string &resRef,
                     GlobalFade::DialogTicket admission = {});

    GlobalFade &globalFade() { return _globalFade; }
    const GlobalFade &globalFade() const { return _globalFade; }
    /**
     * Presentation sound cue: plays a clip (positional when a position is
     * given) and records which clip was chosen and at which tick, so headless
     * runs can check sound selection. Never changes simulation state.
     */
    struct SoundCue {
        uint64_t tick {0};
        std::string category;
        std::string resRef;
    };
    std::shared_ptr<audio::AudioSource> playSoundCue(
        const std::string &category,
        const std::string &resRef,
        std::optional<glm::vec3> position = std::nullopt,
        audio::AudioType type = audio::AudioType::Sound,
        bool loop = false,
        float gain = 1.0f);
    /** Records a cue whose clip the caller plays itself. */
    void recordSoundCue(const std::string &category, const std::string &resRef);
    const std::deque<SoundCue> &soundCues() const { return _soundCues; }
    void fadeOutSoundCue(std::shared_ptr<audio::AudioSource> source, float seconds) { fadeOutAudio(std::move(source), seconds); }

    // Video effects (videoeffects.2da): presentation only.
    struct VideoEffect {
        int row {-1};
        bool saturation {false};
        glm::vec3 modulation {1.0f};
        float saturationAmount {0.0f};
        bool scanNoise {false};
    };
    /** EnableVideoEffect: sticky until DisableVideoEffect or a conversation end. */
    void enableVideoEffect(int row);
    void disableVideoEffect();
    /** A dialog node's CamVidEffect: a row applies, -1 clears unless a script effect is on. */
    void applyDialogVideoEffect(int row);
    const VideoEffect &videoEffect() const { return _videoEffect; }
    bool scriptVideoEffect() const { return _scriptVideoEffect; }

    /** The conversation's own fade layer, driven by dialog node fades. */
    GlobalFade &dialogFade() { return _dialogFade; }
    const GlobalFade &dialogFade() const { return _dialogFade; }

    void pauseConversation();
    void resumeConversation();

    /**
     * SetDialogPlaceableCamera: during a dialog, cut to the area's static
     * camera `cameraId` for the rest of the current entry. False when no
     * dialog is running or the area has no such camera.
     */
    bool setDialogPlaceableCamera(int cameraId);

    /** SetLockOrientationInDialog / SetLockHeadFollowInDialog state. */
    DialogLocks &dialogLocks() { return _dialogLocks; }
    const DialogLocks &dialogLocks() const { return _dialogLocks; }

    /**
     * ResetDialogState: with no conversation running, release a world fade
     * held for a dialog (1 s fade-in).
     */
    void resetDialogState();

    /**
     * Make the music and ambient players follow the current area's music
     * controller. Presentation only: it reads the controller, never the
     * other way round.
     */
    void refreshAreaMusic();

    /** SetMusicVolume: 0.0-1.0 scale on the music from now on. */
    void setMusicVolume(float volume);
    float musicVolume() const { return _musicVolumeScale; }
    const std::string &musicResRef() const { return _musicResRef; }
    const std::string &ambientResRef() const { return _ambientResRef; }

    /**
     * ShowTutorialWindow: pop tutorial.2da row `window`, once per game.
     * Returns true when the window was shown by this call.
     */
    bool showTutorialWindow(int window);
    /** SetTutorialWindowsEnabled: with them off, showTutorialWindow shows nothing. */
    bool tutorialWindowsEnabled() const { return _tutorialWindowsEnabled; }
    void setTutorialWindowsEnabled(bool enabled) { _tutorialWindowsEnabled = enabled; }
    /** A text popup the player confirms (DisplayMessageBox). */
    void showMessageBox(const std::string &text);
    /** The dialog started most recently, "" before any (GetLastConversation). */
    const std::string &lastConversation() const { return _lastConversation; }

    bool wasTutorialWindowShown(int window) const { return _tutorialWindowsShown.count(window) != 0; }

    /** GetSubScreenID: SUBSCREEN_ID_* of the open in-game menu page, 0 if none. */
    int subScreenId() const;

    void setBarkBubbleText(std::string text, float durartion);

    /** Submit one of vanilla's fixed status-summary categories. */
    void submitStatusSummary(StatusSummaryCategory category, int amount = 0, std::vector<std::string> items = {});
    StatusSummaryAccumulator &statusSummary() { return _statusSummary; }
    /**
     * The player's alignment moved by `shift` (+ light, - dark): the status
     * summary's light/dark row and the HUD's shift flash (design note p2_ui).
     */
    void submitAlignmentShift(int shift);

    int getPlotXP(const std::string &plotName);

    /** Award the whole-number percentage used by the GivePlotXP script routine. */
    void awardPlotXP(const std::string &plotName, int percentage);

    /** Award an authored DLG/JRL fraction, where 0.2 means twenty percent. */
    void awardPlotXPByIndex(int plotIndex, float fraction);

    Screen currentScreen() const {
        return _screen;
    }

    /** True while a conversation owns the screen, i.e. a dialogue is running. */
    bool isConversationActive() const {
        return _screen == Screen::Conversation;
    }

    std::shared_ptr<movie::IMovie> movie() const {
        return _movie;
    }

    void quit() {
        _quitRequested = true;
    }

    bool isQuitRequested() {
        return _quitRequested;
    }

    resource::CursorType cursorType() const {
        return _pointer->type();
    }

    bool relativeMouseMode() const {
        return _relativeMouseMode;
    }

    // Module loading

    /**
     * @param entry waypoint tag to spawn at, or empty string to spawn at default location
     */
    bool loadModule(
        const std::string &name,
        std::string entry = "",
        bool initialSaveRestore = false);

    void scheduleModuleTransition(const std::string &moduleName, const std::string &entry);
    void scheduleModuleTransitionWithMovies(const std::string &moduleName, const std::string &entry, std::vector<std::string> movies);

    // Load a savegame. The slot is the durable identity discovered by
    // discoverSavedGames(); it is mounted verbatim rather than re-resolved, so
    // the slot the player picked is the slot that gets loaded.
    bool loadGame(const resource::SaveSlotDescriptor &slot);

    struct PreparedDestinationModule {
        std::string name;
        std::unique_ptr<resource::PreparedModuleLoad> resources;
        std::shared_ptr<resource::Gff> ifo;
        std::shared_ptr<resource::Gff> are;
        std::shared_ptr<resource::Gff> git;
        ModuleLoadContext context {ModuleLoadContext::FreshModule};
    };

    /**
     * Candidate save load, resolved and validated but not yet committed.
     *
     * Everything here is read from the unpublished session, so preparing it
     * cannot disturb the running game. The decoded records are carried into
     * restoration rather than parsed a second time once the candidate has been
     * published.
     */
    struct PreparedSaveLoad {
        struct AutosaveRestoreState {
            std::string startWaypoint;
            uint32_t pauseDay {0};
            uint32_t pauseTime {0};
        };

        std::unique_ptr<resource::SaveSessionState> session;
        resource::NFO nfo;
        std::shared_ptr<resource::Gff> saveInfo;
        std::shared_ptr<resource::Gff> globalVars;
        std::shared_ptr<resource::Gff> partyTable;
        std::shared_ptr<resource::Gff> inventory;
        std::shared_ptr<resource::Gff> playerInfo;
        std::optional<AutosaveRestoreState> autosave;
        PreparedDestinationModule destination;
    };

    std::vector<SavedGame> savedGames() const;
    const std::filesystem::path &gamePath() const { return _path; }

    SaveResult requestSave(SaveRequest request);
    SaveResult requestManualSave(uint32_t slot, std::string displayName);
    SaveResult requestQuickSave();
    SaveResult requestAutoSave();
    const std::optional<SaveResult> &lastSaveResult() const {
        return _lastSaveResult;
    }
    SaveEligibilityReason saveEligibility(bool requireStablePoint = false) const;

    // Clear state of the current game before loading a new game.
    void resetGame();

    // Retire only active-module runtime objects. Party/session objects and
    // committed save-wide state survive so an ordinary transition can build
    // and publish its destination without becoming a full-session load.
    void retireActiveModuleRuntime();

    // Retire instantiated gameplay state without changing committed resource or
    // save-wide logical state. Runtime reconstruction must explicitly publish a
    // new playable session afterwards.
    void retireRuntimeSession();

    // END Module loading

    // Objects

    std::shared_ptr<Object> getObjectById(uint32_t id) const;
    int scaleDamageForDifficulty(int damage, const Object &target) const;
    bool isRuntimeObjectLive(const Object &object) const;

    /**
     * Auto-combat toggle for one character (P1 item 10). The setting stays
     * with the character across leader switches; each co-op player toggles
     * their own character.
     */
    void setAutoCombat(Creature &creature, bool on);
    void toggleLeaderAutoCombat();

    // While a leader's auto-combat round is being decided, the generic AI
    // scripts must see it as a follower: they skip party member 0.
    uint32_t autoCombatScriptCaller() const { return _autoCombatScriptCaller; }
    void setAutoCombatScriptCaller(uint32_t id) { _autoCombatScriptCaller = id; }

    // End the semantic lifetime of this exact object and every runtime object
    // it owns. Registry and saved-identity cleanup are pointer guarded, so a
    // stale owner can never invalidate a newer object using the same number.
    void destroyRuntimeObjectGraph(const std::shared_ptr<Object> &object);

    // Build replacement children outside the live registry, publish ownership
    // with a no-throw swap, then atomically publish the candidates and retire
    // obsolete children. A failed build leaves the old graph and all saved-ID
    // bindings untouched.
    template <class Build, class Publish>
    void replaceRuntimeObjectGraph(
        std::vector<std::shared_ptr<Object>> &obsoleteObjects,
        Build &&build,
        Publish &&publish) {
        static_assert(
            noexcept(std::declval<Publish &>()()),
            "runtime object graph publication must not throw");
        if (_stagedRuntimeObjectGraph) {
            auto obsoleteGraph = collectRuntimeObjectGraph(obsoleteObjects);
            build();
            publish();
            discardStagedRuntimeObjects(obsoleteGraph);
            return;
        }
        beginRuntimeObjectGraphReplacement(obsoleteObjects);
        try {
            build();
            publish();
            commitRuntimeObjectGraphReplacement(obsoleteObjects);
        } catch (...) {
            abortRuntimeObjectGraphReplacement();
            throw;
        }
    }

    inline std::shared_ptr<Module> newModule() {
        return newObject<Module>(*this, _services);
    }
    inline std::shared_ptr<Module> newSavedModule() {
        // The module is not part of the serialized object graph, but it still
        // needs a live identity: authored OnLoad scripts run with the module
        // as OBJECT_SELF and may schedule commands back onto it.
        return newObject<Module>(*this, _services);
    }
    inline std::shared_ptr<Item> newItem() {
        return newObject<Item>(*this, _services);
    }
    inline std::shared_ptr<Item> newPresentationItem() {
        return newPresentationObject<Item>(*this, _services);
    }
    // Items serialized inside an owning inventory/equipment record use an
    // owner-local identity scope. Their saved ObjectId can legitimately match
    // another owned item or an independently registered world object, so the
    // runtime registry must assign them a fresh unambiguous identity.
    inline std::shared_ptr<Item> newOwnedItem() {
        return newItem();
    }
    std::shared_ptr<Item> newOwnedItem(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    std::shared_ptr<Item> newItem(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    std::shared_ptr<Item> newItemFromBlueprint(const std::string &resRef);
    std::shared_ptr<Item> newItemClone(const Item &source);

    inline std::shared_ptr<Area> newArea(std::string sceneName = kSceneMain) {
        return newObject<Area>(std::move(sceneName), *this, _services);
    }
    std::shared_ptr<Area> newSavedArea(
        uint32_t id,
        const SerializedIdentityContext &identityContext,
        std::string sceneName = kSceneMain);

    inline std::shared_ptr<Creature> newCreature(std::string sceneName = kSceneMain) {
        return newObject<Creature>(std::move(sceneName), *this, _services);
    }
    inline std::shared_ptr<Creature> newPresentationCreature(
        std::string sceneName) {
        return newPresentationObject<Creature>(
            std::move(sceneName), *this, _services);
    }
    std::shared_ptr<Creature> newCreature(const resource::Gff &gff, const SerializedIdentityContext &identityContext, std::string sceneName = kSceneMain);
    std::shared_ptr<Creature> newCreatureFromBlueprint(
        const std::string &resRef,
        std::string sceneName = kSceneMain);

    inline std::shared_ptr<Placeable> newPlaceable(std::string sceneName = kSceneMain) {
        return newObject<Placeable>(std::move(sceneName), *this, _services);
    }
    std::shared_ptr<Placeable> newPlaceable(const resource::Gff &gff, const SerializedIdentityContext &identityContext, std::string sceneName = kSceneMain);
    /** A placeable built in code, such as a body bag (no template file). */
    std::shared_ptr<Placeable> newPlaceableFromGff(
        const resource::Gff &gff,
        const std::string &identity,
        std::string sceneName = kSceneMain);
    std::shared_ptr<Placeable> newPlaceableFromBlueprint(
        const std::string &resRef,
        std::string sceneName = kSceneMain);

    inline std::shared_ptr<Door> newDoor(std::string sceneName = kSceneMain) {
        return newObject<Door>(std::move(sceneName), *this, _services);
    }
    std::shared_ptr<Door> newDoor(const resource::Gff &gff, const SerializedIdentityContext &identityContext, std::string sceneName = kSceneMain);

    inline std::shared_ptr<Waypoint> newWaypoint(std::string sceneName = kSceneMain) {
        return newObject<Waypoint>(std::move(sceneName), *this, _services);
    }
    std::shared_ptr<Waypoint> newWaypoint(const resource::Gff &gff, const SerializedIdentityContext &identityContext, std::string sceneName = kSceneMain);

    inline std::shared_ptr<Trigger> newTrigger(std::string sceneName = kSceneMain) {
        return newObject<Trigger>(std::move(sceneName), *this, _services);
    }
    std::shared_ptr<Trigger> newTrigger(const resource::Gff &gff, const SerializedIdentityContext &identityContext, std::string sceneName = kSceneMain);

    inline std::shared_ptr<Sound> newSound(std::string sceneName = kSceneMain) {
        return newObject<Sound>(std::move(sceneName), *this, _services);
    }
    std::shared_ptr<Sound> newSound(const resource::Gff &gff, const SerializedIdentityContext &identityContext, std::string sceneName = kSceneMain);

    inline std::shared_ptr<AnimatedCamera> newAnimatedCamera(std::string sceneName = kSceneMain) {
        return newObject<AnimatedCamera>(std::move(sceneName), *this, _services);
    }

    inline std::shared_ptr<DialogCamera> newDialogCamera(CameraStyle style, std::string sceneName = kSceneMain) {
        return newObject<DialogCamera>(std::move(style), std::move(sceneName), *this, _services);
    }

    inline std::shared_ptr<FirstPersonCamera> newFirstPersonCamera(float fovy, std::string sceneName = kSceneMain) {
        return newObject<FirstPersonCamera>(fovy, std::move(sceneName), *this, _services);
    }

    inline std::shared_ptr<StaticCamera> newStaticCamera(std::string sceneName = kSceneMain) {
        return newObject<StaticCamera>(std::move(sceneName), *this, _services);
    }

    inline std::shared_ptr<ThirdPersonCamera> newThirdPersonCamera(CameraStyle style, std::string sceneName = kSceneMain) {
        return newObject<ThirdPersonCamera>(std::move(style), std::move(sceneName), *this, _services);
    }

    inline std::shared_ptr<Encounter> newEncounter(std::string sceneName = kSceneMain) {
        return newObject<Encounter>(std::move(sceneName), *this, _services);
    }
    std::shared_ptr<Encounter> newEncounter(const resource::Gff &gff, const SerializedIdentityContext &identityContext, std::string sceneName = kSceneMain);

    inline std::shared_ptr<Store> newStore(std::string sceneName = kSceneMain) {
        return newObject<Store>(std::move(sceneName), *this, _services);
    }
    std::shared_ptr<Store> newStore(const resource::Gff &gff, const SerializedIdentityContext &identityContext, std::string sceneName = kSceneMain);

    void prepareSavedRuntimeNamespace(const resource::Gff &ifo, const SerializedIdentityContext &identityContext);
    void restoreWorldTime(
        const resource::Gff &moduleIfo,
        uint64_t pauseDay,
        uint64_t pauseTime);
    void reserveSavedObjectIds(const resource::Gff &gff, const SerializedIdentityContext &identityContext, SerializedGraphRoot graphRoot);
    void resolveSavedObjectReferences();
    void bindSavedRuntimeState();
    void publishSavedRuntimeState();

    /**
     * World time as absolute elapsed world/simulation milliseconds.
     *
     * This is the canonical runtime clock. It advances with simulation dt and
     * is never rescaled; Mod_MinPerHour only changes how the calendar divides
     * it. Retail saves store a day and a time of day instead, so that split
     * happens at the save and load boundaries and nowhere else.
     */
    uint64_t worldTimeMilliseconds() const { return _worldTimeMilliseconds; }

    uint8_t minutesPerHour() const { return _minutesPerHour; }

    /**
     * SetTime: moves the clock forward to this time of day, into the next
     * day when it is not later than now (design note c11_data, "Clock").
     */
    void setTimeOfDayForward(uint32_t timeOfDay);

    /**
     * Whether the world currently being built came from loading a save from
     * disk, as opposed to a new game or an ordinary module transition.
     *
     * Authored scripts read this to skip entry work - spawns, cutscenes,
     * destruction - whose results the save already holds. It is true only
     * while the initial module of a save load is being restored, so an
     * ordinary transition or a revisit to a module the save already knows
     * reports false.
     */
    bool isLoadingFromSaveGame() const { return _loadingFromSaveGame; }

    /**
     * Persist the current state of one roster NPC over its availnpc record.
     *
     * Scripts call this when a companion's state has to survive independently
     * of the party it is or is not currently in - the roster record is what a
     * later spawn reads back. Purely a write: membership, availability,
     * control and placement are all left alone, and a slot holding no creature
     * is silently nothing to save.
     */
    void saveNpcState(int npc);

    /** Persist one live creature as a detached PartyTable roster record. */
    void saveRosterState(
        const RosterIdentity &identity,
        const Creature &creature);

    /** Materialize an available, unbound slot from AVAILNPC/AVAILPUP. */
    std::shared_ptr<Creature> materializeRosterCreature(
        const RosterIdentity &identity);

    /** End a bound roster representation without changing availability. */
    bool killRosterCreature(const RosterIdentity &identity);

    /**
     * Length of a game day in world-time milliseconds.
     *
     * Mod_MinPerHour shortens the day - an in-game hour lasts that many
     * minutes of world time - it does not change the rate at which the clock
     * advances. A day lasts MinutesPerHour * 60 * 1000 * 24 world
     * milliseconds, and the clock accumulates elapsed time.
     */
    uint32_t millisecondsPerWorldDay() const {
        return static_cast<uint32_t>(_minutesPerHour == 0 ? 5 : _minutesPerHour) *
               60u * 1000u * 24u;
    }

    /**
     * Calendar day, derived from the canonical clock. Calendar time is a real
     * engine concept - day/night cycles, NPC schedules, waiting - and not just
     * a detail of the save format.
     */
    uint32_t worldTimeDay() const {
        return static_cast<uint32_t>(
            _worldTimeMilliseconds / millisecondsPerWorldDay());
    }

    /** World milliseconds elapsed within the current calendar day. */
    uint32_t worldTimeOfDay() const {
        return static_cast<uint32_t>(
            _worldTimeMilliseconds % millisecondsPerWorldDay());
    }
    std::optional<float> remainingEffectDuration(const EffectInstance &effect) const;

    template <class T>
    inline std::shared_ptr<T> getObjectById(uint32_t id) const {
        return std::dynamic_pointer_cast<T>(getObjectById(id));
    }

    template <class T, class... Args>
    inline std::shared_ptr<T> newObject(Args &&...args) {
        while (_objectById.count(_nextObjectId) ||
               _publishedRuntimeObjectIds.count(_nextObjectId) ||
               (_stagedRuntimeObjectGraph &&
                _stagedRuntimeObjectGraph->objectById.count(_nextObjectId)) ||
               _reservedSavedObjectIds.count(_nextObjectId)) {
            ++_nextObjectId;
        }
        return newObjectAtId<T>(_nextObjectId++, false, std::forward<Args>(args)...);
    }

    /** Construct a presentation-only object outside the gameplay registry. */
    template <class T, class... Args>
    inline std::shared_ptr<T> newPresentationObject(Args &&...args) {
        auto object = std::make_shared<T>(
            _nextPresentationObjectId--, std::forward<Args>(args)...);
        object->_runtimeState = Object::RuntimeState::Presentation;
        object->_runtimeIncarnation = _nextRuntimeIncarnation++;
        return object;
    }

    template <class T, class... Args>
    inline std::shared_ptr<T> newAction(Args &&...args) {
        return std::make_shared<T>(*this, _services, std::forward<Args>(args)...);
    }

    template <class T, class... Args>
    inline std::shared_ptr<T> newEffect(Args &&...args) {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }

    EffectId allocateEffectId() { return _effectIds.allocate(); }
    EffectIdImportResult importEffectId(EffectId id) { return _effectIds.importId(id); }
    bool setNextEffectId(EffectId id) { return _effectIds.setNextId(id); }
    EffectId nextEffectId() const { return _effectIds.nextId(); }
    bool hasEffectId(EffectId id) const { return _effectIds.contains(id); }
    size_t effectIdCount() const { return _effectIds.size(); }
    bool bindEffectCreator(EffectInstance &effect) const;
    bool bindSavedObjectReference(SavedObjectReference &reference) const;
    std::shared_ptr<Object> resolveSerializedObjectReference(
        uint32_t id,
        const SerializedIdentityContext &identityContext) const;
    std::shared_ptr<Object> getObjectBySavedId(uint32_t id) const;
    void registerSavedObjectIdentity(
        uint32_t id,
        const std::shared_ptr<Object> &object,
        const SerializedIdentityContext &identityContext);

    const SaveResourceShadows &saveResourceShadows() const {
        return _saveResourceShadows;
    }
    void captureSaveResourceShadow(
        SaveResourceKey key,
        const resource::Gff &source) {
        _saveResourceShadows.capture(std::move(key), source);
    }

    template <class... Args>
    inline std::shared_ptr<Event> newEvent(Args &&...args) {
        return std::make_shared<Event>(std::forward<Args>(args)...);
    }

    template <class... Args>
    inline std::shared_ptr<Location> newLocation(Args &&...args) {
        return std::make_shared<Location>(std::forward<Args>(args)...);
    }

    template <class... Args>
    inline std::shared_ptr<Talent> newTalent(Args &&...args) {
        return std::make_shared<Talent>(std::forward<Args>(args)...);
    }

    // END Objects

    // Global variables

    bool getGlobalBoolean(const std::string &name) const;
    int getGlobalNumber(const std::string &name) const;
    std::shared_ptr<Location> getGlobalLocation(const std::string &name) const;
    std::string getGlobalString(const std::string &name) const;

    struct GVCompare {
        bool operator()(const std::string &lhs, const std::string &rhs) const {
            return boost::algorithm::ilexicographical_compare(lhs, rhs);
        }
    };

    const std::map<std::string, std::string, GVCompare> &globalStrings() const { return _globalStrings; }
    const std::map<std::string, bool, GVCompare> &globalBooleans() const { return _globalBooleans; }
    const std::map<std::string, int, GVCompare> &globalNumbers() const { return _globalNumbers; }
    const std::map<std::string, std::shared_ptr<Location>, GVCompare> &globalLocations() const { return _globalLocations; }

    void setCustomToken(int token, std::string value);
    std::string substituteCustomTokens(std::string str) const;
    std::string substituteCustomToken(std::string str, int token, std::string value) const;

    void setGlobalBoolean(const std::string &name, bool value);
    void setGlobalLocation(const std::string &name, const std::shared_ptr<Location> &location);
    void setGlobalNumber(const std::string &name, int value);
    void setGlobalString(const std::string &name, const std::string &value);

    // END Global variables

    std::map<int, std::string> parseCustomTokens(
        const resource::Gff &ifoGff) const;
    void replaceCustomTokens(std::map<int, std::string> tokens);
    PreparedSaveLoad prepareSaveLoad(const resource::SaveSlotDescriptor &slot);
    PreparedDestinationModule prepareDestinationModule(
        const std::string &name,
        bool initialSaveRestore,
        std::shared_ptr<const resource::SaveWorkingState> workingState);
    bool restoreSaveLoad(PreparedSaveLoad prepared);
    bool loadPreparedModule(
        PreparedDestinationModule prepared,
        std::string entry,
        bool initialSaveRestore,
        bool resourcesCommitted,
        std::shared_ptr<const resource::SaveWorkingState> sourceWorkingState = nullptr);
    void validatePreparedDestination(
        const PreparedDestinationModule &prepared) const;
    void validatePartyLoad(const resource::Gff *partyTable) const;
    void retireToMainMenu();

    void deserializeGlobalVariables(resource::Gff &gvtGff);
    void deserializeParty(
        resource::Gff &ifoGff,
        const std::shared_ptr<resource::Gff> &ptGff,
        const SerializedIdentityContext &moduleIdentityContext);
    void publishPartyRuntimeState(
        resource::Gff &ifoGff,
        const std::shared_ptr<resource::Gff> &ptGff,
        const std::shared_ptr<resource::Gff> &pcGff,
        const SerializedIdentityContext &moduleIdentityContext);
    Party::PersistedState parsePartyTable(const resource::Gff &ptGff) const;
    void replacePartyTable(Party::PersistedState state);
    void deserializePazaakPartyTable(resource::Gff &ptGff);
    void deserializeGalaxyMap(resource::Gff &ptGff);
    void resetGalaxyMap();
    void serializePazaakPartyTable(resource::Gff &ptGff) const;
    void deserializePartyMembers(resource::Gff &ptGff);
    void deserializeJournal(const resource::Gff &ptGff);
    void deserializeInventory(resource::Gff &inventoryGff);

private:
    friend class Area;
    friend class Object;
    friend class CoopSession;
    friend class TestGameModule;
    friend class ModuleSnapshotBuilder;
    friend class SaveWideSnapshotBuilder;
    friend struct SerializedScriptSituation;
    friend class SavedScriptContinuation;

    resource::GameID _gameId;
    std::filesystem::path _path;
    OptionsView &_options;
    ServicesView &_services;
    IConsole &_console;

    Screen _screen {Screen::None};

    struct DeveloperOverlay {
        bool visible {false};
        bool triggers {true};
        bool actorLabels {true};
        bool longActorLabels {false};
        bool watchedValues {true};
    };

    DeveloperOverlay _developerOverlay;
    std::shared_ptr<graphics::Font> _developerFont;

    // Minigame runtime. The session is the simulation; _miniGameHost runs its
    // scripts and forwards presentation calls to the swoop or turret view.
    std::unique_ptr<IMiniGameHost> _miniGameHost;
    std::unique_ptr<MiniGameSession> _miniGame;
    std::vector<std::shared_ptr<Object>> _miniGameObjects;
    MiniGameInput _miniGameInput;
    bool _swoopAutopilot {false};
    bool _turretAutopilot {false};
    bool _instantDoorTransitions {false};
    std::string _miniGameModule;

    std::shared_ptr<movie::IMovie> _movie;
    std::queue<std::string> _moduleTransitionMovies;
    std::shared_ptr<PresentationPointer> _pointer;
    float _gameSpeed {1.0f};
    CameraType _cameraType {CameraType::ThirdPerson};
    CameraType _savedCameraType {CameraType::ThirdPerson};
    bool _paused {false};
    uint64_t _clicksBlockedUntilMilliseconds {0};
    bool _timingDiscontinuity {false};
    GlobalFade _globalFade;
    GlobalFade _dialogFade;
    VideoEffect _videoEffect;
    std::deque<SoundCue> _soundCues;
    float _ambientRoomScale {1.0f};
    void updateAmbientRoomScale(float dt);

    // Co-op (coopgame.cpp)
    std::shared_ptr<CoopSession> _coop;
    std::shared_ptr<CoopLaunch> _coopDefaults; // shared: CoopLaunch is incomplete here
    std::shared_ptr<std::ofstream> _coopRecord;
    double _coopRecordClock {0.0};
    double _coopRecordNext {0.0};
    void recordCoopState(float frameTime);
    void tickCoopClient(float dt);
    void initCoopConsole();

public:
    float ambientRoomScale() const { return _ambientRoomScale; }

private:
    uint32_t _autoCombatScriptCaller {script::kObjectInvalid};
    bool _scriptVideoEffect {false};
    void setVideoEffectRow(int row);
    GlobalFade::ArrivalTicket _fadeArrival;
    std::weak_ptr<Module> _fadeArrivalModule;
    std::set<std::string> _moduleNames;
    std::set<std::string> _saveNames;
    bool _quitRequested {false};
    bool _relativeMouseMode {false};
    bool _showImGui {false};

    static constexpr uint32_t kFirstRuntimeObjectId = 2; // ids 0 and 1 are reserved
    uint32_t _nextObjectId {kFirstRuntimeObjectId};
    std::map<uint32_t, std::shared_ptr<Object>> _objectById;
    // A numeric runtime ID names at most one incarnation during a runtime
    // session. Saved cursors and developer-specified IDs may move allocation
    // backwards, but cannot revive stale numeric gameplay references.
    std::set<uint32_t> _publishedRuntimeObjectIds;
    std::map<uint32_t, std::weak_ptr<Object>> _objectBySavedId;
    // One authoritative graph object has one canonical saved identity. Roster
    // doubles live in detached graphs and never create cross-graph aliases.
    std::map<const Object *, uint32_t> _savedIdByObject;
    struct StagedRuntimeObjectGraph {
        uint32_t initialNextObjectId {kFirstRuntimeObjectId};
        std::map<uint32_t, std::shared_ptr<Object>> objectById;
        std::set<uint32_t> publishedRuntimeObjectIds;
        std::map<uint32_t, std::weak_ptr<Object>> objectBySavedId;
        std::map<const Object *, uint32_t> savedIdByObject;
        std::set<const Object *> replaceableObjects;
        std::set<uint32_t> reservedSavedObjectIdsToRelease;
        std::vector<std::shared_ptr<Object>> obsoleteGraph;
        std::vector<std::shared_ptr<Object>> candidateObjects;
    };
    std::optional<StagedRuntimeObjectGraph> _stagedRuntimeObjectGraph;
    uint64_t _nextRuntimeIncarnation {1};
    uint32_t _nextPresentationObjectId {
        std::numeric_limits<uint32_t>::max() - 1};
    std::set<uint32_t> _reservedSavedObjectIds;
    std::optional<std::string> _reservedSavedIdentityNamespace;
    std::map<uint32_t, std::string> _reservedSavedObjectIdClaims;
    EffectIdNamespace _effectIds;
    bool _runtimeSessionPlayable {false};
    bool _cheatUsed {false};
    uint64_t _runtimeSessionGeneration {1};
    uint64_t _savedGraphGeneration {1};
    bool _loadingFromSaveGame {false};
    uint64_t _worldTimeMilliseconds {0};
    uint64_t _frameCount {0};
    FixedStepClock _simClock;
    uint64_t _tickCount {0};
    bool _tickGate {false};
    uint64_t _tickAllowance {0};
    // A harness wait measured in ticks (step, stepidle, untiltick). Checked
    // after every tick, so it ends on an exact tick at any frame rate.
    struct TickWait {
        uint64_t untilTick {0};
        std::function<bool()> done;
        bool finished {true};
        uint64_t startTick {0};
        std::string label;
    };
    TickWait _tickWait;
    uint8_t _minutesPerHour {5};
    double _worldTimeFraction {0.0};
    // The first fresh module after a new game or load starts the clock from
    // its IFO start fields.
    bool _clockStartPending {true};
    double _playedTimeFraction {0.0};

    std::optional<SaveRequest> _pendingSave;
    std::optional<SaveResult> _lastSaveResult;
    uint64_t _nextSaveRequestId {1};
    bool _saveInProgress {false};
    bool _transitionInProgress {false};
    bool _atStableSavePoint {false};
    SaveOrchestrationSeams _saveSeams;
    graphics::Texture *_lastRenderedSceneOutput {nullptr};

    // Services

    Party _party;
    Combat _combat;
    Regeneration _regeneration;
    SwoopRace _swoopRace;
    Turret _turret;
    Journal _journal;
    MessageLog _messageLog;
    FloatingText _floatingText;
    VisualEffectPresenter _visualEffectPresenter;
    StatusSummaryAccumulator _statusSummary;
    DialogLocks _dialogLocks;
    std::set<int> _tutorialWindowsShown;

    std::unique_ptr<script::IRoutines> _routines;
    std::unique_ptr<script::IRoutines> _k2Routines; // K2 systems merge only
    std::unique_ptr<ScriptRunner> _scriptRunner;
    ScriptEventQueue _scriptEvents {*this};

    // END Services

    // GUI

    std::unique_ptr<MainMenu> _mainMenu;
    std::unique_ptr<CharacterGeneration> _charGen;
    std::unique_ptr<HUD> _hud;
    bool _captureHUDPresentation {false};
    std::unique_ptr<InGameMenu> _inGame;
    std::unique_ptr<DialogGUI> _dialog;
    std::unique_ptr<ComputerGUI> _computer;
    std::unique_ptr<ConfirmPopup> _confirmPopup;
    bool _tutorialWindowsEnabled {true};
    std::string _lastConversation;
    std::unique_ptr<ContainerGUI> _container;
    std::unique_ptr<PartySelection> _partySelect;
    std::unique_ptr<SaveLoad> _saveLoad;
    std::unique_ptr<GalaxyMap> _galaxyMap;
    std::unique_ptr<PazaakWagerGUI> _pazaakWager;
    std::unique_ptr<PazaakSetupGUI> _pazaakSetup;
    std::unique_ptr<PazaakBoardGUI> _pazaakBoard;
    std::unique_ptr<StoreGUI> _store;
    std::unique_ptr<UpgradeGUI> _upgrade;
    std::unique_ptr<CreditsGUI> _credits;

    StoreSession _storeSession {*this};
    UserSettings _userSettings;
    Keymap _keymap;
    float _selectionRadius {kDefaultSelectionRadius};
    bool _fixedActionBar {true};
    // The command-line values settings fall back to when a key is unset.
    struct LaunchOptions {
        audio::AudioOptions audio;
        uint8_t difficulty {1};
        bool grass {true};
        bool shadows {true};
        bool vsync {true};
        bool smartCompanions {false};
        graphics::GraphicsOptions graphics; // Enhanced preset settings at launch
    } _launchOptions;

    void loadUserSettings();
    /** Keys the in-game keymap dispatches itself (design note p1_ui, "Key bindings"). */
    bool dispatchKeyAction(KeyAction action);
    /** Escape: close the screen, or open the game menu. */
    bool handleEscape();

    // Screens a script asked for; the next tick opens them (design note gui_screens).
    struct PendingStore {
        RuntimeObjectRef<Store> store;
        RuntimeObjectRef<Creature> customer;
        int bonusMarkUp {0};
        int bonusMarkDown {0};
    };
    std::optional<PendingStore> _pendingStore;
    std::optional<RuntimeObjectRef<Item>> _pendingUpgrade;
    bool _pendingLevelUp {false};
    Screen _creditsOriginScreen {Screen::None};

    struct QueuedMovie {
        std::string name;
        bool skippable {true};
    };
    std::vector<QueuedMovie> _scriptMovieQueue;
    std::deque<QueuedMovie> _playingMovieQueue;
    // Skip behaviour of the movie playing now and the queue behind it.
    bool _movieSkippable {true};
    bool _movieQueueSeparateSkips {true};
    bool playNextQueuedMovie();

    void processPendingScreens();
    bool pendingScreenMayOpen() const;
    void skipMovie();

    std::unique_ptr<PazaakSession> _pazaakSession;
    std::optional<PazaakCompletedResult> _lastPazaakResult;
    RuntimeObjectRef<Object> _pazaakContinuationCaller;
    Screen _pazaakOriginScreen {Screen::None};
    bool _pazaakGUIsReady {false};
    bool _pazaakDevelopmentLaunch {false};
    bool _pazaakSelectionPersisted {false};
    bool _pazaakSettlementApplied {false};
    bool _pazaakShowcaseHands {false};
    float _pazaakOpponentEventElapsed {0.0f};

    // Narrow injectable seams used by focused lifecycle tests.
    PazaakSession::HandSelector _pazaakPlayerHandSelector;
    PazaakSession::HandSelector _pazaakOpponentHandSelector;
    PazaakSession::MainDeckFactory _pazaakMainDeckFactory;
    PazaakSession::FirstParticipantSelector _pazaakFirstParticipantSelector;
    bool _pazaakPaceAutomaticDraws {true};
    std::function<bool()> _pazaakGuiLoadOverride;
    std::function<void(const std::string &, uint32_t)> _pazaakContinuationOverride;
    RuntimeObjectRef<Object> _pazaakDevelopmentSelectedObjectOverride;
    std::optional<pazaak::SideDeck> _pazaakOpponentDeckOverride;

    std::unique_ptr<Map> _map;
    std::unique_ptr<LoadingScreen> _loadScreen;

    Conversation *_conversation {nullptr}; /**< pointer to either DialogGUI or ComputerGUI  */
    Conversation::AutoSkip _conversationAutoSkip;

    // END GUI

    // Modules

    std::string _nextModule;
    bool _endGameRequested {false};
    std::string _nextEntry;
    std::shared_ptr<Module> _module;
    std::map<std::string, std::shared_ptr<Module>> _loadedModules;

    // END Modules

    // Audio

    std::string _musicResRef;
    std::shared_ptr<audio::AudioSource> _music;
    /// Clips the current track is read from; null for the main clips.
    resource::IAudioClips *_musicClips {nullptr};
    float _musicVolumeScale {1.0f}; /**< SetMusicVolume */

    // The area's ambient bed (a looping sound, volume 0-100).
    std::string _ambientResRef;
    int _ambientVolume {0};
    std::shared_ptr<audio::AudioSource> _ambient;

    // Sources fading out on wall time, then stopped.
    struct FadingSource {
        std::shared_ptr<audio::AudioSource> source;
        float total {0.0f};
        float left {0.0f};
        float from {1.0f};
    };
    std::vector<FadingSource> _fadingAudio;

    // END Audio

    // Global variables

    std::map<std::string, std::string, GVCompare> _globalStrings;
    std::map<std::string, bool, GVCompare> _globalBooleans;
    std::map<std::string, int, GVCompare> _globalNumbers;
    std::map<std::string, std::shared_ptr<Location>, GVCompare> _globalLocations;
    std::map<int, std::string> _customTokens;
    bool _partyDefeatPending {false};
    std::function<void()> _partyDefeatHandler;
    bool _autosaveOnEnterPending {false};
    bool _checkpointOnEnterPending {false}; // the area checkpoint, after any retail autosave
    void processCheckpointOnEnter();
    std::optional<std::chrono::steady_clock::time_point> _lastSaveClock;
    void queueAutosaveOnEnter(const std::string &moduleName);
    void deleteSaveGroupOnEnter(const std::string &moduleName);
    void processAutosaveOnEnter();
    mutable StringTokens _stringTokens;
    mutable bool _stringTokensLoaded {false};

    StringTokenSubject stringTokenSubject() const;
    std::string resolveTokens(const std::string &str, const std::map<int, std::string> &customTokens) const;
    SaveResourceShadows _saveResourceShadows;

    // END Global variables

    void stopMovement();

    void advanceWorldTime(float dt);
    void advancePlayedTime(float dt);
    std::shared_ptr<const resource::SaveWorkingState>
    prepareCurrentModuleWorkingState();
    void processPendingSave();
    void finalizeSaveRequest(const SaveRequest &request, SaveResult result);
    SaveResult executeSave(SaveRequest request);
    SaveMetadataInput buildSaveMetadata(const SaveRequest &request) const;
    resource::SaveSlotDescriptor saveTarget(const SaveRequest &request) const;
    std::map<std::string, ByteBuffer> currentLooseSavePassthrough() const;
    /** Host: read the co-op loot ledger from the loaded slot, if it has one. */
    void restoreCoopLootFromSave();
    std::optional<ByteBuffer> captureSaveScreenshot();

    uint32_t savedObjectId(const resource::Gff &gff) const;
    void retireSavedObjectGraph();
    void registerSavedModuleReferenceTarget(
        const std::shared_ptr<Module> &module,
        const SerializedIdentityContext &identityContext);
    void registerObject(
        const std::shared_ptr<Object> &object,
        bool allowReserved);
    void beginRuntimeObjectGraphReplacement(
        const std::vector<std::shared_ptr<Object>> &obsoleteObjects);
    void commitRuntimeObjectGraphReplacement(
        const std::vector<std::shared_ptr<Object>> &obsoleteObjects);
    void abortRuntimeObjectGraphReplacement();
    void unregisterRuntimeObject(const std::shared_ptr<Object> &object);
    bool isRuntimeObjectAttachable(const Object &object) const;
    std::vector<std::shared_ptr<Object>> collectRuntimeObjectGraph(
        const std::vector<std::shared_ptr<Object>> &roots) const;
    void discardStagedRuntimeObjects(
        const std::vector<std::shared_ptr<Object>> &objects);
    void retireActiveAreaRuntime();

    template <class T, class... Args>
    inline std::shared_ptr<T> newObjectAtId(
        uint32_t id,
        bool allowReserved,
        Args &&...args) {
        auto object = std::make_shared<T>(id, std::forward<Args>(args)...);
        registerObject(object, allowReserved);
        return object;
    }

    template <class T, class... Args>
    inline std::shared_ptr<T> newObjectFromGff(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext,
        Args &&...args) {
        std::vector<std::shared_ptr<Object>> noObsolete;
        std::shared_ptr<T> object;
        replaceRuntimeObjectGraph(
            noObsolete,
            [&]() {
                object = newObject<T>(std::forward<Args>(args)...);
                if (identityContext.hasAuthoritativeObjectIds()) {
                    registerSavedObjectIdentity(
                        savedObjectId(gff), object, identityContext);
                }
                object->deserialize(gff, identityContext);
            },
            []() noexcept {});
        return object;
    }
    void loadDefaultParty();
    bool loadParty();
    void loadNextModule();
    void playMusic(const std::string &resRef, resource::IAudioClips *clips = nullptr);
    std::unique_ptr<MainMenu> newThemedMainMenu(bool advanceTheme = true);
    int _menuIdleSwitches {0};
    void switchMainMenuTheme();
    void playAmbient(const std::string &resRef, int volume);
    void fadeOutAudio(std::shared_ptr<audio::AudioSource> source, float seconds);
    void toggleInGameCameraType();

    // One minigame tick: autopilot, presentation inputs, the session step and
    // the minigame objects' own action queues (DelayCommand).
    void tickMiniGame(float dt);
    void applySwoopAutopilot();
    void applyTurretAutopilot();

    // Show/hide the active party creatures. Used to suppress the normal party
    // while a minigame is running: vanilla does not add the party to the scene
    // in a minigame module (the minigame actor represents the player).
    void setPartyVisible(bool visible);

    bool handleKeyDown(const input::KeyEvent &event);
    bool handleMouseMotion(const input::MouseMotionEvent &event);
    bool handleMouseButtonDown(const input::MouseButtonEvent &event);
    bool handleMouseButtonUp(const input::MouseButtonEvent &event);
    bool handleDeveloperKeyDown(const input::KeyEvent &event);

    void onModuleSelected(const std::string &name);
    void renderHUD();

    GameGUI *getScreenGUI() const;
    CameraType getConversationCamera(int &cameraId) const;

    // Updates

    bool startVideo(const std::string &name);
    bool playNextModuleTransitionMovie();
    void updateMovie(float dt);
    void updateMusic(float dt);
    void tick(float dt);
    // Whether the module simulation advances while this screen is shown.
    bool worldRunsOnScreen() const;
    void checkTickWait();
    void beginTickWait(std::string label, uint64_t untilTick, std::function<bool()> done);
    void updateCameraType();
    void updateCamera(float dt);
    void updateSceneGraph(float dt);
    void updateVisualEffects(float dt);
    void updateImGui(float dt);

    // END Updates

    // Rendering

    void renderScene();
    /** ReShade-friendly mode: the main scene's opaque depth into the window's depth buffer. */
    void writeSceneDepthToWindow();
    /** Reflections: the area whose probe was captured, and when it is due again. */
    std::string _reflectionProbeArea;
    void captureReflectionProbeIfDue();
    /** Conversation depth of field: focus on the speaker, eased in and out (presentation only). */
    void updateConversationFocus();
    float _dofFrameTime {0.0f};
    void renderGUI();
    void renderFade(const GlobalFade &fade);
    void settleFadeArrival();
    void renderDeveloperOverlay();
    void renderDeveloperBanner();
    void renderDeveloperTriggerOverlay(const glm::mat4 &projection, const glm::mat4 &view);
    void renderDeveloperActorLabels(const glm::mat4 &projection, const glm::mat4 &view);
    void renderDeveloperWatchedValues();
    void renderDeveloperText(const std::string &text, const glm::vec3 &position, const glm::vec3 &color, graphics::TextGravity gravity = graphics::TextGravity::LeftTop);
    void renderDeveloperPanel(const std::vector<std::string> &lines, glm::vec2 position, glm::vec3 color);
    void renderDeveloperRect(glm::vec2 position, glm::vec2 size, glm::vec4 color);

    // END Rendering

    // GUI

    void loadInGameMenus();
    bool loadPazaakGUIs();
    bool startPazaakFlow(
        PazaakSessionParams params,
        const std::shared_ptr<Object> &continuationCaller,
        bool developmentLaunch);
    bool startDevelopmentPazaak(std::string opponentName, int maximumWager = 0);
    void finishPazaak(PazaakCompletedResult result);
    void releasePazaakFlow(bool restoreOrigin);
    Screen safePazaakOriginScreen() const;

    void changeScreen(Screen screen);

    void withLoadingScreen(const std::string &imageResRef, const std::function<void()> &block);

    template <class T>
    std::unique_ptr<T> tryLoadGUI() {
        auto gui = std::make_unique<T>(*this, _services);
        try {
            gui->init();
            return gui;
        } catch (const std::exception &e) {
            error(str(boost::format("Error loading GUI: %s") % std::string(e.what())));
            return nullptr;
        }
    }

    // END GUI

    // Console commands

    void initConsole();
    void initJournalNotifications();
    int getPlotXPByIndex(int plotIndex);

    using ConsoleCommandHandler = void (Game::*)(const ConsoleArgs &);
    void registerConsoleCommand(std::string name, std::string description, ConsoleCommandHandler handler);

    /**
     * Returns the currently selected object, or the party leader.
     */
    std::shared_ptr<Object> getConsoleTargetObject();

    /**
     * Returns the currently selected Creature object, or the party leader.
     */
    std::shared_ptr<Creature> getConsoleTargetCreature();

    /**
     * Returns the leader creature.
     */
    std::shared_ptr<Creature> getConsoleLeader();

    /**
     * Returns the current Area.
     */
    std::shared_ptr<Area> getConsoleArea();

    void consoleInfo(const ConsoleArgs &tokens);
    void consoleListGlobals(const ConsoleArgs &tokens);
    void consoleListLocals(const ConsoleArgs &tokens);
    void consoleListAnim(const ConsoleArgs &tokens);
    void consolePlayAnim(const ConsoleArgs &tokens);
    void consoleKill(const ConsoleArgs &tokens);
    void consoleAddItem(const ConsoleArgs &tokens);
    void consoleGiveXP(const ConsoleArgs &tokens);
    void consoleGiveGold(const ConsoleArgs &tokens);
    void consoleWarp(const ConsoleArgs &tokens);
    void consoleCamera(const ConsoleArgs &tokens);
    void consoleCamPos(const ConsoleArgs &tokens);
    void consoleCamLook(const ConsoleArgs &tokens);
    void consoleCamStatus(const ConsoleArgs &tokens);
    void consoleOpenMenu(const ConsoleArgs &tokens);
    void consoleOpenCharacterGeneration(const ConsoleArgs &tokens);
    void consoleSkipMovie(const ConsoleArgs &tokens);
    void consoleShowBark(const ConsoleArgs &tokens);
    void consoleShowPopup(const ConsoleArgs &tokens);
    void consoleShowGalleryMode(const ConsoleArgs &tokens);
    void consoleSeed(const ConsoleArgs &tokens);
    void consoleGraphics(const ConsoleArgs &tokens);
    void consoleShowHUD(const ConsoleArgs &tokens);
    void consoleShowTransition(const ConsoleArgs &tokens);
    void consoleOpenContainer(const ConsoleArgs &tokens);
    void consoleSelectDialogOption(const ConsoleArgs &tokens);
    void consoleRunScript(const ConsoleArgs &tokens);
    void consoleShowAABB(const ConsoleArgs &tokens);
    void consoleShowWalkmesh(const ConsoleArgs &tokens);
    void consoleShowTriggers(const ConsoleArgs &tokens);
    void consoleSpawnCreature(const ConsoleArgs &tokens);
    void consoleSpawnCompanion(const ConsoleArgs &tokens);
    void consoleAddAvailableNpc(const ConsoleArgs &tokens);
    void consoleSelectObjectById(const ConsoleArgs &tokens);
    void consoleSelectObjectByTag(const ConsoleArgs &tokens);
    void consoleSelectLeader(const ConsoleArgs &tokens);
    void consoleSetFaction(const ConsoleArgs &tokens);
    void consoleSetPosition(const ConsoleArgs &tokens);
    void consoleProfessionalTools(const ConsoleArgs &tokens);
    void consoleKillRoom(const ConsoleArgs &tokens);
    void consoleAutoSkipEnable(const ConsoleArgs &tokens);
    void consoleAutoSkipEntries(const ConsoleArgs &tokens);
    void consoleAutoSkipReplies(const ConsoleArgs &tokens);
    void consoleStartConversation(const ConsoleArgs &tokens);
    void consoleCutsceneAttack(const ConsoleArgs &tokens);
    void consoleSetAbility(const ConsoleArgs &tokens);
    void consoleSetSkill(const ConsoleArgs &tokens);
    void consoleAddOrRemoveFeat(const ConsoleArgs &tokens);
    void consoleAddOrRemoveSpell(const ConsoleArgs &tokens);
    void consoleCastSpellAtObject(const ConsoleArgs &tokens);
    void consoleOpenCloseDoor(const ConsoleArgs &tokens);
    void consoleListGames(const ConsoleArgs &tokens);
    void consoleLoadGame(const ConsoleArgs &tokens);
    void consoleSaveGame(const ConsoleArgs &tokens);
    void consoleStartPazaak(const ConsoleArgs &tokens);
    void consoleMiniGameInfo(const ConsoleArgs &tokens);
    void consoleMiniGameState(const ConsoleArgs &tokens);
    void consoleSetGlobal(const ConsoleArgs &tokens);
    void consoleMiniGameInputCommand(const ConsoleArgs &tokens);
    void consoleSwoopAuto(const ConsoleArgs &tokens);
    void consoleTurretAuto(const ConsoleArgs &tokens);
    void consolePazaakState(const ConsoleArgs &tokens);
    void consolePazaakWager(const ConsoleArgs &tokens);
    void consolePazaakDeck(const ConsoleArgs &tokens);
    void consolePazaakPlay(const ConsoleArgs &tokens);
    void consolePazaakEnd(const ConsoleArgs &tokens);
    void consolePazaakStand(const ConsoleArgs &tokens);
    void consolePazaakForfeit(const ConsoleArgs &tokens);
    void consoleShowImGui(const ConsoleArgs &tokens);
    void consoleShowPath(const ConsoleArgs &tokens);
    void consoleDumpState(const ConsoleArgs &tokens);
    void consoleTickGate(const ConsoleArgs &tokens);
    void consoleStep(const ConsoleArgs &tokens);
    void consoleStepIdle(const ConsoleArgs &tokens);
    void consoleSpawnFollowers(const ConsoleArgs &tokens);
    void consoleSetVitals(const ConsoleArgs &tokens);
    void consoleUntilTick(const ConsoleArgs &tokens);
    void consoleTickStatus(const ConsoleArgs &tokens);
    void consoleInterp(const ConsoleArgs &tokens);

    // Render interpolation (design note render_interp)

    struct InterpTraceSample {
        uint64_t tick {0};
        float alpha {1.0f};
        double time {0.0};
        glm::vec3 drawn {0.0f};
        glm::vec3 simulated {0.0f};
    };
    uint32_t _interpTraceObject {0};
    size_t _interpTraceLimit {0};
    /** interp headless 1: blend in a headless run too, as a player's window does. */
    bool _interpHeadless {false};
    std::vector<InterpTraceSample> _interpTrace;

    void presentInterpolation();
    void recordInterpTrace(float alpha);
    void consoleCosmeticSeed(const ConsoleArgs &tokens);
    void consoleDumpObject(const ConsoleArgs &tokens);
    void consoleNewGame(const ConsoleArgs &tokens);

    // Harness movement and interaction. These queue the same actions a
    // player's click does, rather than teleporting or bypassing the queue.

    std::function<bool()> _commandBlocker;
    // Frames the latest waitidle waited, for idlestatus.
    int _waitIdleFrames {0};

    std::shared_ptr<Object> findConsoleObject(std::string_view idOrTag);
    // The creature an @id|tag selector names, or the party leader.
    std::shared_ptr<Creature> findConsoleActor(const std::optional<std::string> &idOrTag);

    void consoleMoveTo(const ConsoleArgs &tokens);
    void consoleMoveToObject(const ConsoleArgs &tokens);
    void consoleClick(const ConsoleArgs &tokens);
    void consoleClearActions(const ConsoleArgs &tokens);
    void consoleCancelCombat(const ConsoleArgs &tokens);
    void consoleInstantDoors(const ConsoleArgs &tokens);
    void consoleAutoCombat(const ConsoleArgs &tokens);
    void consoleWaitIdle(const ConsoleArgs &tokens);
    void consoleIdleStatus(const ConsoleArgs &tokens);
    void consoleCloseScreen(const ConsoleArgs &tokens);
    void consolePickReply(const ConsoleArgs &tokens);
    void consoleTakeAll(const ConsoleArgs &tokens);
    void consoleContainerGive(const ConsoleArgs &tokens);
    void consoleEquipAuto(const ConsoleArgs &tokens);
    void consoleEquipSlot(const ConsoleArgs &tokens);
    void consoleSwapWeapons(const ConsoleArgs &tokens);
    void consolePartySelect(const ConsoleArgs &tokens);
    void consoleSwitchLeader(const ConsoleArgs &tokens);
    void consoleUseSkill(const ConsoleArgs &tokens);
    void consoleGiveLoot(const ConsoleArgs &tokens);
    void consoleVfx(const ConsoleArgs &tokens);
    void consoleUseHealItem(const ConsoleArgs &tokens);
    void consoleUseDefensiveItem(const ConsoleArgs &tokens);
    void consoleUseAttackItem(const ConsoleArgs &tokens);
    void consoleLevelUp(const ConsoleArgs &tokens);
    void consoleK2Merge(const ConsoleArgs &tokens);
    void consoleForm(const ConsoleArgs &tokens);
    void consoleStore(const ConsoleArgs &tokens);
    void consoleUpgrade(const ConsoleArgs &tokens);
    void consoleCredits(const ConsoleArgs &tokens);
    void consoleOptions(const ConsoleArgs &tokens);
    void consoleShowLoadScreen(const ConsoleArgs &tokens);
    void consoleKey(const ConsoleArgs &tokens);
    void consoleGuiClick(const ConsoleArgs &tokens);
    void consoleGuiDump(const ConsoleArgs &tokens);
    void consoleGuiShow(const ConsoleArgs &tokens);

    // END Console commands
};

} // namespace game

} // namespace reone
