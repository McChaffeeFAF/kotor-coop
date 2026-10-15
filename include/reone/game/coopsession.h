/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <boost/json.hpp>

#include "reone/net/protocol.h"
#include "reone/net/transport.h"

#include "coopcommand.h"
#include "cooploot.h"
#include "coopflow.h"
#include "coopminigame.h"
#include "coopidmap.h"
#include "guestbenchscreen.h"
#include "visualeffectpresenter.h"

namespace reone {

namespace scene {
class ModelSceneNode;
}

namespace game {

class Area;
struct Character;
struct ContextAction;
class Creature;
class CreatureAttributes;
class Item;
class Game;
class Module;
class Object;
class Projectile;

/** Free Mode: guests level up to the game's cap, independently of the host. */
constexpr int kCoopGuestLevelCap = 50;

enum class CoopRole {
    None,
    Host,
    Client
};

const char *coopRoleName(CoopRole role);

struct CoopConfig {
    CoopRole role {CoopRole::None};
    std::string mode {"story"};
    std::string playerName;
    std::string preferredTag; // client: companion wanted; empty for any
    std::string build;        // must match the peer's exactly
    net::Fingerprint k1;
    net::Fingerprint k2;
    int maxClients {2}; // Story Mode: the host plus two companions
    // Host options (co-op design notes)
    bool hostOnlyTransitions {false}; // only the host's exit moves the party
    bool hostOnlyTrading {false};     // Story Mode: only the host may open stores
    bool noPause {false};             // "No pause in co-op"
    float pauseCooldown {0.0f};       // seconds between one player's pauses
    float enemyScaling {1.0f};        // strength of the per-player enemy curve; 0 off
    // Guest minigames (swoop races a guest starts, turrets the host hands over)
    bool guestMinigames {true};            // Free Mode: a guest may start a minigame from an NPC's prompt
    int guestRacePrize {100};              // credits a swoop race a guest finishes pays that guest
    bool guestMinigamesAffectStory {false}; // keep the story changes a guest's minigame scripts make (off: undone)
    // Host: every new player waits for the host's Accept / Decline (design,
    // "Handshake" step 4). Off in headless runs unless asked for; a player
    // rejoining with their rejoin token is let back in without asking.
    bool askToJoin {false};
    // Client: where the rejoin token is kept so a restarted game can reclaim
    // its companion (coop_rejoin.json next to reone.cfg), and what names the
    // session it belongs to (the join code, or the host address). Either empty
    // keeps the token in memory only.
    std::filesystem::path rejoinStore;
    std::string sessionKey;
};

/** How long a saved rejoin token stays good. */
constexpr int64_t kRejoinTokenLifetimeSeconds = 2 * 60 * 60;

/**
 * The client's saved rejoin tokens, one per session and player name. The host
 * keeps the matching table in memory, so a token only works for the host
 * process that issued it; a stranger who reuses the name does not have it.
 * Expired entries are dropped when read. `now` is Unix seconds.
 */
uint64_t loadRejoinToken(const std::filesystem::path &store, const std::string &sessionKey, const std::string &playerName, int64_t now);
void saveRejoinToken(const std::filesystem::path &store, const std::string &sessionKey, const std::string &playerName, uint64_t token, int64_t now);
void forgetRejoinToken(const std::filesystem::path &store, const std::string &sessionKey, const std::string &playerName);

/** What the command line asks for: --host, --join, --coop-mode and friends. */
struct CoopLaunch {
    CoopRole role {CoopRole::None};
    uint16_t port {0};    // host: UDP port to listen on (0 with a join code: P2P only)
    std::string address;  // client: host "ip:port"
    std::string code;     // client: a join code instead of an address
    std::string service;  // the join-code service (--coop-server); a host with one gets a code
    bool relayOnly {false}; // ICE relay candidates only (TURN-forced tests)
    int askToJoin {-1};   // host: -1 follows the display (ask unless headless), 0 never, 1 always
    std::string mode {"story"};
    std::string playerName;
    std::string preferredTag;
    std::filesystem::path gamePath;
    std::filesystem::path k2Path; // empty unless --k2game
    std::filesystem::path executablePath;
    bool hostOnlyTransitions {false};
    bool hostOnlyTrading {false};
    bool noPause {false};
    float pauseCooldown {0.0f};
    float enemyScaling {1.0f};
};

/** A join request as the host's Accept/Decline prompt would show it. */
struct CoopJoinRequest {
    uint32_t clientId {0};
    std::string playerName;
    std::string companionTag;
};

/** Returns true to accept. The MVP default accepts everyone. */
using CoopJoinApprover = std::function<bool(const CoopJoinRequest &)>;

/** Opens a new client transport for a reconnect after a drop, or null with error set. */
using CoopTransportFactory = std::function<std::unique_ptr<net::ITransport>(std::string &error)>;

/**
 * A co-op session in Story Mode (design note coop_net).
 *
 * Host: runs the full single-player simulation untouched. Between ticks it
 * applies the input commands its clients sent, after validating each
 * against that client's creature; after each tick it sends every client a
 * delta snapshot of the replicated state.
 *
 * Client: a thin viewer. It loads the host's module from its own install,
 * runs no scripts and no simulation, binds its copy of each object to the
 * host's id, applies snapshots on its own ticks and interpolates between
 * them through Game::tickAlpha(). Its player sends commands.
 *
 * With no session, Game behaves exactly as single-player.
 */
class TestCoopSession;

class CoopSession {
public:
    CoopSession(Game &game, CoopConfig config, std::unique_ptr<net::ITransport> transport);
    ~CoopSession();

    CoopRole role() const { return _config.role; }
    bool isHost() const { return _config.role == CoopRole::Host; }

    /** Free Mode loot taken by each guest; persisted in the host's saves (CoopLootLedger::kSaveFileName). */
    CoopLootLedger &lootLedger() { return _lootLedger; }
    const CoopLootLedger &lootLedger() const { return _lootLedger; }
    bool isClient() const { return _config.role == CoopRole::Client; }

    // Game hooks

    /** Once per frame: transport events and messages. */
    void pollNetwork();
    /** Host, before each simulation tick: apply queued client commands. */
    void beforeTick();
    /** Host, after each simulation tick: slots, then snapshots. */
    void afterTick();
    /** Client, on each of its own ticks: apply received snapshots. */
    void clientTick(float dt);
    /** Client, per frame: interpolate replicated transforms. */
    void present(float alpha);
    /** Both: the area's GIT objects exist and no script has run yet. */
    void onModuleStructureLoaded();
    /** Client: a world click turns into a command. True when consumed. */
    bool interceptClick(const std::shared_ptr<Object> &object);
    /** Host: a visual effect started; replicated with its target. */
    void onVisualEffectBegin(const std::shared_ptr<VisualEffectPresenter::Request> &request);
    /** Host: a projectile left a weapon. */
    void onProjectileFired(const Creature &attacker, const Object &target, bool offhand, bool miss);
    /** Host: a movie started; clients play the same one from their install. */
    void onMovieStarted(const std::string &name);

    /**
     * The pause key (design, "Pause"): any player pauses for everyone and any
     * player resumes. A client asks the host. True when co-op handled it.
     */
    bool handlePauseKey();

    /**
     * Host: a player-initiated area exit (a transition trigger the leader
     * walked into, a transition door clicked). With clients present it
     * starts or joins the ready check instead, and returns true; the party
     * moves when the check passes. False: go ahead as single-player does.
     */
    bool interceptTransition(const std::string &module, const std::string &entry, uint32_t clickTrigger, uint32_t triggerer);

    /** Host: damage an enemy deals, scaled for the number of players. */
    int scaleEnemyDamage(const Object *attacker, const Object &target, int amount) const;

    /** Client: the container screen's buttons. True when the client handled them. */
    bool clientContainerTakeAll();
    bool clientContainerClose();
    bool clientContainerView() const { return isClient() && _clientViewing != 0; }

    void setJoinApprover(CoopJoinApprover approver) { _approver = std::move(approver); }

    /** Client: how to reach the host again after a drop (the AI keeps the companion meanwhile). */
    void setReconnect(CoopTransportFactory factory) { _reconnect = std::move(factory); }

    /** Party wipe: host, the party is down and not yet reloaded; client, the host is choosing. */
    bool partyDefeated() const { return isHost() ? _hostDefeated : _defeatShown; }

    /** Client test seam: close the connection with no Bye, as a lost network would. */
    void simulateDrop();

    /** Host: answer a pending join request (the Accept / Decline prompt, or the harness). */
    bool answerJoin(uint32_t clientId, bool accept);

    // Join codes and the Co-op screens

    /** The transport's join-code state; online false for a direct ip:port session. */
    net::OnlineStatus onlineStatus() const;
    /** Host: a new join code; the old one stops working. */
    void reissueCode();

    /** Client: where joining stands, for the Connecting... screen. */
    enum class JoinProgress {
        Connecting,   // resolving the code, opening the connection, handshaking
        Waiting,      // welcomed; waiting for the host to accept or a companion to free up
        Joined,       // playing
        Reconnecting, // the connection dropped; trying to get back in
        Failed        // for good: failureMessage() says why
    };
    JoinProgress joinProgress() const;

    /** Host: the connected players, for the Co-op screen. */
    struct PlayerSummary {
        uint32_t clientId {0};
        std::string name;
        std::string companion; // tag of the companion they control; empty while waiting
        bool waiting {false};  // waiting for the host's Accept / Decline
    };
    std::vector<PlayerSummary> players() const;
    /** A sentence for the player: bad code, version mismatch, files mismatch naming the file, host full, timed out... */
    std::string failureMessage() const;

    /**
     * Host: a connected client controls this creature, so the party AI must
     * leave it alone (no follow, no combat orders). False once the client
     * drops: the AI takes the companion over until the player rejoins.
     */
    bool isPlayerControlled(uint32_t creatureId) const;

    /** Host: a player's stand-in (design item 12): outside the party, driven by its player. */
    bool isStandIn(uint32_t creatureId) const;

    /**
     * Host: a module snapshot (a save, or leaving the module) is about to be
     * taken, and has been. In between, stand-ins are gone and scaled enemies
     * carry their base hit points, so neither reaches the saved state.
     */
    void beginModuleSnapshot();
    void endModuleSnapshot();

    // Co-op completeness (coopinventory.cpp)

    /** Host: a one-line bark on the host's HUD; clients show it too. */
    void onBark(const std::string &text, float duration);
    /** Host: hand a story terminal or console to a player, or take it back ("Hand over terminal"). */
    bool handOver(uint32_t clientId, uint32_t objectId, bool on);

    // Minigames (swoop races, turrets). The host simulates; a guest who owns the running
    // minigame steers it with MiniGameControl messages.

    /** Host: give the running minigame (a turret, usually) to a guest, or take it back with on false. */
    bool handMiniGame(uint32_t clientId, bool on);
    /** Host: the game started / ended a minigame; the session decides who drives it and what its end means. */
    void onMiniGameStarted(MinigameType type, const std::string &module);
    void onMiniGameEnded(const MiniGameSession &session);
    /** Host: a guest drives the running minigame, so the host's own keys are ignored. */
    bool miniGameRemoteOwned() const { return isHost() && _miniGame.remoteOwned(); }
    /** Host: once per minigame tick, before the simulation reads its input. */
    void applyMiniGameInput(MiniGameInput &input);
    const CoopMiniGame &miniGame() const { return _miniGame; }
    /** Client: the newest state of the running minigame the host sent (active false once it ended). */
    const net::MiniGameState &miniGameState() const { return _miniGameState; }
    /** Client: send the owner's input; false unless this player drives the running minigame. */
    bool clientSendMiniGameControl(net::MiniGameControl control);
    bool clientQuitMiniGame();
    /** Client: the host's states, for drawing the running minigame. */
    MiniGameReplay &miniGameReplay() { return _miniGameReplay; }
    /** Client: the result line drawn over a minigame that has just ended, while it is held on screen. */
    const std::string &clientMiniGameBanner() const {
        static const std::string none;
        return isClient() && (_miniGameResultHold > 0.0f || _moduleHeldForResult) ? _miniGameResult : none;
    }
    /** Client: this player drives the running minigame. */
    bool clientOwnsMiniGame() const;
    /**
     * Client, once a tick: the owner's local input goes to the host as MiniGameControl (one-shot
     * aim deltas are then spent); nothing is sent for a minigame this player only watches.
     */
    void clientMiniGameInput(MiniGameInput &input, glm::vec2 rateStep = glm::vec2(0.0f));
    /** Host: a sound the running minigame played; it goes out with the next state. */
    void onMiniGameSound(const std::string &resRef);
    /** Client: the host's minigame sounds received since the last call. */
    std::vector<std::string> takeMiniGameSounds();
    /** Client: the owner's turret aim drawn ahead of the sampled state by the aim not yet used on the host. */
    void clientPredictMiniGame(net::MiniGameState &sample);
    /** Host: the running minigame's scripts animated an actor; it goes out with the next state. */
    void onMiniGameAnimation(size_t actor, const std::string &name, uint8_t flags);
    /** Client: the host's animations received since the last call, in order. */
    std::vector<net::MiniGameAnimation> takeMiniGameAnimations();
    /** The co-op side of the running minigame, for the harness (coopmg). */
    boost::json::object miniGameJson() const;

    /** Client: the action bar used an item from the shared inventory. True when co-op handled it. */
    bool clientUseItem(const std::shared_ptr<Item> &localItem, uint32_t targetHostId);
    /** Client: the equipment screen put an item in a slot of the companion (null clears it). */
    bool clientEquip(const std::shared_ptr<Item> &localItem, int slot);
    /** Client: the host's sheet allows a level-up now (a level is due, within the host's level). */
    bool clientMayLevelUp() const;
    /** Client: the level-up screens finished: the host checks and applies the choices. */
    bool clientLevelUp(const CreatureAttributes &before, const CreatureAttributes &after, int clazz);
    /** Client: the host id of a local copy of a shared inventory item, 0 for none. */
    uint32_t clientHostItemId(uint32_t localItemId) const;

    // Free Mode (coopfree.cpp)

    bool freeMode() const { return _config.mode == "free"; }
    /** Host: a container screen opened on the host (Free Mode keeps a copy of what it held for the guests). */
    void onContainerOpened(const Object &container);
    /** The guests the HUD lists (Free Mode): the host sees all, a guest the others. */
    std::vector<net::GuestRosterEntry> guestList() const;
    /** Client: answer the store or minigame prompt the host offered. */
    bool clientAnswerOffer(bool yes);
    /** Client: its own Pazaak game (a prompt's) ended; the credits won or lost go to the host. */
    void clientPazaakFinished(bool won, int wager);
    /** Host: the creature a handed-over terminal session speaks as (GetPCSpeaker), 0 for the host's own. */
    uint32_t pcSpeaker() const { return _pcSpeaker; }
    /** Client: a reply row in the host's terminal session this player operates. True when co-op handled it. */
    bool clientPickReply(int row);
    /** Harness: show the prompts (Accept / Decline, store) in a headless run, for screenshots. */
    void setForcePrompts(bool force) { _forcePrompts = force; }
    /** Client: the target menu's action on the selected object, as a command. True when co-op handled it. */
    bool clientContextAction(const ContextAction &action, const std::shared_ptr<Object> &target);
    /**
     * Host: the object a door or trigger script sees as the one entering or
     * opening. A player-controlled creature (a companion, stand-in or guest)
     * counts as the PC, as the party leader does in KotOR 2, so scripts that
     * test GetFirstPC fire for it.
     */
    uint32_t scriptActor(uint32_t objectId) const;
    /** Client: the host says this local creature is in combat. */
    bool clientInCombat(uint32_t localId) const;
    /** Host test seam: a guest's credits. */
    bool setGuestCredits(uint32_t clientId, int credits);
    /** Host: every XP award the party gets goes to each guest too (design, "XP: mirrors the host"). */
    void onPartyXP(int amount);
    /** Client: the character this player brings (made with character generation, or loaded). Saved locally and sent. */
    void clientSetGuest(const net::GuestCharacter &guest);
    /** Client: a guest character is needed and none is loaded yet. */
    bool clientNeedsGuest() const { return isClient() && freeMode() && !_guestSent; }
    /** Client: the store screen's buttons for a guest's store. True when co-op handled them. */
    bool clientStoreBuy(uint32_t localItemId);
    bool clientStoreSell(uint32_t localItemId);
    bool clientStoreClosed();
    bool clientStoreOpen() const { return isClient() && _storeView.store != 0 && _storeView.open; }
    /** Client: the workbench or lab screen's buttons for a guest's bench. True when a request went out. */
    bool clientBenchOpen() const { return isClient() && _benchView.bench != 0 && _benchView.open; }
    const net::BenchView &benchView() const { return _benchView; }
    bool clientBenchUpgrade(uint32_t itemId, uint32_t partId);
    bool clientBenchRemove(uint32_t itemId, int slotIndex);
    bool clientBenchCraft(const std::string &resref, int material);
    bool clientBenchBreak(uint32_t itemId);
    bool clientBenchClose();
    /** Client: the bench screen's model (the view, its stand-in items, the last rejection). */
    GuestBenchScreen &benchScreen() { return _benchScreen; }
    /** Client: the local holder of the bench's stand-in items, or null. */
    std::shared_ptr<Object> benchHolder() const;
    /** Client: send one of the bench screen's requests. False when no bench is open. */
    bool clientBenchRequest(const BenchRequest &request);
    /** Where this player's guest character file lives. */
    std::filesystem::path guestPath() const;

    /**
     * Carry out the area's pending object removals now. Area::update does it
     * on the host; a client never updates its area, so it flushes them.
     */
    static void flushDestroyedObjects(Area &area);

    // Client input

    /** Send a command for the controlled creature. Returns its sequence number. */
    uint32_t submitCommand(net::Command command);
    uint32_t controlledHostId() const {
        return _slot.status == net::SlotStatus::Assigned || _slot.status == net::SlotStatus::StandIn ||
                       _slot.status == net::SlotStatus::Guest
                   ? _slot.creatureId
                   : 0;
    }
    /** Client: a host id from a host id or a tag of a replicated object. */
    uint32_t resolveHostId(const std::string &idOrTag) const;
    std::shared_ptr<Object> localObjectForHost(uint32_t hostId) const;

    // Harness

    /** A character the map marks beyond the local party (design note p2_ui). */
    struct MapMarker {
        std::shared_ptr<Object> object;
        bool player {false}; // a player's character; else a party member
    };
    /**
     * Host: the characters connected clients control. Client: the host's
     * party and the players' characters, less the one this client controls.
     */
    std::vector<MapMarker> mapMarkers() const;

    boost::json::object statusJson() const;
    /** Client: every replicated object as its host state and local binding. */
    boost::json::object dumpJson() const;
    /** Last command result the host sent for a sequence number. */
    const net::CommandResult *commandResult(uint32_t seq) const;

    /** Answer the running ready check (host or client). False when none runs. */
    bool answerReady(bool ready);
    /** Host: move the party now. */
    bool forceTransition();
    /** Client: a session request (Ready, Pause, TakeItem, CloseContainer, PickSlot). */
    uint32_t submitRequest(net::CommandType type, uint32_t target, int32_t param, const std::string &item = std::string());
    /** Client: host id of the local copy of an item in the open container. */
    uint32_t hostItemId(uint32_t localItemId) const;

private:
    struct HostClient {
        net::ConnectionId connection {0};
        uint32_t clientId {0};
        std::string name;
        std::string address;
        bool welcomed {false};
        bool slotRequested {false};
        bool slotAnswered {false};
        std::string preferredTag;
        net::SlotAssign slot;
        uint32_t moduleGeneration {0};
        CoopRateLimiter limiter;
        std::deque<net::Command> pending;
        uint32_t violations {0};
        bool sawCommand {false};
        uint32_t lastCommandSeq {0}; // commands arrive in order, so a seq at or below this is a replay
        uint64_t commandsAccepted {0};
        uint64_t commandsRejected {0};
        std::map<uint8_t, uint64_t> rejectCounts;
        uint64_t snapshotsSent {0};
        uint64_t snapshotBytes {0};
        uint64_t maxSnapshotBytes {0};
        uint64_t fullSnapshotBytes {0};
        uint32_t viewing {0}; // container this client has open
        std::vector<net::ContainerItem> viewSent;
        uint32_t insideExit {0}; // transition trigger the client's creature stands in
        uint32_t readySent {0};  // ready check id + state last sent
        net::ReadyState readyStateSent {net::ReadyState::Idle};
        uint16_t readySecondsSent {0};
        // N3: the snapshots sent since the last one the client acknowledged,
        // each as the client will hold it once decoded (the delta base).
        struct Sent {
            uint32_t tick {0};
            std::map<uint32_t, net::EntityState> entities;
            net::WorldState world;
        };
        std::deque<Sent> history;
        uint32_t ackedTick {0};
        std::map<uint32_t, uint8_t> animSerials;
        std::chrono::steady_clock::time_point lastSnapshotSent;
        uint64_t acks {0};
        uint64_t resends {0};
        uint64_t rejoinToken {0};
        bool trusted {false};  // came back with the token of a join the host approved: no prompt
        bool replaced {false}; // dropped because the same player reconnected: its token stays
        bool approved {false}; // the host let this player in (see hostMarkApproved)
        enum class Approval {
            None,
            Pending,
            Accepted,
            Declined
        };
        Approval approval {Approval::None};
        std::string approvalTag;  // the companion the prompt names
        std::string approvalName; // and its display name
        // Stand-in (design item 12): the host id of this player's stand-in, 0
        // while there is none, and its hit points when it last went away in
        // this module visit (a conversation, a save), so a respawn keeps wounds.
        uint32_t standIn {0};
        int standInHp {0};
        net::SlotAssign slotSent; // the slot as this client was last told it
        bool slotSentValid {false};
        // Completeness: the inventory and sheet this client was last sent,
        // and the story terminals the host handed it.
        std::vector<uint8_t> inventorySent;
        std::vector<uint8_t> sheetSent;
        std::set<uint32_t> handedOver;
        // Free Mode: the guest's own character, its creature in this module
        // (0 while away), the store it trades with, and what it was last sent.
        bool guestReceived {false};
        uint32_t guestId {0};
        uint32_t storeOpen {0};
        std::vector<uint8_t> storeSent;
        // The workbench or lab this player has open (either mode), and what it was last sent.
        uint32_t benchOpen {0};
        net::BenchKind benchKind {net::BenchKind::Workbench};
        std::vector<uint8_t> benchSent;
        uint64_t miniGameIgnored {0}; // minigame inputs from this player that nobody was listening for
        std::vector<uint8_t> guestSent;
        uint64_t guestSyncTick {0};
        bool levelFloorChecked {false};
        net::Offer offer;          // the prompt this guest has open, or answered with yes (Pazaak)
        bool pazaakPlaying {false};
        uint32_t terminalControl {0}; // the handed-over terminal whose session this guest runs now
        uint64_t terminalStartTick {0};
    };

    struct Replica {
        net::EntityState state;
        uint32_t localId {0};
        net::Vec3 previousPosition;
        float previousFacing {0.0f};
        bool interpolate {false};
        bool spawned {false}; // created by the client from its template
    };

    struct ClientProjectile {
        std::unique_ptr<Projectile> projectile;
        float age {0.0f};
    };

    Game &_game;
    CoopConfig _config;
    std::unique_ptr<net::ITransport> _transport;
    CoopJoinApprover _approver;
    CoopTransportFactory _reconnect;

    // Host
    std::map<net::ConnectionId, HostClient> _clients;
    uint32_t _nextClientId {1};
    uint32_t _generation {0};
    // The module the clients were last told about. Held weakly, not by
    // address: a new module can be allocated where the old one was freed,
    // and an address compare then misses the change.
    std::weak_ptr<Module> _hostModule;
    bool hasHostModule() const { return !_hostModule.expired(); }
    std::map<uint32_t, std::string> _hostKeys; // host id -> load key
    /** Players who dropped: name -> the companion tag they get back on rejoin. */
    std::map<std::string, std::string> _reservations;
    /**
     * A rejoin token handed out this session. Holding one proves who a player
     * is; it does not make them a member. Only a join the host actually let
     * in (approved) comes back trusted; a pending one comes back pending.
     */
    struct RejoinToken {
        std::string name;
        bool approved {false};
    };
    std::map<uint64_t, RejoinToken> _rejoinTokens;
    /** The order tokens were issued in, oldest first, for evicting when the table is full. */
    std::deque<uint64_t> _rejoinTokenOrder;
    uint32_t _joinPromptClient {0}; // the client the visible Accept / Decline prompt is about
    // Ready check (area exits)
    CoopReadyCheck _ready;
    uint32_t _nextReadyId {1};
    struct PendingExit {
        std::string module;
        std::string entry;
        uint32_t clickTrigger {0};
        uint32_t triggerer {0};
    };
    PendingExit _pendingExit;
    bool _hostReadyPrompted {false};
    uint32_t _readyOutcomeSent {0};
    // Pause
    std::string _pausedBy;
    bool _lastPaused {false};
    bool _shownPause {false};
    std::string _shownPausedBy;
    CoopPauseCooldown _pauseCooldown;
    // Enemy scaling: creature id -> base hit points before scaling, and the factor applied
    struct Scaled {
        int base {0};
        float factor {1.0f};
    };
    std::map<uint32_t, Scaled> _scaled;
    int _scalePlayers {1};
    CoopEnemyScale _scale;
    std::vector<net::NetEvent> _pendingEvents;
    struct HostVfx {
        uint32_t handle {0};
        std::shared_ptr<VisualEffectPresenter::Request> request;
    };
    std::vector<HostVfx> _hostVfx;
    uint32_t _nextVfxHandle {1};
    // Stand-ins: stand-in id -> the solo character it copies
    std::map<uint32_t, uint32_t> _standInSource;
    std::string _standInPause; // why stand-ins are away now (status); empty when they may be out
    struct SnapshotHp {
        int base {0};
        int current {0};
    };
    std::map<uint32_t, SnapshotHp> _snapshotHp;
    bool _inModuleSnapshot {false};
    // Free Mode: each container's contents when a player first opened it,
    // the copy every guest loots from.
    struct LootEntry {
        uint32_t item {0};
        std::string resref;
        std::string tag;
        uint16_t stack {1};
        bool plot {false};
        bool credits {false};
    };
    std::map<uint32_t, std::vector<LootEntry>> _lootBaseline;
    // What each guest took, per module and container, for the session: a
    // revisit restores it (object ids change, the ledger keys by tag and place).
    CoopLootLedger _lootLedger;
    std::string lootModule() const;
    std::string lootKey(const Object &container) const;
    // Free Mode, host: each guest's character by player name, kept current
    // from its creature, so it survives transitions, drops and rejoins.
    std::map<std::string, net::GuestCharacter> _guests;
    // The rejoin token each record belongs to. A name is only a label: the
    // record goes back to whoever holds that token, and retires with it.
    std::map<std::string, uint64_t> _guestOwners;
    /** Forget a guest's record (and, with \p loot, what it took). */
    void hostRetireGuest(const std::string &name, bool loot);
    /** Keep at most kMaxAbsentGuests records of players who are away. */
    void hostTrimAbsentGuests();
    /** Forget a token with everything tied to it: reservation, guest record, loot. */
    void hostRetireToken(uint64_t token);
    // Free Mode, client
    bool _guestSent {false};
    bool _moduleHeld {false}; // a ModuleInfo waiting for the guest character
    net::GuestCharacter _guest;
    net::StoreView _storeView;
    net::BenchView _benchView;
    // Minigames: who drives the running one, a request a guest made that has not started yet, and (client) the state sent.
    CoopMiniGame _miniGame;
    struct PendingMiniGame {
        uint32_t client {0};
        std::string module;
        StoryState story;
    };
    PendingMiniGame _pendingMiniGame;
    net::MiniGameState _miniGameState;
    uint32_t _miniGameControlSeq {0};
    MiniGameReplay _miniGameReplay;
    uint64_t _miniGameControlsSent {0};
    uint64_t _miniGameControlsAccepted {0}; // host: owner inputs used
    std::string _miniGameResult;            // client: the line shown when the last one ended
    std::vector<net::MiniGameAnimation> _miniGameAnimations; // host: for the next state; client: to play
    uint64_t _miniGameAnimationsDropped {0};
    MiniGameAimPrediction _miniGameAim;
    std::vector<std::string> _miniGameSounds; // host: for the next state; client: to play
    uint64_t _miniGameSoundsHeard {0};
    // Client: the finished minigame (and its result line) stays up this long before the
    // module the host moved on to is loaded.
    float _miniGameResultHold {0.0f};
    bool _moduleHeldForResult {false};
    std::shared_ptr<Object> _localStore;
    GuestBenchScreen _benchScreen;
    std::shared_ptr<Object> _benchHolder; // a local store shell holding the bench's stand-ins
    std::map<uint32_t, uint32_t> _storeLocal; // local store item id -> host item id
    uint64_t _guestSaves {0};
    std::vector<net::GuestRosterEntry> _roster;  // client: from the host
    std::vector<uint8_t> _rosterSent;            // host: the last list sent
    net::Offer _offer;                           // client: the prompt the host offered
    bool _offerPrompted {false};
    bool _guestPazaak {false};                   // client: its own Pazaak game from a prompt is on
    uint32_t _pcSpeaker {0};                     // host: a guest's creature running a handed-over terminal
    bool _clientTerminal {false};
    bool _forcePrompts {false};
    bool _steering {false};     // client: movement keys held, MoveTo commands going out
    float _steerTimer {0.0f};
    uint64_t _steerCommands {0};                // client: this player makes the terminal's choices
    std::string _terminalRead;                   // client: the info-only terminal last opened here

    // Client
    enum class ClientState {
        Connecting,
        Handshaking,
        Joined,
        Rejected,
        Disconnected,
        Reconnecting
    };
    ClientState _clientState {ClientState::Connecting};
    net::ConnectionId _hostConnection {0};
    net::Welcome _welcome;
    net::Reject _reject;
    std::string _disconnectReason;
    net::SlotAssign _slot;
    net::ModuleInfo _module;
    uint32_t _loadedGeneration {0};
    bool _moduleLoaded {false};
    std::map<std::string, uint32_t> _localKeys; // load key -> local id
    std::map<uint32_t, std::string> _localKeyById;
    std::map<uint32_t, uint32_t> _carried; // host id -> local party creature from the previous module
    friend class TestCoopSession; // test/game/coopnet.cpp: drives the client side without a host

    NetIdMap _ids;
    std::map<uint32_t, Replica> _replicas; // host id -> replica
    // N3 snapshots: decoded states by host tick. _recon keeps recent ones as
    // delta bases; _buffer is the jitter buffer of states not yet applied;
    // _applied is the state the world shows now.
    struct Recon {
        uint32_t tick {0};
        std::map<uint32_t, net::EntityState> entities;
        net::WorldState world;
    };
    std::map<uint32_t, std::shared_ptr<const Recon>> _recon;
    std::map<uint32_t, std::shared_ptr<const Recon>> _buffer;
    std::shared_ptr<const Recon> _applied;
    std::deque<net::EventBatch> _eventQueue;
    uint32_t _playTick {0};
    int _delayTicks {2};
    float _jitter {0.0f};
    uint64_t _clientTicks {0};
    uint64_t _lastArrivalClientTick {0};
    uint32_t _lastArrivalTick {0};
    uint64_t _baseMissing {0};
    uint64_t _acksSent {0};
    // Reconnect after a drop
    uint64_t _rejoinToken {0};
    bool _everJoined {false};
    bool _reconnecting {false}; // from a drop until the host welcomes us back
    bool _lostShown {false};    // the "connection lost" popup went up
    bool _defeatShown {false};  // client: "Party defeated: the host is choosing..." is up
    bool _hostDefeated {false}; // host: the party is down and the host has not chosen yet
    bool _hostEnded {false};
    std::chrono::steady_clock::time_point _reconnectDeadline;
    std::chrono::steady_clock::time_point _nextReconnect;
    int _reconnects {0};
    net::WorldState _world;
    uint32_t _appliedTick {0};
    uint64_t _snapshotsApplied {0};
    uint64_t _snapshotsDropped {0};
    uint64_t _bytesReceived {0};
    uint32_t _nextSeq {1};
    std::deque<net::CommandResult> _results;
    std::map<uint32_t, std::shared_ptr<VisualEffectPresenter::Request>> _clientVfx; // by host handle
    std::vector<ClientProjectile> _projectiles;
    uint32_t _unmatchedSpawns {0};
    uint32_t _spawnFailures {0};
    uint32_t _idsEqual {0};
    uint32_t _idsBoundByKey {0};
    uint32_t _hostMessageViolations {0};
    std::string _replicaDialog;
    net::ReadyCheck _clientReady;
    bool _clientReadyAnswered {false};
    uint32_t _clientReadyPrompted {0};
    uint32_t _clientViewing {0}; // host id of the container this client has open
    std::map<uint32_t, uint32_t> _clientItems; // local item id -> host item id
    net::SlotStatus _lastSlotStatus {net::SlotStatus::Waiting};
    uint32_t _lastSlotCreature {0};
    uint32_t _standInSpawns {0};
    // Completeness (client)
    std::map<uint32_t, uint32_t> _invLocal; // local item id -> host item id
    std::map<uint32_t, uint32_t> _invHost;  // host item id -> local item id
    int32_t _inventoryCredits {0};
    uint64_t _inventoryUpdates {0};
    net::CharacterSheet _sheet;
    bool _sheetValid {false};
    uint64_t _sheetUpdates {0};
    std::map<int, uint32_t> _equipHostIds; // slot -> host item id
    uint64_t _barksReceived {0};
    std::string _lastBark;
    uint64_t _barksSent {0}; // host
    std::string _hostMovieName;
    std::string _lastNotice;
    std::deque<std::string> _notices;

    // Host
    void hostHandleConnected(const net::TransportEvent &event);
    void hostHandleDisconnected(const net::TransportEvent &event);
    void hostHandleMessage(HostClient &client, const std::vector<uint8_t> &data);
    void hostHandleHello(HostClient &client, const net::Hello &hello);
    void hostReject(HostClient &client, net::RejectReason reason, const std::string &detail);
    void hostViolation(HostClient &client, const std::string &what);
    void hostUpdateSlots();
    /** A companion held for a player who dropped: only that player, proving it with their rejoin token, may take it. */
    bool isReservedForOther(const std::string &tag, const HostClient &client) const;
    void hostIssueRejoinToken(HostClient &client);
    void hostForgetRejoinToken(const HostClient &client);
    /** The host let this client in (a companion, a stand-in, a guest character, or an Accept). */
    void hostMarkApproved(HostClient &client);
    /** May this client act in the session yet (Pause, Ready)? */
    bool hostApproved(const HostClient &client) const;
    void hostTakeControl(Creature &creature);
    int hostPlayerCount() const;
    bool showsPrompts() const;
    void bark(const std::string &text, float duration);
    float hostNow() const;

    // N2 flows (coopflows.cpp)
    net::CommandReject hostSessionCommand(HostClient &client, const net::Command &command, bool &handled);
    void hostCarryOutUse(HostClient &client, const net::Command &command, const CoopCommandTargets &targets);
    void hostOpenContainer(uint32_t clientId, const std::shared_ptr<Object> &container, uint32_t actor);
    void hostUpdateContainers();
    void hostSendContainer(HostClient &client, bool open);
    void hostStartReadyCheck(uint32_t initiator, const PendingExit &exit);
    void hostUpdateReadyCheck();
    void hostSendReady(HostClient &client, bool force);
    void hostPromptReady();
    void hostCheckClientExits();
    void hostUpdatePause();
    void hostUpdateScaling();
    // Stand-ins (coopstandin.cpp)
    uint32_t hostControlledId(const HostClient &client) const;
    net::SlotAssign hostEffectiveSlot(const HostClient &client) const;
    void hostSendSlot(HostClient &client, bool force);
    void hostUpdateStandIns();
    std::string hostStandInPause() const;
    std::shared_ptr<Creature> hostSpawnStandIn(HostClient &client, const Creature &source, int index);
    void hostDespawnStandIn(HostClient &client, bool remember);
    std::shared_ptr<Object> clientSpawnStandIn(Replica &replica);
    // Completeness (coopinventory.cpp)
    net::CommandReject hostCompanionCommand(HostClient &client, const net::Command &command, bool &handled);
    void hostSendInventories();
    void hostSendSheets();
    void hostUsePlaceable(HostClient &client, const std::shared_ptr<Creature> &actor, const std::shared_ptr<Object> &target);
    void hostPromptHandOver(HostClient &client, const Object &object);
    // Minigames
    net::CommandReject hostMiniGameCommand(HostClient &client, const net::Command &command, bool &handled);
    net::CommandReject hostRequestGuestMiniGame(HostClient &client);
    void hostHandleMiniGameControl(HostClient &client, const net::MiniGameControl &control);
    void hostSendMiniGame(const net::MiniGameState &state);
    void hostBroadcastMiniGame();
    void hostPromptMiniGameHandOver();
    void clientHandleMiniGameState(const net::MiniGameState &state);
    void clientHandleInventory(const net::SharedInventory &inventory);
    void clientHandleSheet(const net::CharacterSheet &sheet);
    void clientHandleBark(const net::Bark &bark);
    // Free Mode (coopfree.cpp)
    std::string hostCheckGuest(const net::GuestCharacter &guest) const;
    void hostHandleGuest(HostClient &client, const net::GuestCharacter &guest);
    void hostUpdateGuests();
    std::shared_ptr<Creature> hostSpawnGuest(HostClient &client, const Creature &leader, int index);
    void hostSyncGuest(HostClient &client, bool send);
    void hostDespawnGuest(HostClient &client);
    // Stores: opening one is mode-agnostic (an area lookup); hostSendStore and
    // hostStoreCommand trade against the guest's own wallet/items in Free
    // Mode, or the shared party pool/inventory in Story Mode.
    bool hostOpenGuestStore(HostClient &client, const std::shared_ptr<Object> &merchant);
    void hostSendStore(HostClient &client, bool open);
    net::CommandReject hostStoreCommand(HostClient &client, const net::Command &command, bool &handled);
    // Workbenches and lab stations: a guest uses one through its own view (GuestBench).
    void hostUseBench(HostClient &client, const std::shared_ptr<Creature> &actor, const std::shared_ptr<Object> &bench);
    bool hostOpenGuestBench(HostClient &client, const std::shared_ptr<Object> &bench);
    void hostSendBench(HostClient &client, bool open);
    net::CommandReject hostBenchCommand(HostClient &client, const net::Command &command, bool &handled);
    void clientHandleBench(const net::BenchView &view);
    void hostSendGuestInventory(HostClient &client);
    void hostSendRoster();
    bool hostOfferGuest(HostClient &client, const std::shared_ptr<Object> &npc);
    // Would npc's conversation, as things stand, reach its store (or Pazaak)
    // node? Its Active conditions run with the client's creature as the PC
    // speaker; its action scripts never run.
    bool hostDialogReaches(HostClient &client, const Object &npc, net::OfferKind kind);
    // Story Mode: a merchant whose conversation would not open its store now
    // refuses with a notice (true) instead of opening it.
    bool hostRefusesTrade(HostClient &client, const Object &merchant);
    net::CommandReject hostOfferCommand(HostClient &client, const net::Command &command, bool &handled);
    void clientHandleOffer(const net::Offer &offer);
    // Terminals (design, "Computer terminals")
    net::CommandReject hostGuestTerminal(HostClient &client, const std::shared_ptr<Object> &terminal);
    void hostUpdateTerminals();
    void clientHandleTerminalNotice(const net::Notice &notice);
    void hostCaptureLoot(const Object &container);
    std::vector<net::ContainerItem> hostContainerView(HostClient &client, const Object &container);
    net::CommandReject hostGuestTake(HostClient &client, Object &container, Creature &actor, int32_t param);
    void clientHandleGuest(const net::GuestCharacter &guest);
    void clientHandleStore(const net::StoreView &view);
    void clientSaveGuest() const;
    void clientSteer(float dt);
    void clientAfterWelcomeFree();
public:
    /** Client: open character generation for this player's guest character. */
    void clientOpenGuestCreation();

private:
    void hostNotice(HostClient &client, net::NoticeKind kind, const std::string &text);
    std::vector<CoopReadyCheck::Player> hostReadyPlayers() const;
    void clientHandleContainer(const net::ContainerView &view);
    void clientHandleReady(const net::ReadyCheck &check);
    void clientPromptReady();
    void clientHandleNotice(const net::Notice &notice);
    void clientShowRejection(const net::CommandResult &result);
    void hostSendModuleInfo(HostClient &client);
    void hostDetectModuleChange();
    std::map<uint32_t, net::EntityState> hostCaptureEntities() const;
    net::WorldState hostCaptureWorld() const;
    void hostSendSnapshot(HostClient &client, const std::map<uint32_t, net::EntityState> &entities,
                          const net::WorldState &world);
    std::vector<uint8_t> hostEncodeSnapshot(const HostClient &client, const HostClient::Sent &target) const;
    void hostHandleAck(HostClient &client, const net::SnapshotAck &ack);
    void hostResendSnapshots();
    void hostDropClient(net::ConnectionId connection, const std::string &reason);
    void hostPromptJoin();
    void hostUpdateServiceLock();
    void hostPartyDefeated();
    void hostCollectVfxEnds();
    void send(net::ConnectionId connection, const std::vector<uint8_t> &data);

    // Client
    void clientHandleMessage(const std::vector<uint8_t> &data);
    void clientEnterModule();
    void clientReceiveSnapshot(const net::Snapshot &snapshot);
    void clientApplyRecon(const std::shared_ptr<const Recon> &recon);
    void clientApplyDueEvents();
    void clientHandleDisconnect(const std::string &reason);
    void clientUpdateReconnect();
    void clientSendHello();
    void clientApplyEntity(const net::EntityRecord &record);
    std::shared_ptr<Object> clientBind(Replica &replica);
    void clientApplyWorld(uint16_t fields, const net::WorldState &world);
    void clientApplyEvent(const net::NetEvent &event);
    void clientRetireUnbound();
    void clientMirrorParty();
    void clientUpdateProjectiles(float dt);
    void clientShowConversation(const net::ConversationState &conversation);
    void clientShowMovie();
    void clientShowPause();
    void clientCloseContainerScreen(bool silent);
    void showNotice(net::NoticeKind kind, const std::string &text);
};

/**
 * The build identity compared in the handshake: the protocol version and a
 * hash of the engine executable, so only identical builds play together.
 */
std::string coopBuildVersion(const std::filesystem::path &executable);

/** Free Mode: a new guest character from what character generation made. */
net::GuestCharacter coopGuestFromCharacter(Game &game, const Character &character);

/**
 * Fingerprint the installs, open the transport and start a session on game.
 * Returns null with error set when the launch is invalid or the transport
 * cannot start. Nothing is created for CoopRole::None.
 */
std::shared_ptr<CoopSession> startCoopSession(Game &game, const CoopLaunch &launch, std::string &error);

} // namespace game

} // namespace reone
