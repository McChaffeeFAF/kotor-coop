/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace reone {

namespace net {

/**
 * Co-op wire protocol (design note coop_net).
 *
 * Host-authoritative: the host simulates, clients send input commands and
 * render the snapshots the host sends back. Every message starts with a
 * MessageType byte. Every decoder rejects truncated input, trailing bytes,
 * over-long strings, oversized lists, out-of-range enums and non-finite
 * floats, so nothing malformed reaches the game.
 */

constexpr uint16_t kProtocolVersion = 6; // 4: VfxBegin carries a beam source; 5: guest minigames (MiniGameControl/State, MiniGameQuit); 6: text chat and pings (ChatSend/ChatMessage/PingSend/PingMarker, ids 100+), optional Hello password and Reject BadPassword (dedicated hosts)

/** A client message is small: a hello, a slot request or one command. */
constexpr size_t kMaxClientMessageBytes = 2048;
/** A full snapshot of a large area stays well under this. */
constexpr size_t kMaxHostMessageBytes = 1024 * 1024;
constexpr size_t kMaxStringBytes = 128;
constexpr size_t kMaxEntities = 4096;
constexpr size_t kMaxEvents = 1024;
constexpr size_t kMaxReplies = 64;
constexpr size_t kMaxPartyEntries = 16;
constexpr size_t kMaxContainerItems = 256;
constexpr size_t kMaxReadyPlayers = 16;
constexpr size_t kMaxInventoryItems = 1024;
constexpr size_t kMaxSheetEntries = 1024; // feats or powers on a character sheet
constexpr size_t kMaxSheetEquipment = 24;
/** Free Mode: a guest's character travels whole, so it may be larger than a client's other messages. */
constexpr size_t kMaxGuestCharacterBytes = 16384;
constexpr size_t kMaxGuestItems = 400;
constexpr size_t kMaxStoreItems = 400;

enum class MessageType : uint8_t {
    Hello = 1,         // client -> host
    Welcome = 2,       // host -> client
    Reject = 3,        // host -> client
    SlotRequest = 4,   // client -> host
    SlotAssign = 5,    // host -> client
    ModuleInfo = 6,    // host -> client
    Snapshot = 7,      // host -> client
    Command = 8,       // client -> host
    CommandResult = 9, // host -> client
    Bye = 10,          // either way
    Notice = 11,       // host -> client: a line for the player (refusals, who paused)
    ContainerView = 12, // host -> client: a container this client has open, and its contents
    ReadyCheck = 13,   // host -> client: the area-exit ready check
    // N3 (design note coop_online): snapshots travel unreliably, deltas
    // against the last one the client acknowledged; events stay reliable.
    SnapshotAck = 14,  // client -> host, unreliable: the newest snapshot the client holds
    Events = 15,       // host -> client, reliable: one tick's VFX and projectile events
    // Co-op completeness (design note coop_net): numbered apart from the
    // online work's so the two can grow side by side.
    SharedInventory = 20, // host -> client: the shared party inventory (Story Mode), when it changes
    CharacterSheet = 21,  // host -> client: the sheet of the creature the client controls
    Bark = 22,            // host -> client: a one-line bark the host's HUD shows
    // Free Mode (design note coop_net, "Free Mode")
    GuestCharacter = 23,  // either way: a guest's own character (client: at join; host: its saved state)
    StoreView = 24,       // host -> client: the store a guest trades with, priced for them
    GuestRoster = 25,     // host -> client: every guest's name, hit points and level, for the HUD list
    Offer = 26,           // host -> client: a store or minigame prompt instead of a conversation
    BenchView = 27,       // host -> client: the workbench or lab a guest uses, with what it can do there
    // Guest minigames (swoop races, turret sequences): the host simulates, the owner steers
    MiniGameControl = 28, // client -> host: the owner's input for the running minigame
    MiniGameState = 29,   // host -> client: what a client needs to draw the running minigame
    // Text chat and pings: numbered from 100 so in-flight branches do not collide.
    ChatSend = 100,       // client -> host: a chat line to relay
    ChatMessage = 101,    // host -> client: a relayed chat line
    PingSend = 102,       // client -> host: mark a point or object in the world
    PingMarker = 103      // host -> client: a short-lived marker to draw
};

/** The largest message a client may send, by its leading type byte. */
inline size_t clientMessageLimit(uint8_t type) {
    return type == static_cast<uint8_t>(MessageType::GuestCharacter) ? kMaxGuestCharacterBytes : kMaxClientMessageBytes;
}


/** Message types a host accepts from a client. Anything else is a violation. */
bool isClientMessage(MessageType type);
/** Message types a client accepts from the host. */
bool isHostMessage(MessageType type);

/** First byte of a message, if it is a known type. */
std::optional<MessageType> peekType(const std::vector<uint8_t> &bytes);

const char *messageTypeName(MessageType type);

struct Vec3 {
    float x {0.0f};
    float y {0.0f};
    float z {0.0f};

    bool operator==(const Vec3 &o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const Vec3 &o) const { return !(*this == o); }
};

// Handshake

/** Install fingerprint: hashes over chitin.key, the module list and dialog.tlk. */
struct Fingerprint {
    bool present {false};
    uint64_t key {0};
    uint64_t modules {0};
    uint64_t tlk {0};

    bool operator==(const Fingerprint &o) const {
        return present == o.present && key == o.key && modules == o.modules && tlk == o.tlk;
    }
    bool operator!=(const Fingerprint &o) const { return !(*this == o); }
};

struct Hello {
    uint16_t protocol {kProtocolVersion};
    std::string build;
    std::string mode; // "story" (MVP) or "free"
    std::string playerName;
    Fingerprint k1;
    Fingerprint k2;
    // The token a previous Welcome gave this player, 0 on a first join. A
    // reconnect presenting it replaces a connection the host still thinks
    // is alive, and gets the same companion back.
    uint64_t rejoinToken {0};
    // The session password (dedicated hosts, --coop-password). Appended after
    // the other fields and sent only when set, so a Hello without one is the
    // same bytes as before and the protocol version did not change.
    std::string password;
};

struct Welcome {
    uint32_t clientId {0};
    uint32_t hostTick {0};
    uint16_t tickRate {30};
    std::string mode;
    uint64_t rejoinToken {0}; // for Hello::rejoinToken after a drop
};

enum class RejectReason : uint8_t {
    ProtocolMismatch = 1,
    BuildMismatch = 2,
    FilesMismatchK1 = 3,
    FilesMismatchK2 = 4,
    ModeMismatch = 5,
    SessionFull = 6,
    Declined = 7,
    Malformed = 8,
    Kicked = 9,
    BadPassword = 10 // the host needs a password and this one is missing or wrong
};

const char *rejectReasonName(RejectReason reason);

struct Reject {
    RejectReason reason {RejectReason::Malformed};
    std::string detail;
};

/** The client asks for a companion slot. Empty tag: any free companion. */
struct SlotRequest {
    std::string preferredTag;
};

enum class SlotStatus : uint8_t {
    Assigned = 1, // the client controls creatureId
    Waiting = 2,  // accepted, but no companion is free yet
    Released = 3, // the companion left the party; waiting again
    Declined = 4, // the host declined the request
    StandIn = 5,  // no companion is free: the client controls creatureId, a stand-in outside the party
    Guest = 6     // Free Mode: the client controls creatureId, its own character, outside the party
};

const char *slotStatusName(SlotStatus status);

struct SlotAssign {
    SlotStatus status {SlotStatus::Waiting};
    int32_t npc {-1};
    uint32_t creatureId {0};
    std::string tag;
};

/** What the client needs to build the host's player character locally. */
struct PlayerLook {
    uint32_t hostId {0};
    int32_t appearance {0};
    uint8_t gender {0};
    std::string name;
    int32_t portrait {-1}; // portraits.2da row, for the client's HUD
};

/** The host entered a module: the client loads it from its own install. */
struct ModuleInfo {
    uint32_t generation {0};
    std::string module;
    PlayerLook player;
};

// Snapshots

enum EntityField : uint16_t {
    kFieldIdentity = 1 << 0, // type, key, tag, template: sent once per module visit
    kFieldPosition = 1 << 1,
    kFieldFacing = 1 << 2,
    kFieldAnim = 1 << 3, // animation name and the time into it
    kFieldVitals = 1 << 4,
    kFieldWeapons = 1 << 5, // main hand, off hand and body armor templates
    kFieldOpenLock = 1 << 6,
    kFieldDoor = 1 << 7,
    kFieldFlags = 1 << 8,
    kFieldRemoved = 1 << 9,
    kFieldTarget = 1 << 10,
    kFieldAllMask = (1 << 11) - 1
};

enum EntityFlag : uint8_t {
    kFlagVisible = 1 << 0,
    kFlagSelectable = 1 << 1,
    kFlagInCombat = 1 << 2,
    kFlagInConversation = 1 << 3,
    // Map markers (design note p2_ui, "Party markers"): a member of the
    // host's party, and a player's character (the host's leader or a
    // connected client's).
    kFlagPartyMember = 1 << 4,
    kFlagPlayer = 1 << 5
};

/** Replicated state of one object, keyed by the host's object id. */
struct EntityState {
    uint32_t id {0};
    uint8_t type {0}; // game::ObjectType
    std::string key;  // stable load key: matches the client's own copy
    std::string tag;
    std::string resref;
    Vec3 position;
    float facing {0.0f};
    std::string anim;
    float animTime {0.0f};
    uint16_t animFlags {0}; // scene::AnimationFlags of the playing channel
    float animSpeed {1.0f};
    uint8_t animSerial {0}; // changes whenever the host (re)starts the clip, so a restart is never mistaken for a resend
    uint32_t animSentTick {0}; // host bookkeeping, not on the wire: the tick animTime was taken at
    int16_t hp {0};
    int16_t maxHp {0};
    bool dead {false};
    std::string rightWeapon;
    std::string leftWeapon;
    std::string armor;
    std::string headgear; // head slot: masks and helmets show on the model
    bool open {false};
    bool locked {false};
    uint8_t doorState {0};
    uint8_t doorTransition {0};
    uint8_t flags {0};
    uint32_t attackTarget {0};
};

/** One entity in a snapshot: the id and the fields that changed. */
struct EntityRecord {
    uint16_t fields {0};
    EntityState state;
};

enum WorldField : uint16_t {
    kWorldScreen = 1 << 0,
    kWorldFade = 1 << 1,
    kWorldMovie = 1 << 2,
    kWorldCamera = 1 << 3,
    kWorldConversation = 1 << 4,
    kWorldParty = 1 << 5,
    kWorldPause = 1 << 6, // tactical pause and who paused
    kWorldAllMask = (1 << 7) - 1
};

struct ConversationState {
    bool active {false};
    uint8_t kind {0}; // 0 dialog, 1 computer
    std::string dialog;
    int32_t entry {-1};
    uint32_t owner {0};
    std::string speaker;
    std::vector<int32_t> replies; // reply node indices, in list order
    bool entryEnded {false};

    bool operator==(const ConversationState &o) const {
        return active == o.active && kind == o.kind && dialog == o.dialog && entry == o.entry &&
               owner == o.owner && speaker == o.speaker && replies == o.replies && entryEnded == o.entryEnded;
    }
    bool operator!=(const ConversationState &o) const { return !(*this == o); }
};

struct PartyEntry {
    int8_t npc {-1};
    uint32_t id {0};

    bool operator==(const PartyEntry &o) const { return npc == o.npc && id == o.id; }
};

struct WorldState {
    uint8_t screen {0}; // game::Game::Screen
    float fadeOpacity {0.0f};
    Vec3 fadeColor;
    bool movie {false};
    std::string movieName; // the movie the host is playing, from the client's own install
    uint8_t cameraType {0};
    uint32_t cameraId {0};
    ConversationState conversation;
    std::vector<PartyEntry> party;
    uint32_t leader {0};
    bool paused {false};
    std::string pausedBy; // player name
};

enum class EventKind : uint8_t {
    VfxBegin = 1,  // handle, row, a: target (0: at location), location, facing,
                   // b: beam source (0: none), flags: the source's BodyNode (0-4)
    VfxEnd = 2,    // handle
    Projectile = 3 // a: attacker, b: target, flags: bit0 offhand, bit1 miss
};

/** The largest BodyNode a VfxBegin may name (game::BodyNode::HandRight). */
constexpr uint8_t kMaxVfxBodyNode = 4;

struct NetEvent {
    EventKind kind {EventKind::VfxBegin};
    uint32_t handle {0};
    uint32_t row {0};
    uint32_t a {0};
    uint32_t b {0};
    Vec3 location;
    float facing {0.0f};
    uint8_t flags {0};
};

/**
 * The replicated state at one host tick, as a delta against the snapshot
 * of baseTick the client acknowledged (0: against nothing, a full one).
 * Sent unreliably: a lost one is superseded by the next, and the host keeps
 * sending against the last acknowledged base until a newer ack arrives.
 */
struct Snapshot {
    uint32_t tick {0};
    uint32_t generation {0};
    uint32_t baseTick {0};
    uint16_t worldFields {0};
    WorldState world;
    std::vector<EntityRecord> entities;

    bool full() const { return baseTick == 0; }
};

/** Client -> host: the newest snapshot the client has decoded and keeps as a base. */
struct SnapshotAck {
    uint32_t generation {0};
    uint32_t tick {0};
};

/** The events of one host tick, sent reliably next to its snapshot. */
struct EventBatch {
    uint32_t generation {0};
    uint32_t tick {0};
    std::vector<NetEvent> events;
};

// Input

enum class CommandType : uint8_t {
    MoveTo = 1,
    Attack = 2,
    UseObject = 3,
    OpenDoor = 4,
    Cancel = 5,
    UseItem = 6,
    UseFeat = 7,
    UsePower = 8,
    UseSkill = 9, // param: skill id, on a door, placeable or mine (Security on a locked door)
    // Session requests (N2): not orders for the creature, so they need no slot.
    Ready = 10,          // param: 1 ready, 0 not ready, for the running area-exit check
    Pause = 11,          // param: 1 pause, 0 resume
    TakeItem = 12,       // target: the container this client has open; param: host item id, 0 for all
    CloseContainer = 13, // target: the container this client has open
    PickSlot = 14,       // item: companion tag wanted when the current one is gone
    // Orders for the companion itself (co-op completeness)
    Equip = 15,          // param: host item id from the shared inventory, 0 to clear; item: the slot number
    LevelUp = 16,        // item: the level's choices (coopprogress.h)
    // Free Mode stores: target the store, param the host item id
    StoreBuy = 17,
    StoreSell = 18,
    StoreClose = 19,
    // Free Mode prompts (design, "Store and minigame prompts for non-hosts")
    OfferAnswer = 20,    // target: the NPC of the pending offer; param: 1 yes, 0 no
    PazaakResult = 21,   // param: the credits the guest won (+) or lost (-) at its own Pazaak table
    PickReply = 22,      // param: the reply row, in a terminal session the host handed to this player
    // Orders from the HUD and key binds (input audit, design note coop_input_audit)
    SwitchWeapons = 23,  // the weapon-set swap
    AutoCombat = 24,     // param: 1 on, 0 off (the HUD's AI toggle)
    SetMine = 25,        // param: host item id of the mine; point: where to lay it
    DisarmTrap = 26,     // target: the detected trap trigger to disarm
    // Workbenches and lab stations (Free and Story Mode guests); target is the bench this guest has open
    BenchUpgrade = 27,   // param: host id of the item to upgrade; item: decimal host id of the upgrade part to install
    BenchRemove = 28,    // param: host id of the item; item: decimal slot index (BenchItem::slots) to empty
    BenchCraft = 29,     // param: material (0 components, 1 chemicals); item: the recipe's template
    BenchBreak = 30,     // param: host id of the item to break down
    BenchClose = 31,
    MiniGameQuit = 32    // the owner of the running minigame leaves it
};

const char *commandTypeName(CommandType type);

/** One input command. The host checks it against the sender's creature. */
struct Command {
    uint32_t seq {0};
    CommandType type {CommandType::Cancel};
    uint32_t actor {0};  // host id of the creature the client claims to control
    uint32_t target {0}; // host id, 0 for none
    Vec3 point;
    bool run {true};
    int32_t param {0};  // feat, spell or skill id
    std::string item;   // item tag or template for UseItem
};

enum class CommandReject : uint8_t {
    None = 0,
    NoSlot = 1,          // the client controls nothing yet
    NotControlled = 2,   // actor is not the client's creature
    ActorUnavailable = 3, // dead, gone, or not in the area
    TargetInvalid = 4,
    TargetTooFar = 5,
    PointInvalid = 6,
    Busy = 7,            // a conversation, cutscene or load is running
    RateLimited = 8,
    NotSupported = 9,
    ItemUnavailable = 10,
    FeatUnavailable = 11,
    PowerUnavailable = 12,
    SkillUnavailable = 13,
    HostLeadsConversation = 14, // "The host leads conversations"
    StoreUnavailable = 15,      // a store or minigame NPC: no guest flow yet
    PauseDisabled = 16,         // host option "No pause in co-op"
    PauseCooldown = 17,         // host's per-player pause cooldown
    NoReadyCheck = 18,          // an answer with no check running
    HostOnlyTransitions = 19,   // host option "Host-only transitions"
    ContainerNotOpen = 20,      // take/close for a container this client does not have open
    LevelUpUnavailable = 21,    // no level due, or the host's level is the cap
    LevelUpInvalid = 22,        // a choice the rules do not allow
    EquipRejected = 23,         // the item does not go in that slot for this character
    NotHandedOver = 24,         // a story terminal the host has not handed to this player
    NotEnoughCredits = 25,      // Free Mode: the guest's own credits do not cover it
    StoreClosed = 26,           // Free Mode: no store open for this guest, or it does not trade that way
    NoOffer = 27,               // an answer or a result with no matching prompt
    GuestsCannotRace = 28,      // module minigames (swoop races, turrets) are the host's
    NotYourTerminal = 29,       // a reply pick with no terminal session handed to this player
    ClassLocked = 30,           // a level-up into a class the story or prestige unlock has not opened for this character
    ClassAlignment = 31,        // a prestige class the character's alignment does not qualify for
    ClassSlotsFull = 32,        // a level-up into a new class with no class slot free
    BenchClosed = 33,           // no workbench or lab open for this guest, or it does not do that
    UpgradeIncompatible = 34,   // the part does not fit the item, or the slot is taken
    NotEnoughMaterial = 35,     // not enough components or chemicals
    SkillTooLow = 36,           // the crafter's skill does not reach the recipe
    NotBreakable = 37,          // the item cannot be broken down, or has nothing to remove
    NotYourMiniGame = 38,       // a minigame command from a player who is not driving it
    MiniGameBusy = 39           // a minigame is running already, or the host does not allow guest minigames now
};

const char *commandRejectName(CommandReject reason);

struct CommandResult {
    uint32_t seq {0};
    bool accepted {false};
    CommandReject reason {CommandReject::None};
    uint32_t hostTick {0};
};

struct Bye {
    std::string reason;
};

enum class NoticeKind : uint8_t {
    Info = 0,
    Refusal = 1,
    Pause = 2,
    Defeat = 3, // the whole party is down; the host is choosing (text empty: it chose)
    // Free Mode terminals (design, "Computer terminals" item 5)
    TerminalRead = 4,   // read an info-only terminal on your own screen: "<host object id> <dialog>"
    TerminalControl = 5 // you make the choices in the host's terminal session now ("on"), or no longer ("off")
};

/** A line the client shows its player (a refusal, who paused). */
struct Notice {
    NoticeKind kind {NoticeKind::Info};
    std::string text;
};

struct ContainerItem {
    uint32_t id {0}; // host item id
    std::string resref;
    std::string tag;
    uint16_t stack {1};
};

/**
 * A container the client has open (design items 14-15). The host owns the
 * contents; it resends them whenever they change while the screen is open,
 * so every viewer's list stays live. open false closes the client's screen.
 */
struct ContainerView {
    uint32_t container {0};
    bool open {true};
    std::vector<ContainerItem> items;
};

enum class ReadyState : uint8_t {
    Idle = 0,      // no check running (the last one ended)
    Active = 1,    // waiting for answers
    Passed = 2,    // the party moves
    Cancelled = 3  // not enough players ready in time, or the exit went away
};

enum class ReadyAnswer : uint8_t {
    Pending = 0,
    Ready = 1,
    NotReady = 2
};

struct ReadyPlayer {
    std::string name;
    ReadyAnswer answer {ReadyAnswer::Pending};
    bool busy {false}; // in a menu or conversation: gets the prompt on return
};

/** The area-exit ready check (design item 8), sent whenever it changes. */
struct ReadyCheck {
    uint32_t id {0};
    ReadyState state {ReadyState::Idle};
    std::string initiator;
    std::string destination; // module name
    uint16_t secondsLeft {0};
    std::vector<ReadyPlayer> players;
};

/** InventoryItem::flags */
constexpr uint8_t kInventoryUsable = 1 << 0; // has an Activate Item power (medpacs, grenades, shields)

struct InventoryItem {
    uint32_t id {0}; // host item id
    std::string resref;
    uint16_t stack {1};
    uint8_t flags {0};
};

/** The shared party inventory (design item 4), resent whenever it changes. */
struct SharedInventory {
    uint32_t owner {0}; // the host creature holding it (the player character)
    int32_t credits {0};
    std::vector<InventoryItem> items;
};

struct SheetClass {
    int16_t clazz {0};
    int16_t level {0};
};

struct SheetEquipment {
    uint8_t slot {0};
    uint32_t id {0}; // host item id
    std::string resref;
};

/**
 * What the client's character, equipment and level-up screens need of the
 * creature it controls, resent whenever it changes. levelUp: a level is
 * due and the host's cap allows it (design item 5).
 */
struct CharacterSheet {
    uint32_t creature {0};
    int32_t xp {0};
    bool levelUp {false};
    std::vector<SheetClass> classes;
    uint8_t abilities[6] {};
    uint8_t skills[8] {};
    std::vector<uint16_t> feats;
    std::vector<uint16_t> spells;
    int16_t baseHp {0};
    int16_t baseFp {0};
    std::vector<SheetEquipment> equipment;
};

/** A guest's item: slot is an equipment slot, or kGuestInventory for the bag. */
constexpr uint8_t kGuestInventory = 255;

struct GuestItem {
    std::string resref;
    uint16_t stack {1};
    uint8_t slot {kGuestInventory};
    uint32_t upgrades {0};       // installed upgrade rows (upgrade.2da bits), set at a workbench
    int16_t modelVariation {-1}; // a lightsaber's blade colour from a colour crystal, or -1
};

/**
 * Free Mode: a guest's own character (design, "Guest characters"), saved on
 * the guest's machine in this same encoding. The host checks it on join and
 * sends it back as it changes, so the guest's file stays current.
 */
struct GuestCharacter {
    std::string name;
    int32_t appearance {0};
    uint8_t gender {0};
    int32_t portrait {-1};
    std::vector<SheetClass> classes;
    uint8_t abilities[6] {};
    uint8_t skills[8] {};
    std::vector<uint16_t> feats;
    std::vector<uint16_t> spells;
    int32_t xp {0};
    int32_t credits {0};
    int16_t baseHp {0};
    int16_t currentHp {0};
    int16_t baseFp {0};
    uint8_t goodEvil {50};
    std::vector<GuestItem> items;
    int32_t components {0}; // crafting material this guest holds (Free Mode: not the party's)
    int32_t chemicals {0};
};

struct StoreEntry {
    uint32_t id {0}; // host item id: the store's, or the guest's for selling
    std::string resref;
    uint16_t stack {1};
    int32_t price {0};
};

/** Free Mode: the store a guest trades with, resent after each trade; open false closes it. */
struct StoreView {
    uint32_t store {0};
    bool open {true};
    int32_t credits {0};
    std::vector<StoreEntry> stock;
    std::vector<StoreEntry> sellable;
};

constexpr size_t kMaxBenchItems = 128;
constexpr size_t kMaxBenchSlots = 8;
constexpr size_t kMaxBenchCandidates = 64;
constexpr size_t kMaxBenchRecipes = 192;
constexpr size_t kMaxBenchBreakdown = 256;
constexpr size_t kMaxBenchParts = 128;

enum class BenchKind : uint8_t {
    Workbench = 1, // upgrades, and crafting and breakdown of both materials
    Lab = 2        // crafting and breakdown with chemicals
};

/** A slot of an upgradeable item and the inventory parts that could go in it now. */
struct BenchSlot {
    uint8_t type {0};            // upgradetypes.2da row the slot takes
    bool color {false};          // a lightsaber's colour crystal slot
    int16_t installedRow {-1};   // upgrade.2da row in it, or -1
    std::vector<uint32_t> candidates; // host ids of BenchView::parts that fit
};

struct BenchItem {
    uint32_t id {0};
    std::string resref;
    uint8_t category {0}; // UpgradeCategory: 1 lightsaber, 2 ranged, 3 melee, 4 armor
    bool equipped {false};
    uint32_t upgrades {0};
    int16_t modelVariation {-1};
    std::vector<BenchSlot> slots;
};

struct BenchBreakdown {
    uint32_t id {0};
    std::string resref;
    uint16_t stack {1};
    uint16_t yield {0};   // what breaking one down gives this crafter
    uint8_t material {0}; // 0 components, 1 chemicals
};

struct BenchRecipe {
    std::string resref;
    uint8_t material {0};
    int32_t cost {0};
    bool skilled {false};
    bool affordable {false};
};

/**
 * Workbenches and lab stations for a guest: what they may upgrade, break down and make, all
 * computed on the host (Free Mode from the guest's own bag, Story Mode from the shared
 * inventory). Resent after each command; open false closes it.
 */
struct BenchView {
    uint32_t bench {0};
    BenchKind kind {BenchKind::Workbench};
    bool open {true};
    int32_t components {0};
    int32_t chemicals {0};
    std::vector<BenchItem> items;
    std::vector<StoreEntry> parts; // upgrade parts in the inventory (price unused, 0)
    std::vector<BenchBreakdown> breakdown;
    std::vector<BenchRecipe> recipes;
};

/** Limits on a minigame input: nothing outside them is ever sent. */
constexpr float kMaxMiniGameDelta = 45.0f; // degrees of one-shot aim in a message
constexpr size_t kMaxMiniGameActors = 160;
constexpr size_t kMaxMiniGameAnimations = 64;
constexpr size_t kMaxMiniGameSounds = 16;

/**
 * Client -> host: the input of the player who owns the running minigame (a guest racing
 * a swoop or aiming a turret the host handed over). The host uses the newest one it has;
 * input that stops arriving decays to neutral. Mirrors MiniGameInput (steer, fire, turret
 * aim directions and one-shot aim deltas).
 */
struct MiniGameControl {
    uint32_t seq {0};     // rises with each message; an old one is dropped
    float steer {0.0f};   // -1 left .. +1 right
    bool fire {false};
    float pitchRate {0.0f}; // -1 .. +1, the turret's aim keys
    float yawRate {0.0f};
    float pitchDelta {0.0f}; // degrees, one shot (mouse)
    float yawDelta {0.0f};
};

enum class MiniGameKind : uint8_t {
    SwoopRace = 1,
    Turret = 2
};

enum class MiniGameRole : uint8_t {
    Player = 0,
    Enemy = 1,   // a rival, a pad or a turret target
    Obstacle = 2
};

/** A unit rotation (x, y, z, w), sent as four 16-bit fractions. */
struct MiniGameRotation {
    float x {0.0f};
    float y {0.0f};
    float z {0.0f};
    float w {1.0f};
};

/** An animation the minigame's scripts played (or removed) on an actor's models since the last state. */
struct MiniGameAnimation {
    enum Flags : uint8_t {
        Looping = 1,
        Queue = 2,
        Overlay = 4,
        Remove = 8
    };
    uint16_t actor {0}; // index into MiniGameState::actors
    std::string name;
    uint8_t flags {0};
};

struct MiniGameActorState {
    MiniGameRole kind {MiniGameRole::Enemy};
    int16_t hp {0};
    bool dead {false};
    Vec3 position;
    MiniGameRotation rotation; // the actor's track frame (a swoop's hook, a fighter's heading)
};

/**
 * Host -> client: the running minigame, about ten times a second, for the client to draw
 * (and for the results). `owner` names the player driving it, empty for the host.
 */
struct MiniGameState {
    bool active {true}; // false once it has ended
    MiniGameKind kind {MiniGameKind::SwoopRace};
    std::string module;
    std::string owner;
    uint32_t tick {0};      // simulation ticks since it started
    float elapsed {0.0f};   // seconds
    float speed {0.0f};     // the swoop's current speed
    Vec3 offset;            // the player's offset from its track (the turret's aim in degrees)
    int16_t hp {0};
    int16_t maxHp {0};
    bool finished {false};  // the player's part is over (ended, or killed)
    bool quit {false};      // the owner left it
    bool firing {false};    // the player's fire input this tick (clients draw its shots)
    uint32_t controlSeq {0}; // the owner's newest MiniGameControl the host has used (0: none)
    int32_t prize {0};      // credits this player was paid, set on the final message
    std::vector<MiniGameActorState> actors;
    std::vector<MiniGameAnimation> animations; // in the order the scripts played them
    std::vector<std::string> sounds;           // sound resrefs the host played since the last state
};

struct GuestRosterEntry {
    std::string name;
    int16_t hp {0};
    int16_t maxHp {0};
    uint8_t level {1};
    bool present {true}; // in the world now (not away for a scene)
};

/** Free Mode: the guests the HUD lists beside the party portraits (design, "HUD"). */
struct GuestRoster {
    std::vector<GuestRosterEntry> entries;
};

enum class OfferKind : uint8_t {
    None = 0,
    Store = 1,   // "Browse store"
    Pazaak = 2,  // "Play Pazaak"
    MiniGame = 3 // a minigame module (swoop race, turret): the host's only
};

/** A prompt a guest gets instead of a conversation (design, "Store and minigame prompts"). */
struct Offer {
    OfferKind kind {OfferKind::None};
    uint32_t target {0}; // the NPC
    std::string name;    // its name, for the prompt
    int32_t maximumWager {0};
};

// Chat and pings

/** The longest chat line, in UTF-8 bytes, after the host has accepted it. */
constexpr size_t kMaxChatBytes = 200;
/** The longest chat line a client may put on the wire, so the host can answer "too long" instead of dropping it. */
constexpr size_t kMaxChatWireBytes = 512;
constexpr uint8_t kChatColours = 16; // colour indices 0..15; the palette is the client's
constexpr float kMaxPingCoordinate = 100000.0f;
constexpr float kMaxPingSeconds = 60.0f;

enum class ChatChannel : uint8_t {
    All = 0,
    Team = 1 // Free Mode: the sender's own side (party or guests); Story Mode: same as All
};

/** Client -> host. The text is raw: the host validates and cleans it. */
struct ChatSend {
    ChatChannel channel {ChatChannel::All};
    std::string text;
};

/** Host -> client. sender 0 is the host. The text is already valid UTF-8 without control characters. */
struct ChatMessage {
    uint32_t sender {0};
    std::string name;
    uint8_t colour {0};
    ChatChannel channel {ChatChannel::All};
    std::string text;
};

/** Client -> host: target 0 marks the point, otherwise the object (the host uses the object's own position). */
struct PingSend {
    Vec3 position;
    uint32_t target {0};
};

/** Host -> client: a marker for every player to draw for a few seconds. */
struct PingMarker {
    uint32_t sender {0};
    std::string name;
    uint8_t colour {0};
    Vec3 position;
    uint32_t target {0};
    std::string targetName; // empty for a bare point
    float seconds {0.0f};
};

/** True for well-formed UTF-8 (no overlong forms, surrogates or code points past U+10FFFF). */
bool validUtf8(const std::string &text);

/** A one-line bark on the host's HUD (BarkString and friends). */
struct Bark {
    std::string text;
    float duration {0.0f};
};

// Encoding. encode* returns the whole message, type byte included.

std::vector<uint8_t> encode(const Hello &message);
std::vector<uint8_t> encode(const Welcome &message);
std::vector<uint8_t> encode(const Reject &message);
std::vector<uint8_t> encode(const SlotRequest &message);
std::vector<uint8_t> encode(const SlotAssign &message);
std::vector<uint8_t> encode(const ModuleInfo &message);
std::vector<uint8_t> encode(const Snapshot &message);
std::vector<uint8_t> encode(const Command &message);
std::vector<uint8_t> encode(const CommandResult &message);
std::vector<uint8_t> encode(const Bye &message);
std::vector<uint8_t> encode(const Notice &message);
std::vector<uint8_t> encode(const ContainerView &message);
std::vector<uint8_t> encode(const ReadyCheck &message);
std::vector<uint8_t> encode(const SnapshotAck &message);
std::vector<uint8_t> encode(const EventBatch &message);
std::vector<uint8_t> encode(const SharedInventory &message);
std::vector<uint8_t> encode(const CharacterSheet &message);
std::vector<uint8_t> encode(const Bark &message);
std::vector<uint8_t> encode(const GuestCharacter &message);
std::vector<uint8_t> encode(const StoreView &message);
std::vector<uint8_t> encode(const GuestRoster &message);
std::vector<uint8_t> encode(const Offer &message);
std::vector<uint8_t> encode(const BenchView &message);
std::vector<uint8_t> encode(const MiniGameControl &message);
std::vector<uint8_t> encode(const MiniGameState &message);
std::vector<uint8_t> encode(const ChatSend &message);
std::vector<uint8_t> encode(const ChatMessage &message);
std::vector<uint8_t> encode(const PingSend &message);
std::vector<uint8_t> encode(const PingMarker &message);

// Decoding: false for anything malformed; the output is then unspecified.

bool decode(const std::vector<uint8_t> &bytes, Hello &out);
bool decode(const std::vector<uint8_t> &bytes, Welcome &out);
bool decode(const std::vector<uint8_t> &bytes, Reject &out);
bool decode(const std::vector<uint8_t> &bytes, SlotRequest &out);
bool decode(const std::vector<uint8_t> &bytes, SlotAssign &out);
bool decode(const std::vector<uint8_t> &bytes, ModuleInfo &out);
bool decode(const std::vector<uint8_t> &bytes, Snapshot &out);
bool decode(const std::vector<uint8_t> &bytes, Command &out);
bool decode(const std::vector<uint8_t> &bytes, CommandResult &out);
bool decode(const std::vector<uint8_t> &bytes, Bye &out);
bool decode(const std::vector<uint8_t> &bytes, Notice &out);
bool decode(const std::vector<uint8_t> &bytes, ContainerView &out);
bool decode(const std::vector<uint8_t> &bytes, ReadyCheck &out);
bool decode(const std::vector<uint8_t> &bytes, SnapshotAck &out);
bool decode(const std::vector<uint8_t> &bytes, EventBatch &out);
bool decode(const std::vector<uint8_t> &bytes, SharedInventory &out);
bool decode(const std::vector<uint8_t> &bytes, CharacterSheet &out);
bool decode(const std::vector<uint8_t> &bytes, Bark &out);
bool decode(const std::vector<uint8_t> &bytes, GuestCharacter &out);
bool decode(const std::vector<uint8_t> &bytes, StoreView &out);
bool decode(const std::vector<uint8_t> &bytes, GuestRoster &out);
bool decode(const std::vector<uint8_t> &bytes, Offer &out);
bool decode(const std::vector<uint8_t> &bytes, BenchView &out);
bool decode(const std::vector<uint8_t> &bytes, MiniGameControl &out);
bool decode(const std::vector<uint8_t> &bytes, MiniGameState &out);
bool decode(const std::vector<uint8_t> &bytes, ChatSend &out);
bool decode(const std::vector<uint8_t> &bytes, ChatMessage &out);
bool decode(const std::vector<uint8_t> &bytes, PingSend &out);
bool decode(const std::vector<uint8_t> &bytes, PingMarker &out);

// Entity deltas

/** Fields of cur that differ from prev beyond the replication tolerances. */
uint16_t diffEntity(const EntityState &prev, const EntityState &cur);

/**
 * Fields of cur that differ from prev at all (the anim serial included).
 * For states that only ever change by whole fields a host sent, such as
 * two decoded snapshots.
 */
uint16_t exactDiffEntity(const EntityState &prev, const EntityState &cur);

/** World fields of cur that differ from prev at all. */
uint16_t exactDiffWorld(const WorldState &prev, const WorldState &cur);

/** Copy the world fields named in mask from src into dst. */
void mergeWorld(WorldState &dst, const WorldState &src, uint16_t mask);

/** Copy the fields named in mask from src into dst. */
void mergeEntity(EntityState &dst, const EntityState &src, uint16_t mask);

/** Facing is sent as a u16 over [0, 2pi). */
uint16_t quantizeFacing(float radians);
float dequantizeFacing(uint16_t value);

} // namespace net

} // namespace reone
