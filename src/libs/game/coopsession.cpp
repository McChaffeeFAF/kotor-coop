/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/coopsession.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>

#include <boost/algorithm/string.hpp>

#include "reone/game/attack.h"
#include "reone/game/coopprogress.h"
#include "reone/game/coopreplication.h"
#include "reone/game/coopstandin.h"
#include "reone/game/game.h"
#include "reone/game/gui/computer.h"
#include "reone/game/gui/confirmpopup.h"
#include "reone/game/gui/container.h"
#include "reone/game/gui/conversation.h"
#include "reone/game/gui/dialog.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/game/script/runner.h"
#include "reone/game/visualeffects.h"
#include "reone/net/fingerprint.h"
#include "reone/net/joincode.h"
#include "reone/resource/dialog.h"
#include "reone/resource/gff.h"
#include "reone/resource/provider/dialogs.h"
#include "reone/scene/graph.h"
#include "reone/scene/graphs.h"
#include "reone/system/logutil.h"

using namespace reone::net;

namespace json = boost::json;

namespace reone {

namespace game {

namespace {

constexpr uint32_t kMaxViolations = 8;
constexpr size_t kMaxRejoinTokens = 256;
// Free Mode: characters kept for approved players who are away, for a rejoin.
constexpr size_t kMaxAbsentGuests = 32;
constexpr size_t kMaxPendingCommands = 32;
constexpr size_t kMaxResults = 64;
/** Animation drift, in seconds, past which the host resends the time. */
constexpr float kAnimResync = 0.25f;

std::string sanitizeName(const std::string &name) {
    std::string result;
    for (char c : name) {
        if (std::isprint(static_cast<unsigned char>(c)) && result.size() < 32) {
            result.push_back(c);
        }
    }
    return result.empty() ? std::string("Player") : result;
}

/** A name without the " (2)" suffix the host adds when two players share one. */
std::string baseName(const std::string &name) {
    auto open = name.rfind(" (");
    if (open != std::string::npos && name.size() > open + 3 && name.back() == ')') {
        bool digits = true;
        for (size_t i = open + 2; i + 1 < name.size(); ++i) {
            digits = digits && std::isdigit(static_cast<unsigned char>(name[i]));
        }
        if (digits) {
            return name.substr(0, open);
        }
    }
    return name;
}

json::array vec3Json(const Vec3 &v) {
    return json::array {v.x, v.y, v.z};
}

bool sameWorldFade(const WorldState &a, const WorldState &b) {
    return std::fabs(a.fadeOpacity - b.fadeOpacity) < 0.004f && a.fadeColor == b.fadeColor;
}

float lerpAngle(float from, float to, float alpha) {
    constexpr float kPi = 3.14159265358979f;
    float delta = std::fmod(to - from, 2.0f * kPi);
    if (delta > kPi) {
        delta -= 2.0f * kPi;
    } else if (delta < -kPi) {
        delta += 2.0f * kPi;
    }
    return from + delta * alpha;
}

} // namespace

const char *coopRoleName(CoopRole role) {
    switch (role) {
    case CoopRole::Host:
        return "host";
    case CoopRole::Client:
        return "client";
    default:
        return "none";
    }
}

namespace {

int64_t unixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string rejoinEntryKey(const std::string &sessionKey, const std::string &playerName) {
    return sessionKey + "|" + playerName;
}

boost::json::object readRejoinStore(const std::filesystem::path &store) {
    try {
        std::ifstream in(store, std::ios::binary);
        if (in) {
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            auto value = boost::json::parse(text);
            if (value.is_object()) {
                return value.as_object();
            }
        }
    } catch (const std::exception &) {
    }
    return {};
}

void writeRejoinStore(const std::filesystem::path &store, const boost::json::object &entries) {
    try {
        if (entries.empty()) {
            std::error_code ec;
            std::filesystem::remove(store, ec);
            return;
        }
        std::ofstream out(store, std::ios::binary | std::ios::trunc);
        out << boost::json::serialize(entries);
    } catch (const std::exception &) {
    }
}

} // namespace

uint64_t loadRejoinToken(const std::filesystem::path &store, const std::string &sessionKey, const std::string &playerName, int64_t now) {
    if (store.empty() || sessionKey.empty()) {
        return 0;
    }
    auto entries = readRejoinStore(store);
    bool changed = false;
    uint64_t token = 0;
    for (auto it = entries.begin(); it != entries.end();) {
        const auto *entry = it->value().if_object();
        const auto *expires = entry ? entry->if_contains("expires") : nullptr;
        if (!expires || !expires->is_int64() || expires->as_int64() <= now) {
            it = entries.erase(it);
            changed = true;
            continue;
        }
        ++it;
    }
    if (auto found = entries.find(rejoinEntryKey(sessionKey, playerName)); found != entries.end()) {
        const auto *text = found->value().as_object().if_contains("token");
        if (text && text->is_string()) {
            try {
                token = std::stoull(std::string(text->as_string()));
            } catch (const std::exception &) {
            }
        }
    }
    if (changed) {
        writeRejoinStore(store, entries);
    }
    return token;
}

void saveRejoinToken(const std::filesystem::path &store, const std::string &sessionKey, const std::string &playerName, uint64_t token, int64_t now) {
    if (store.empty() || sessionKey.empty() || !token) {
        return;
    }
    auto entries = readRejoinStore(store);
    boost::json::object entry;
    entry["token"] = std::to_string(token); // a string: JSON numbers lose 64-bit values
    entry["expires"] = now + kRejoinTokenLifetimeSeconds;
    entries[rejoinEntryKey(sessionKey, playerName)] = entry;
    writeRejoinStore(store, entries);
}

void forgetRejoinToken(const std::filesystem::path &store, const std::string &sessionKey, const std::string &playerName) {
    if (store.empty() || sessionKey.empty()) {
        return;
    }
    auto entries = readRejoinStore(store);
    if (entries.erase(rejoinEntryKey(sessionKey, playerName)) > 0) {
        writeRejoinStore(store, entries);
    }
}

CoopSession::CoopSession(Game &game, CoopConfig config, std::unique_ptr<ITransport> transport) :
    _game(game),
    _config(std::move(config)),
    _transport(std::move(transport)) {
    // Joins wait for the host's Accept / Decline when askToJoin is on
    // (hostPromptJoin); setJoinApprover replaces that with a rule.
    if (isClient()) {
        // A client never runs game scripts: the host owns every story decision.
        _game.scriptRunner().setSuppressed(true);
        // A restarted game proves who it is with the token it was given.
        _rejoinToken = loadRejoinToken(_config.rejoinStore, _config.sessionKey, _config.playerName, unixNow());
    } else if (isHost()) {
        // Party wipe (co-op design notes): the host chooses; clients are told and
        // follow whatever the host loads.
        _game.setPartyDefeatHandler([this]() { hostPartyDefeated(); });
    }
    _pauseCooldown.setSeconds(_config.pauseCooldown);
    _scale = coopEnemyScale(1, _config.enemyScaling);
    info(std::string("Co-op: ") + coopRoleName(_config.role) + " session, mode " + _config.mode + ", over " +
         (_transport ? _transport->describe() : std::string("no transport")));
}

CoopSession::~CoopSession() {
    if (_transport) {
        Bye bye {"session ended"};
        auto bytes = encode(bye);
        if (isHost()) {
            for (auto &[connection, client] : _clients) {
                _transport->send(connection, bytes, SendMode::Reliable);
            }
        } else if (_hostConnection) {
            _transport->send(_hostConnection, bytes, SendMode::Reliable);
        }
    }
    for (auto &projectile : _projectiles) {
        if (projectile.projectile) {
            projectile.projectile->reset();
        }
    }
    if (isClient()) {
        // Back to single-player: the game's own scripts run again.
        _game.scriptRunner().setSuppressed(false);
    } else if (isHost()) {
        _game.setPartyDefeatHandler(nullptr); // the single-player death screen again
    }
    if (_joinPromptClient && _game._confirmPopup && _game._confirmPopup->isVisible()) {
        _game._confirmPopup->hide();
    }
}

void CoopSession::send(ConnectionId connection, const std::vector<uint8_t> &data) {
    if (_transport) {
        _transport->send(connection, data, SendMode::Reliable);
    }
}

// Network

void CoopSession::pollNetwork() {
    if (isClient() && _moduleHeld && _guestSent) {
        _moduleHeld = false;
        clientEnterModule();
        if (_game.currentScreen() == Game::Screen::CharacterGeneration) {
            _game.openInGame(); // the guest's character is made: into the host's world
        }
    }
    if (isClient() && _clientState == ClientState::Reconnecting) {
        clientUpdateReconnect();
    }
    if (!_transport) {
        return;
    }
    std::vector<TransportEvent> events;
    _transport->poll(events);
    for (auto &event : events) {
        if (isHost()) {
            switch (event.type) {
            case TransportEventType::Connected:
                hostHandleConnected(event);
                break;
            case TransportEventType::Disconnected:
                hostHandleDisconnected(event);
                break;
            case TransportEventType::Message: {
                auto it = _clients.find(event.connection);
                if (it == _clients.end()) {
                    break;
                }
                // The transport refuses an oversized message before copying it;
                // a transport that does not (loopback) is checked here.
                size_t size = event.oversizedBytes ? event.oversizedBytes : event.data.size();
                if (event.oversizedBytes || size > clientMessageLimit(event.data.empty() ? 0 : event.data[0])) {
                    hostViolation(it->second, "oversized message (" + std::to_string(size) + " bytes)");
                    break;
                }
                hostHandleMessage(it->second, event.data);
                break;
            }
            }
        } else {
            switch (event.type) {
            case TransportEventType::Connected: {
                _hostConnection = event.connection;
                _clientState = ClientState::Handshaking;
                clientSendHello();
                info("Co-op: connected to host " + event.reason + "; handshaking");
                break;
            }
            case TransportEventType::Disconnected:
                clientHandleDisconnect(event.reason);
                break;
            case TransportEventType::Message:
                _bytesReceived += event.data.size();
                clientHandleMessage(event.data);
                break;
            }
            if (_clientState == ClientState::Reconnecting && !_transport) {
                break; // the transport is gone; the rest of its events with it
            }
        }
    }
    if (isHost()) {
        hostResendSnapshots();
        hostPromptJoin();
    } else if (_everJoined && !_lostShown && showsPrompts() &&
               (_clientState == ClientState::Disconnected || _clientState == ClientState::Rejected) &&
               _game.currentScreen() == Game::Screen::InGame) {
        // The game is gone for good (the host quit, or the reconnect window
        // passed): say so, then back to the main menu.
        if (!_game._confirmPopup) {
            _game._confirmPopup = _game.tryLoadGUI<ConfirmPopup>();
        }
        if (_game._confirmPopup && !_game._confirmPopup->isVisible()) {
            _lostShown = true;
            Game &game = _game;
            _game._confirmPopup->showChoices(failureMessage(), {{"OK", [&game]() { game.endCoopSession(); }}});
        }
    }
}

void CoopSession::clientSendHello() {
    Hello hello;
    hello.build = _config.build;
    hello.mode = _config.mode;
    hello.playerName = _config.playerName;
    hello.k1 = _config.k1;
    hello.k2 = _config.k2;
    hello.rejoinToken = _rejoinToken;
    send(_hostConnection, encode(hello));
}

namespace {

constexpr auto kReconnectWindow = std::chrono::seconds(90);
constexpr auto kReconnectFirst = std::chrono::milliseconds(1500);
constexpr auto kReconnectEvery = std::chrono::seconds(4);
/**
 * How long one attempt may take before the next replaces it. A relayed ICE
 * connection takes several seconds to come up (a TURN allocation, then the
 * checks through it); GNS gives up on its own after 20 s.
 */
constexpr auto kReconnectAttemptBudget = std::chrono::seconds(25);

/** Service failures that another try cannot fix. */
bool finalServiceFailure(const std::string &reason) {
    return reason.rfind("service:bad_code", 0) == 0 || reason.rfind("service:version", 0) == 0 ||
           reason.rfind("service:bad_request", 0) == 0;
}

} // namespace

void CoopSession::clientHandleDisconnect(const std::string &reason) {
    _hostConnection = 0;
    _disconnectReason = reason;
    if (_clientState == ClientState::Rejected) {
        warn("Co-op: disconnected from host after a rejection: " + reason);
        return;
    }
    auto now = std::chrono::steady_clock::now();
    bool retry = _everJoined && _reconnect && !_hostEnded && !finalServiceFailure(reason);
    if (!retry) {
        _clientState = ClientState::Disconnected;
        _reconnecting = false;
        warn("Co-op: disconnected from host: " + reason);
        return;
    }
    if (!_reconnecting) {
        // Design item 10: meanwhile the AI plays the companion on the host.
        _reconnecting = true;
        _reconnectDeadline = now + kReconnectWindow;
        _nextReconnect = now + kReconnectFirst;
        showNotice(NoticeKind::Info, "Connection to the host lost: reconnecting...");
        warn("Co-op: lost the host (" + reason + "); reconnecting");
    } else if (now >= _reconnectDeadline) {
        _clientState = ClientState::Disconnected;
        _reconnecting = false;
        warn("Co-op: giving up on the host: " + reason);
        return;
    } else {
        _nextReconnect = now + kReconnectEvery; // this attempt failed: the next one soon
    }
    _clientState = ClientState::Reconnecting;
}

void CoopSession::simulateDrop() {
    if (isClient() && _transport && _hostConnection) {
        _transport->close(_hostConnection, "simulated drop");
    }
}

void CoopSession::clientUpdateReconnect() {
    auto now = std::chrono::steady_clock::now();
    if (now < _nextReconnect) {
        return;
    }
    if (now >= _reconnectDeadline) {
        _clientState = ClientState::Disconnected;
        _reconnecting = false;
        warn("Co-op: could not get back to the host in time");
        return;
    }
    _transport.reset();
    std::string error;
    auto transport = _reconnect(error);
    ++_reconnects;
    if (!transport) {
        warn("Co-op: reconnect attempt failed: " + error);
        _nextReconnect = now + kReconnectEvery;
        return;
    }
    info("Co-op: reconnect attempt " + std::to_string(_reconnects));
    _transport = std::move(transport);
    // The attempt runs until it connects or fails (clientHandleDisconnect
    // brings the next one forward); only a stuck one is replaced.
    _nextReconnect = now + kReconnectAttemptBudget;
    // Stay Reconnecting until the host welcomes us back; a failed attempt
    // lands in clientHandleDisconnect and schedules the next one.
}

// Host

void CoopSession::hostHandleConnected(const TransportEvent &event) {
    HostClient client;
    client.connection = event.connection;
    client.clientId = _nextClientId++;
    client.address = event.reason;
    _clients[event.connection] = std::move(client);
    info("Co-op: client connected from " + event.reason + "; awaiting hello");
}

void CoopSession::hostHandleDisconnected(const TransportEvent &event) {
    hostDropClient(event.connection, event.reason);
}

void CoopSession::hostDropClient(ConnectionId connection, const std::string &reason) {
    auto it = _clients.find(connection);
    if (it == _clients.end()) {
        return;
    }
    auto &client = it->second;
    info("Co-op: client " + std::to_string(client.clientId) + " (" + client.name + ") left: " + reason);
    if (_joinPromptClient == client.clientId) {
        _joinPromptClient = 0;
        if (_game._confirmPopup && _game._confirmPopup->isVisible()) {
            _game._confirmPopup->hide();
        }
    }
    if (client.welcomed && client.slot.status == SlotStatus::Assigned) {
        // Design item 10: the AI takes the companion over (it is no longer
        // player-controlled, so its heartbeat and combat scripts run again),
        // and the same player gets it back on rejoin.
        _reservations[client.name] = client.slot.tag;
        info("Co-op: the AI takes over " + client.slot.tag + " until " + client.name + " rejoins");
    } else if (client.welcomed) {
        // Nothing here is worth coming back for, so the token is not kept: joining
        // and leaving over and over cannot fill the table real players need.
        bool keep = client.approval == HostClient::Approval::Accepted || client.guestId ||
                    client.slot.status == SlotStatus::StandIn || client.slot.status == SlotStatus::Guest;
        if (!keep) {
            hostForgetRejoinToken(client);
        }
    }
    if (auto owner = _guestOwners.find(client.name); owner != _guestOwners.end() && owner->second == client.rejoinToken &&
                                                     !client.approved) {
        // Free Mode: a character the host never let in is not kept.
        hostRetireGuest(client.name, false);
    }
    if (_ready.active()) {
        _ready.removePlayer(client.clientId);
    }
    if (_miniGame.running() && _miniGame.client() == client.clientId) {
        // The player who was steering is gone: the controls go back to the host.
        _miniGame.handOver(0, std::string());
    }
    if (_pendingMiniGame.client == client.clientId) {
        _pendingMiniGame = PendingMiniGame();
    }
    if (client.standIn) {
        // The stand-in goes with its player; a rejoin brings a new one.
        hostDespawnStandIn(client, false);
    }
    if (client.guestId) {
        // A guest's character leaves with its player; the host keeps its state for a rejoin.
        hostDespawnGuest(client);
    }
    if (client.viewing) {
        if (auto placeable = _game.getObjectById<Placeable>(client.viewing); placeable && client.slot.status == SlotStatus::Assigned) {
            placeable->signalClosed(client.slot.creatureId);
        }
    }
    _clients.erase(it);
    if (!_guests.empty()) {
        hostTrimAbsentGuests();
    }
}

void CoopSession::hostViolation(HostClient &client, const std::string &what) {
    ++client.violations;
    warn("Co-op: client " + std::to_string(client.clientId) + " sent " + what);
    if (client.violations >= kMaxViolations) {
        hostReject(client, RejectReason::Kicked, "too many malformed messages");
    }
}

void CoopSession::hostReject(HostClient &client, RejectReason reason, const std::string &detail) {
    Reject reject;
    reject.reason = reason;
    reject.detail = detail.substr(0, kMaxStringBytes);
    send(client.connection, encode(reject));
    warn(std::string("Co-op: rejecting client ") + std::to_string(client.clientId) + ": " + rejectReasonName(reason) +
         (detail.empty() ? "" : " (" + detail + ")"));
    auto connection = client.connection;
    _transport->close(connection, rejectReasonName(reason));
    // The Disconnected event the transport queues removes the entry.
}

void CoopSession::hostHandleMessage(HostClient &client, const std::vector<uint8_t> &data) {
    auto type = peekType(data);
    if (!type || !isClientMessage(*type)) {
        hostViolation(client, "a message type a client may not send");
        return;
    }
    switch (*type) {
    case MessageType::Hello: {
        Hello hello;
        if (client.welcomed || !decode(data, hello)) {
            hostViolation(client, client.welcomed ? "a second hello" : "a malformed hello");
            if (!client.welcomed) {
                hostReject(client, RejectReason::Malformed, "hello");
            }
            return;
        }
        hostHandleHello(client, hello);
        break;
    }
    case MessageType::SlotRequest: {
        SlotRequest request;
        if (!client.welcomed || !decode(data, request)) {
            hostViolation(client, "a slot request out of turn or malformed");
            return;
        }
        client.slotRequested = true;
        client.preferredTag = request.preferredTag;
        info("Co-op: client " + std::to_string(client.clientId) + " asks for a companion slot" +
             (request.preferredTag.empty() ? std::string() : " (" + request.preferredTag + ")"));
        hostUpdateSlots();
        break;
    }
    case MessageType::Command: {
        Command command;
        if (!client.welcomed || !decode(data, command)) {
            hostViolation(client, "a command out of turn or malformed");
            return;
        }
        // Commands arrive in order, so one at or below the last is a replay or a
        // stray old packet: running it again would redo what the player already did.
        if (client.sawCommand && command.seq <= client.lastCommandSeq) {
            hostViolation(client, "a replayed or out-of-order command");
            return;
        }
        client.sawCommand = true;
        client.lastCommandSeq = command.seq;
        if (client.pending.size() >= kMaxPendingCommands) {
            CommandResult result {command.seq, false, CommandReject::RateLimited, static_cast<uint32_t>(_game.tickCount())};
            send(client.connection, encode(result));
            ++client.commandsRejected;
            ++client.rejectCounts[static_cast<uint8_t>(CommandReject::RateLimited)];
            return;
        }
        client.pending.push_back(command);
        break;
    }
    case MessageType::MiniGameControl: {
        MiniGameControl control;
        if (!client.welcomed || !decode(data, control)) {
            hostViolation(client, "a minigame input out of turn or malformed");
            return;
        }
        hostHandleMiniGameControl(client, control);
        break;
    }
    case MessageType::SnapshotAck: {
        SnapshotAck ack;
        if (!client.welcomed || !decode(data, ack)) {
            hostViolation(client, "a snapshot ack out of turn or malformed");
            return;
        }
        hostHandleAck(client, ack);
        break;
    }
    case MessageType::GuestCharacter: {
        GuestCharacter guest;
        if (!client.welcomed || !freeMode() || !decode(data, guest)) {
            hostViolation(client, "a guest character out of turn or malformed");
            return;
        }
        hostHandleGuest(client, guest);
        break;
    }
    case MessageType::Bye: {
        Bye bye;
        decode(data, bye);
        info("Co-op: client " + std::to_string(client.clientId) + " says bye: " + bye.reason);
        _transport->close(client.connection, "bye");
        break;
    }
    default:
        break;
    }
}

void CoopSession::hostHandleAck(HostClient &client, const SnapshotAck &ack) {
    if (ack.generation != client.moduleGeneration || ack.tick <= client.ackedTick) {
        return; // an old module visit, or a late duplicate
    }
    auto it = std::find_if(client.history.begin(), client.history.end(), [&](const auto &sent) { return sent.tick == ack.tick; });
    if (it == client.history.end()) {
        if (client.history.empty() || ack.tick > client.history.back().tick) {
            hostViolation(client, "an ack for a snapshot never sent");
        }
        return; // already dropped from the history: a newer ack has superseded it
    }
    client.ackedTick = ack.tick;
    ++client.acks;
    // Everything older than the new base is no use as a base any more.
    client.history.erase(client.history.begin(), it);
}

void CoopSession::hostHandleHello(HostClient &client, const Hello &hello) {
    if (hello.protocol != kProtocolVersion) {
        hostReject(client, RejectReason::ProtocolMismatch,
                   "host protocol " + std::to_string(kProtocolVersion) + ", client " + std::to_string(hello.protocol));
        return;
    }
    if (hello.build != _config.build) {
        hostReject(client, RejectReason::BuildMismatch, "host build " + _config.build);
        return;
    }
    if (!hello.mode.empty() && hello.mode != _config.mode) {
        hostReject(client, RejectReason::ModeMismatch, "host mode " + _config.mode);
        return;
    }
    // The BIF index and the modules must match; dialog.tlk is the game's language and may
    // differ (a French player and a German one play the same modules and scripts).
    if (!installsCompatible(_config.k1, hello.k1)) {
        hostReject(client, RejectReason::FilesMismatchK1, "K1 install differs: " + fingerprintDifference(_config.k1, hello.k1));
        return;
    }
    if (tlkDiffers(_config.k1, hello.k1)) {
        warn("Co-op: " + hello.playerName + "'s K1 dialog.tlk differs (another language or a text mod); joining anyway");
    }
    // K2 only supplies names, menus and the merged feat and power tables here;
    // the host simulates and validates everything, and no K2 module is ever
    // loaded. A different K2 install (a mod, another language, another store
    // build) therefore joins with a warning instead of being refused.
    bool k2Differs = (_config.k2.present || hello.k2.present) && hello.k2 != _config.k2;
    if (k2Differs) {
        warn("Co-op: " + hello.playerName + "'s K2 install differs (" + fingerprintDifference(_config.k2, hello.k2) +
             "); joining anyway");
    }
    // A player coming back after a drop may still have a connection here the
    // host has not timed out yet: the token they were given proves it is
    // theirs, so it goes now and its companion is kept for them. The token is
    // theirs only under the name it was issued to; anyone else presenting it
    // gets no say over that player's connection.
    std::string requestedName = sanitizeName(hello.playerName);
    auto token = hello.rejoinToken ? _rejoinTokens.find(hello.rejoinToken) : _rejoinTokens.end();
    bool owner = token != _rejoinTokens.end() && baseName(token->second.name) == requestedName;
    if (owner) {
        std::vector<ConnectionId> stale;
        for (const auto &[connection, other] : _clients) {
            if (connection != client.connection && other.welcomed && other.rejoinToken == hello.rejoinToken) {
                stale.push_back(connection);
            }
        }
        for (auto connection : stale) {
            // Its token stays: the same player is taking the connection over.
            // That carries no trust: a join still waiting for the host stays
            // waiting on the new connection (the token's approved flag decides).
            _clients[connection].replaced = true;
            _transport->close(connection, "replaced by the same player reconnecting");
            hostDropClient(connection, "replaced by the same player reconnecting");
        }
    }
    int welcomed = static_cast<int>(std::count_if(_clients.begin(), _clients.end(), [](const auto &entry) {
        return entry.second.welcomed;
    }));
    if (welcomed >= _config.maxClients) {
        hostReject(client, RejectReason::SessionFull, std::string(freeMode() ? "Free Mode" : "Story Mode") + " takes " + std::to_string(_config.maxClients + 1) + " players");
        return;
    }
    auto nameTaken = [&](const std::string &name) {
        return std::any_of(_clients.begin(), _clients.end(), [&](const auto &entry) {
            return entry.second.welcomed && entry.second.name == name;
        });
    };
    // A name belongs to the player a token was issued to for as long as that
    // player is away: nobody else is given it, so they can still come back to it.
    auto heldForAbsentPlayer = [&](const std::string &name) {
        for (const auto &[issued, owned] : _rejoinTokens) {
            if (owned.name != name || (owner && issued == hello.rejoinToken)) {
                continue;
            }
            bool here = std::any_of(_clients.begin(), _clients.end(), [&](const auto &entry) {
                return entry.second.welcomed && entry.second.rejoinToken == issued;
            });
            if (!here) {
                return true;
            }
        }
        return false;
    };
    client.name = requestedName;
    if (owner && !nameTaken(token->second.name)) {
        client.name = token->second.name; // the name the token was issued to, suffix and all
    }
    // Names identify a player on rejoin, so two connected players never share one.
    for (int suffix = 2; nameTaken(client.name) || heldForAbsentPlayer(client.name); ++suffix) {
        client.name = requestedName + " (" + std::to_string(suffix) + ")";
    }
    client.welcomed = true;
    if (owner && token->second.name == client.name) {
        // The same player, back: same token. No Accept / Decline again, but
        // only if the host let them in before; a pending join stays pending.
        client.rejoinToken = token->first;
        client.trusted = token->second.approved;
        client.approved = client.trusted;
    } else {
        hostIssueRejoinToken(client);
    }
    Welcome welcome;
    welcome.clientId = client.clientId;
    welcome.hostTick = static_cast<uint32_t>(_game.tickCount());
    welcome.tickRate = static_cast<uint16_t>(_game.tickRate());
    welcome.mode = _config.mode;
    welcome.rejoinToken = client.rejoinToken;
    send(client.connection, encode(welcome));
    info("Co-op: welcomed client " + std::to_string(client.clientId) + " (" + client.name + ")");
    if (_ready.active()) {
        CoopReadyCheck::Player player;
        player.key = client.clientId;
        player.name = client.name;
        _ready.addPlayer(player);
        hostSendReady(client, true);
    }
    if (hasHostModule()) {
        hostSendModuleInfo(client);
    }
}

void CoopSession::hostSendModuleInfo(HostClient &client) {
    auto module = _game.module();
    if (!module) {
        return;
    }
    net::ModuleInfo message;
    message.generation = _generation;
    message.module = module->name();
    if (auto player = _game.party().player()) {
        message.player.hostId = player->id();
        message.player.appearance = player->appearance();
        message.player.gender = static_cast<uint8_t>(player->gender());
        message.player.name = player->name().substr(0, kMaxStringBytes);
        message.player.portrait = player->portraitId();
    }
    send(client.connection, encode(message));
    client.moduleGeneration = _generation;
    // A new module visit starts from nothing: the first snapshot is full.
    client.history.clear();
    client.ackedTick = 0;
    info("Co-op: client " + std::to_string(client.clientId) + " -> module " + message.module + " (generation " +
         std::to_string(_generation) + ")");
}

void CoopSession::hostDetectModuleChange() {
    auto module = _game.module();
    std::shared_ptr<Module> current = module && module->area() ? module : nullptr;
    if (current == _hostModule.lock()) {
        return;
    }
    _hostModule = current;
    if (!current) {
        return;
    }
    ++_generation;
    if (_hostDefeated) {
        // The host chose a load: everyone follows it (the ModuleInfo below).
        _hostDefeated = false;
        info("Co-op: the host loaded after the party wipe; clients follow");
    }
    _hostVfx.clear();
    _pendingEvents.clear();
    _scaled.clear();
    for (auto &[connection, client] : _clients) {
        client.viewing = 0;
        client.viewSent.clear();
        client.insideExit = 0;
        // The stand-in and a guest's creature stayed with the old module's objects.
        client.standIn = 0;
        client.standInHp = 0;
        client.guestId = 0;
        client.storeOpen = 0;
        client.benchOpen = 0;
        client.benchSent.clear();
        // The client loads the new module: resend what its screens show.
        client.inventorySent.clear();
        client.sheetSent.clear();
    }
    _standInSource.clear();
    _lootBaseline.clear();
    if (_ready.active()) {
        _ready.reset();
    }
    for (auto &[connection, client] : _clients) {
        if (client.welcomed) {
            hostSendModuleInfo(client);
        }
    }
}

bool CoopSession::isPlayerControlled(uint32_t creatureId) const {
    if (!isHost() || creatureId == 0) {
        return false;
    }
    for (const auto &[connection, client] : _clients) {
        if (client.welcomed && client.slot.status == SlotStatus::Assigned && client.slot.creatureId == creatureId) {
            return true;
        }
        if (client.welcomed && (client.standIn == creatureId || client.guestId == creatureId)) {
            return true;
        }
    }
    return false;
}

bool CoopSession::isReservedForOther(const std::string &tag, const HostClient &client) const {
    for (const auto &[player, reserved] : _reservations) {
        if (!boost::iequals(reserved, tag)) {
            continue;
        }
        // Names are not identities: only the client that proved itself with the
        // rejoin token issued to that name may take its companion back.
        if (player != client.name || !client.trusted) {
            return true;
        }
    }
    return false;
}

void CoopSession::hostIssueRejoinToken(HostClient &client) {
    std::random_device device;
    do {
        client.rejoinToken = (static_cast<uint64_t>(device()) << 32) | device();
    } while (!client.rejoinToken || _rejoinTokens.count(client.rejoinToken));
    if (_rejoinTokens.size() >= kMaxRejoinTokens) {
        // Make room by forgetting the oldest token whose player is not here now.
        for (auto it = _rejoinTokenOrder.begin(); it != _rejoinTokenOrder.end(); ++it) {
            uint64_t oldest = *it;
            bool inUse = std::any_of(_clients.begin(), _clients.end(), [&](const auto &entry) {
                return entry.second.welcomed && entry.second.rejoinToken == oldest;
            });
            if (inUse) {
                continue;
            }
            hostRetireToken(oldest);
            break;
        }
    }
    if (_rejoinTokens.size() < kMaxRejoinTokens) {
        _rejoinTokens[client.rejoinToken] = RejoinToken {client.name, false};
        _rejoinTokenOrder.push_back(client.rejoinToken);
    }
}

void CoopSession::hostMarkApproved(HostClient &client) {
    client.approved = true;
    if (auto token = _rejoinTokens.find(client.rejoinToken); token != _rejoinTokens.end() && token->second.name == client.name) {
        token->second.approved = true;
    }
}

bool CoopSession::hostApproved(const HostClient &client) const {
    // With nobody to ask, everyone who is welcomed is in.
    return client.approved || client.trusted || (!_config.askToJoin && !_approver);
}

void CoopSession::hostForgetRejoinToken(const HostClient &client) {
    if (!client.rejoinToken || client.replaced) {
        return;
    }
    auto token = _rejoinTokens.find(client.rejoinToken);
    if (token == _rejoinTokens.end() || token->second.approved) {
        return;
    }
    hostRetireToken(client.rejoinToken);
}

void CoopSession::hostRetireToken(uint64_t token) {
    if (auto entry = _rejoinTokens.find(token); entry != _rejoinTokens.end()) {
        const auto &name = entry->second.name;
        _reservations.erase(name);
        if (auto owner = _guestOwners.find(name); owner != _guestOwners.end() && owner->second == token) {
            // The character and its loot go with the identity: whoever takes
            // the name next brings their own.
            hostRetireGuest(name, true);
        }
        _rejoinTokens.erase(entry);
    }
    _rejoinTokenOrder.erase(std::remove(_rejoinTokenOrder.begin(), _rejoinTokenOrder.end(), token), _rejoinTokenOrder.end());
}

void CoopSession::hostRetireGuest(const std::string &name, bool loot) {
    _guests.erase(name);
    _guestOwners.erase(name);
    if (loot) {
        _lootLedger.forgetGuest(name);
    }
}

void CoopSession::hostTrimAbsentGuests() {
    auto present = [&](uint64_t token) {
        return token && std::any_of(_clients.begin(), _clients.end(), [&](const auto &entry) {
            return entry.second.welcomed && entry.second.rejoinToken == token;
        });
    };
    // A record whose token is gone cannot be claimed by anyone: it goes now.
    for (auto it = _guests.begin(); it != _guests.end();) {
        auto owner = _guestOwners.find(it->first);
        uint64_t token = owner != _guestOwners.end() ? owner->second : 0;
        auto name = it->first;
        ++it;
        if (!present(token) && !_rejoinTokens.count(token)) {
            hostRetireGuest(name, true);
        }
    }
    auto absent = [&]() {
        size_t count = 0;
        for (const auto &[name, token] : _guestOwners) {
            count += present(token) ? 0 : 1;
        }
        return count;
    };
    // Oldest first, the way the token table makes room.
    for (size_t i = 0; i < _rejoinTokenOrder.size() && absent() > kMaxAbsentGuests;) {
        uint64_t token = _rejoinTokenOrder[i];
        auto entry = _rejoinTokens.find(token);
        bool owns = entry != _rejoinTokens.end() && _guestOwners.count(entry->second.name) &&
                    _guestOwners.at(entry->second.name) == token;
        if (owns && !present(token)) {
            hostRetireToken(token); // erases from the order: i now points at the next
        } else {
            ++i;
        }
    }
}

void CoopSession::hostTakeControl(Creature &creature) {
    // Drop what the party AI queued (a follow, a combat round) so the
    // player's first order is not fighting it; a scene's actions stay.
    if (creature.isCommandable() && !creature.isInConversation()) {
        creature.clearAllActions();
    }
}

void CoopSession::hostUpdateSlots() {
    auto &party = _game.party();
    std::set<uint32_t> taken;
    for (auto &[connection, client] : _clients) {
        if (client.slot.status == SlotStatus::Assigned) {
            taken.insert(client.slot.creatureId);
        }
    }
    for (auto &[connection, client] : _clients) {
        if (!client.welcomed || !client.slotRequested || client.slot.status == SlotStatus::Declined) {
            continue;
        }
        if (freeMode()) {
            // Free Mode: guests play their own characters and never take the
            // host's companions (design, "Party and scripts").
            if (!client.slotAnswered) {
                client.slotAnswered = true;
                client.slot.status = SlotStatus::Waiting;
                hostSendSlot(client, false);
            }
            continue;
        }
        auto previous = client.slot;
        if (client.slot.status == SlotStatus::Assigned) {
            auto creature = _game.getObjectById<Creature>(client.slot.creatureId);
            if (!creature || !party.isMember(*creature)) {
                // A load (a retry after a party wipe) makes new objects of
                // the same companions: the player keeps theirs.
                for (const auto &member : party.members()) {
                    if (member.npc != kNpcPlayer && member.creature && !taken.count(member.creature->id()) &&
                        boost::iequals(member.creature->tag(), client.slot.tag)) {
                        creature = member.creature;
                        taken.erase(client.slot.creatureId);
                        client.slot.creatureId = creature->id();
                        client.slot.npc = member.npc;
                        taken.insert(creature->id());
                        hostTakeControl(*creature);
                        info("Co-op: client " + std::to_string(client.clientId) + " keeps " + client.slot.tag +
                             " (now id " + std::to_string(creature->id()) + ")");
                        break;
                    }
                }
            }
            if (!creature || !party.isMember(*creature)) {
                taken.erase(client.slot.creatureId);
                client.slot = SlotAssign();
                client.slot.status = SlotStatus::Released;
                info("Co-op: client " + std::to_string(client.clientId) + "'s companion left the party");
            }
        }
        if (client.slot.status != SlotStatus::Assigned) {
            std::shared_ptr<Creature> choice;
            int choiceNpc = -1;
            // A rejoining player gets their companion back first.
            // Only the player who proves it with the rejoin token gets theirs back.
            auto reserved = client.trusted ? _reservations.find(client.name) : _reservations.end();
            std::string wanted = reserved != _reservations.end() ? reserved->second : client.preferredTag;
            for (const auto &member : party.members()) {
                if (member.npc == kNpcPlayer || !member.creature || taken.count(member.creature->id())) {
                    continue;
                }
                if (isReservedForOther(member.creature->tag(), client)) {
                    continue;
                }
                bool preferred = !wanted.empty() && boost::iequals(member.creature->tag(), wanted);
                if (!choice || preferred) {
                    choice = member.creature;
                    choiceNpc = member.npc;
                }
                if (preferred) {
                    break;
                }
            }
            // Design, "Handshake" step 4: the host accepts or declines a new
            // player before they take a companion. A rule set through
            // setJoinApprover answers at once; otherwise, with askToJoin,
            // the request waits for the prompt (hostPromptJoin) or coopapprove.
            bool waitForHost = false;
            bool declined = false;
            // With no companion free the player gets a stand-in (design item
            // 12), which the host accepts or declines the same way.
            auto leader = party.getLeader();
            if (choice || (leader && client.approval == HostClient::Approval::None && !client.trusted)) {
                CoopJoinRequest request {client.clientId, client.name, choice ? choice->tag() : std::string()};
                if (_approver) {
                    declined = !_approver(request);
                    if (!choice && !declined) {
                        client.approval = HostClient::Approval::Accepted;
                        hostMarkApproved(client);
                    }
                } else if (_config.askToJoin && !client.trusted && client.approval != HostClient::Approval::Accepted) {
                    if (client.approval == HostClient::Approval::None) {
                        client.approval = HostClient::Approval::Pending;
                        client.approvalTag = choice ? choice->tag() : std::string();
                        client.approvalName = choice ? choice->name() : "a stand-in of " + leader->name();
                        info("Co-op: " + client.name + " wants to join as " + (choice ? choice->tag() : client.approvalName) +
                             "; waiting for the host");
                    }
                    waitForHost = client.approval == HostClient::Approval::Pending;
                    declined = client.approval == HostClient::Approval::Declined;
                }
            }
            if (declined) {
                client.slot = SlotAssign();
                client.slot.status = SlotStatus::Declined;
                send(client.connection, encode(client.slot));
                hostReject(client, RejectReason::Declined, "the host declined the request");
                continue;
            }
            if (waitForHost) {
                if (client.slot.status != SlotStatus::Released) {
                    client.slot.status = SlotStatus::Waiting;
                }
            } else if (choice) {
                {
                    client.slot.status = SlotStatus::Assigned;
                    client.slot.npc = choiceNpc;
                    client.slot.creatureId = choice->id();
                    client.slot.tag = choice->tag();
                    taken.insert(choice->id());
                    hostMarkApproved(client);
                    if (client.trusted) {
                        _reservations.erase(client.name);
                    }
                    hostTakeControl(*choice);
                    info("Co-op: client " + std::to_string(client.clientId) + " (" + client.name + ") controls " +
                         choice->tag() + " (id " + std::to_string(choice->id()) + ")");
                }
            } else if (client.slot.status != SlotStatus::Released) {
                client.slot.status = SlotStatus::Waiting;
            }
        }
        bool changed = previous.status != client.slot.status || previous.creatureId != client.slot.creatureId;
        if (changed || !client.slotAnswered) {
            client.slotAnswered = true;
            hostSendSlot(client, false);
            if (changed && client.viewing) {
                hostSendContainer(client, false);
            }
        }
    }
}

bool CoopSession::answerJoin(uint32_t clientId, bool accept) {
    for (auto &[connection, client] : _clients) {
        if (client.clientId != clientId || client.approval != HostClient::Approval::Pending) {
            continue;
        }
        client.approval = accept ? HostClient::Approval::Accepted : HostClient::Approval::Declined;
        if (accept) {
            hostMarkApproved(client);
        }
        info("Co-op: the host " + std::string(accept ? "accepted " : "declined ") + client.name);
        if (_joinPromptClient == clientId) {
            _joinPromptClient = 0;
        }
        hostUpdateSlots();
        return true;
    }
    return false;
}

void CoopSession::hostPromptJoin() {
    // One prompt at a time, in the game, when no other prompt is up.
    if (_joinPromptClient || !showsPrompts() || _game.currentScreen() != Game::Screen::InGame) {
        return;
    }
    for (auto &[connection, client] : _clients) {
        if (client.approval != HostClient::Approval::Pending) {
            continue;
        }
        if (!_game._confirmPopup) {
            _game._confirmPopup = _game.tryLoadGUI<ConfirmPopup>();
        }
        if (!_game._confirmPopup || _game._confirmPopup->isVisible()) {
            return;
        }
        uint32_t id = client.clientId;
        _joinPromptClient = id;
        auto companion = client.approvalName.empty() ? client.approvalTag : client.approvalName;
        _game._confirmPopup->showChoices(
            client.name + " wants to join as " + companion + ".",
            {{"Accept", [this, id]() { answerJoin(id, true); }},
             {"Decline", [this, id]() { answerJoin(id, false); }}});
        return;
    }
}

void CoopSession::hostPartyDefeated() {
    _hostDefeated = true;
    info("Co-op: the party is defeated; the host is choosing");
    for (auto &[connection, client] : _clients) {
        if (client.welcomed) {
            hostNotice(client, NoticeKind::Defeat, "Party defeated: the host is choosing...");
        }
    }
    _game.showDeathScreen();
}

void CoopSession::hostUpdateServiceLock() {
    // Design, "Code rules": the code locks while the session is full, so the
    // service turns joiners away before they reach the host.
    int welcomed = static_cast<int>(std::count_if(_clients.begin(), _clients.end(), [](const auto &entry) {
        return entry.second.welcomed;
    }));
    _transport->setJoinLocked(welcomed >= _config.maxClients);
}

std::vector<CoopSession::PlayerSummary> CoopSession::players() const {
    std::vector<PlayerSummary> result;
    for (const auto &[connection, client] : _clients) {
        if (!client.welcomed) {
            continue;
        }
        PlayerSummary player;
        player.clientId = client.clientId;
        player.name = client.name;
        player.companion = client.slot.status == SlotStatus::Assigned ? client.slot.tag : std::string();
        player.waiting = client.approval == HostClient::Approval::Pending;
        result.push_back(std::move(player));
    }
    return result;
}

OnlineStatus CoopSession::onlineStatus() const {
    return _transport ? _transport->onlineStatus() : OnlineStatus();
}

void CoopSession::reissueCode() {
    if (isHost() && _transport) {
        _transport->reissueCode();
    }
}

CoopSession::JoinProgress CoopSession::joinProgress() const {
    if (_reconnecting) {
        return JoinProgress::Reconnecting;
    }
    switch (_clientState) {
    case ClientState::Connecting:
    case ClientState::Handshaking:
        return JoinProgress::Connecting;
    case ClientState::Joined:
        return _slot.status == SlotStatus::Assigned || _moduleLoaded ? JoinProgress::Joined : JoinProgress::Waiting;
    case ClientState::Reconnecting:
        return JoinProgress::Reconnecting;
    default:
        return JoinProgress::Failed;
    }
}

std::string CoopSession::failureMessage() const {
    if (_clientState == ClientState::Rejected) {
        switch (_reject.reason) {
        case RejectReason::ProtocolMismatch:
        case RejectReason::BuildMismatch:
            return "Version mismatch: you and the host need the same build of the game.";
        case RejectReason::FilesMismatchK1:
        case RejectReason::FilesMismatchK2: {
            // The detail names what differs: chitin.key, dialog.tlk or the modules folder.
            std::string game = _reject.reason == RejectReason::FilesMismatchK1 ? "Knights of the Old Republic" : "Knights of the Old Republic II";
            std::string detail = _reject.detail;
            auto colon = detail.find(": ");
            std::string files = colon == std::string::npos ? detail : detail.substr(colon + 2);
            return "Your " + game + " files differ from the host's (" + files + "). Both players need the same version.";
        }
        case RejectReason::ModeMismatch:
            return "The host is playing a different co-op mode.";
        case RejectReason::SessionFull:
            return "The host's game is full.";
        case RejectReason::Declined:
            return "The host declined your request to join.";
        case RejectReason::Kicked:
            return "The host removed you from the game.";
        default:
            return "The host refused the connection (" + std::string(rejectReasonName(_reject.reason)) + ").";
        }
    }
    const auto &reason = _disconnectReason;
    auto starts = [&](const char *prefix) { return reason.rfind(prefix, 0) == 0; };
    if (starts("service:bad_code")) {
        return "No game has that code. Check the code and try again.";
    }
    if (starts("service:host_full")) {
        return "The host's game is full.";
    }
    if (starts("service:host_left")) {
        return "The host closed the game.";
    }
    if (starts("service:rate_limited")) {
        return "Too many attempts. Wait a minute and try again.";
    }
    if (starts("service:version")) {
        return "Version mismatch with the co-op service: update the game.";
    }
    if (starts("service:unreachable")) {
        return "Cannot reach the co-op service. Check your connection.";
    }
    if (starts("service:server_full")) {
        return "The co-op service is busy. Try again later.";
    }
    if (starts("service:no_route")) {
        return "Could not connect to the host (no network path, even through the relay).";
    }
    if (_hostEnded) {
        return "The host ended the game.";
    }
    if (_everJoined) {
        return "Lost the connection to the host.";
    }
    if (reason.find("imed out") != std::string::npos || reason.find("imeout") != std::string::npos) {
        return "Connecting timed out: the host did not answer.";
    }
    return reason.empty() ? "Could not connect to the host." : "Could not connect to the host (" + reason + ").";
}

void CoopSession::beforeTick() {
    if (!isHost()) {
        return;
    }
    uint64_t tick = _game.tickCount();
    for (auto &[connection, client] : _clients) {
        while (!client.pending.empty()) {
            Command command = client.pending.front();
            client.pending.pop_front();
            CommandReject reason;
            CoopCommandTargets targets;
            bool session = false;
            if (!client.limiter.allow(tick, _game.tickRate())) {
                reason = CommandReject::RateLimited;
            } else {
                reason = hostSessionCommand(client, command, session);
                if (!session) {
                    reason = hostStoreCommand(client, command, session);
                }
                if (!session) {
                    reason = hostBenchCommand(client, command, session);
                }
                if (!session) {
                    reason = hostMiniGameCommand(client, command, session);
                }
                if (!session) {
                    reason = hostOfferCommand(client, command, session);
                }
                if (!session) {
                    reason = hostCompanionCommand(client, command, session);
                }
                if (!session) {
                    uint32_t controlled = hostControlledId(client);
                    bool own = controlled != 0 && (controlled == client.standIn || controlled == client.guestId);
                    reason = validateCoopCommand(_game, command, controlled, targets, own, freeMode());
                    if (reason == CommandReject::HostLeadsConversation && freeMode() && controlled == client.guestId &&
                        targets.target && targets.target->type() == ObjectType::Placeable) {
                        // Free Mode terminals: info-only ones are open to guests;
                        // story ones only through the host's hand-over.
                        reason = hostGuestTerminal(client, targets.target);
                        if (reason == CommandReject::None) {
                            session = true;
                        }
                    }
                    if (reason == CommandReject::StoreUnavailable && freeMode() && controlled == client.guestId &&
                        hostOfferGuest(client, targets.target)) {
                        // Free Mode: a guest gets a prompt ("Browse store", "Play
                        // Pazaak") instead of the conversation.
                        reason = CommandReject::None;
                        session = true;
                    }
                    if (reason == CommandReject::StoreUnavailable && !freeMode() && !_config.hostOnlyTrading &&
                        targets.target && hostRefusesTrade(client, *targets.target)) {
                        // Story Mode: the merchant's conversation would not
                        // open the store now; the refusal notice went out.
                        reason = CommandReject::None;
                        session = true;
                    }
                    if (reason == CommandReject::StoreUnavailable && !freeMode() && !_config.hostOnlyTrading &&
                        hostOpenGuestStore(client, targets.target)) {
                        // Story Mode: any player may open a store directly
                        // (the "host-only trading" option, off by default,
                        // restricts it to the host).
                        reason = CommandReject::None;
                        session = true;
                    }
                    if (reason == CommandReject::None && targets.use == CoopUse::Transition && _config.hostOnlyTransitions) {
                        reason = CommandReject::HostOnlyTransitions;
                    }
                    // The terminal design: a console that changes story state
                    // is the host's until the host hands it over; one that
                    // only shows information is open to anyone.
                    if (reason == CommandReject::None && targets.use == CoopUse::Placeable && targets.story && targets.target &&
                        !client.handedOver.count(targets.target->id())) {
                        reason = CommandReject::NotHandedOver;
                        hostPromptHandOver(client, *targets.target);
                    }
                }
            }
            if (reason == CommandReject::None) {
                if (!session) {
                    if (targets.use == CoopUse::Normal) {
                        executeCoopCommand(_game, command, targets);
                    } else {
                        hostCarryOutUse(client, command, targets);
                    }
                }
                ++client.commandsAccepted;
            } else {
                ++client.commandsRejected;
                ++client.rejectCounts[static_cast<uint8_t>(reason)];
            }
            info(std::string("Co-op: client ") + std::to_string(client.clientId) + " command " + std::to_string(command.seq) +
                 " " + commandTypeName(command.type) + " target " + std::to_string(command.target) + " -> " +
                 (reason == CommandReject::None ? "accepted" : commandRejectName(reason)) + " before tick " +
                 std::to_string(tick + 1));
            CommandResult result {command.seq, reason == CommandReject::None, reason, static_cast<uint32_t>(tick + 1)};
            send(client.connection, encode(result));
        }
    }
}

std::map<uint32_t, EntityState> CoopSession::hostCaptureEntities() const {
    std::map<uint32_t, EntityState> result;
    auto module = _game.module();
    if (!module || !module->area()) {
        return result;
    }
    auto player = _game.party().player();
    for (const auto &object : module->area()->objects()) {
        if (!object || !object->isRuntimeLive() || !CoopReplication::isReplicated(*object)) {
            continue;
        }
        std::string key;
        auto found = _hostKeys.find(object->id());
        if (found != _hostKeys.end()) {
            key = found->second;
        } else if (object == player) {
            key = kPlayerLoadKey;
        }
        auto state = CoopReplication::capture(*object, key);
        if (auto source = _standInSource.find(object->id()); source != _standInSource.end()) {
            // A stand-in has no template: the client builds the same look.
            auto &creature = static_cast<const Creature &>(*object);
            state.resref = coopStandInResRef(creature.appearance(), static_cast<int>(creature.gender()), source->second);
        }
        if (object->type() == ObjectType::Creature) {
            if (_game.party().isMember(*object)) {
                state.flags |= kFlagPartyMember;
            }
            if (object == _game.party().getLeader() || isPlayerControlled(object->id())) {
                state.flags |= kFlagPlayer;
            }
        }
        result.emplace(object->id(), std::move(state));
    }
    return result;
}

std::vector<CoopSession::MapMarker> CoopSession::mapMarkers() const {
    std::vector<MapMarker> markers;
    if (isHost()) {
        for (const auto &[connection, client] : _clients) {
            if (!client.welcomed || client.slot.status != SlotStatus::Assigned) {
                continue;
            }
            if (auto object = _game.getObjectById(client.slot.creatureId)) {
                markers.push_back({std::move(object), true});
            }
        }
        return markers;
    }
    uint32_t controlled = controlledHostId();
    for (const auto &[hostId, replica] : _replicas) {
        if (hostId == controlled || replica.localId == 0 ||
            (replica.state.flags & (kFlagPartyMember | kFlagPlayer)) == 0 || replica.state.dead) {
            continue;
        }
        if (auto object = _game.getObjectById(replica.localId)) {
            markers.push_back({std::move(object), (replica.state.flags & kFlagPlayer) != 0});
        }
    }
    return markers;
}

WorldState CoopSession::hostCaptureWorld() const {
    WorldState world;
    world.screen = static_cast<uint8_t>(_game._screen);
    world.fadeOpacity = std::clamp(_game._globalFade.opacity(), 0.0f, 1.0f);
    const auto &color = _game._globalFade.color();
    world.fadeColor = {color.x, color.y, color.z};
    world.movie = static_cast<bool>(_game._movie);
    if (world.movie) {
        world.movieName = _hostMovieName;
    }
    world.cameraType = static_cast<uint8_t>(_game._cameraType);
    if (_game._screen == Game::Screen::Conversation && _game._conversation) {
        auto state = _game._conversation->inspectionState();
        if (state.dialog) {
            auto &conversation = world.conversation;
            conversation.active = true;
            conversation.kind = _game._conversation == static_cast<Conversation *>(_game._dialog.get()) ? 0 : 1;
            conversation.dialog = state.dialog->resRef.substr(0, kMaxStringBytes);
            const auto &entries = state.dialog->entries;
            if (state.entry && !entries.empty() && state.entry >= entries.data() && state.entry < entries.data() + entries.size()) {
                conversation.entry = static_cast<int32_t>(state.entry - entries.data());
                conversation.speaker = state.entry->speaker.substr(0, kMaxStringBytes);
            }
            conversation.owner = state.owner ? state.owner->id() : 0;
            const auto &replies = state.dialog->replies;
            for (auto *reply : state.replies) {
                if (conversation.replies.size() >= kMaxReplies) {
                    break;
                }
                if (!replies.empty() && reply >= replies.data() && reply < replies.data() + replies.size()) {
                    conversation.replies.push_back(static_cast<int32_t>(reply - replies.data()));
                }
            }
            conversation.entryEnded = state.entryEnded;
        }
    }
    for (const auto &member : _game.party().members()) {
        if (member.creature && world.party.size() < kMaxPartyEntries) {
            world.party.push_back({static_cast<int8_t>(member.npc), member.creature->id()});
        }
    }
    if (auto leader = _game.party().getLeader()) {
        world.leader = leader->id();
    }
    world.paused = _game.isPaused();
    if (world.paused) {
        world.pausedBy = _pausedBy.substr(0, kMaxStringBytes);
    }
    return world;
}

void CoopSession::hostCollectVfxEnds() {
    for (auto it = _hostVfx.begin(); it != _hostVfx.end();) {
        // Ended by the simulation, or finished and dropped by the presenter.
        if (!it->request || it->request->ended || it->request.use_count() == 1) {
            NetEvent event;
            event.kind = net::EventKind::VfxEnd;
            event.handle = it->handle;
            _pendingEvents.push_back(event);
            it = _hostVfx.erase(it);
        } else {
            ++it;
        }
    }
}

void CoopSession::onVisualEffectBegin(const std::shared_ptr<VisualEffectPresenter::Request> &request) {
    if (!isHost() || !request || !request->desc || _pendingEvents.size() >= kMaxEvents / 2) {
        return;
    }
    NetEvent event;
    event.kind = net::EventKind::VfxBegin;
    event.handle = _nextVfxHandle++;
    event.row = request->desc->id;
    event.a = request->targetId;
    event.location = {request->location.x, request->location.y, request->location.z};
    event.facing = request->facing;
    // A beam's effector, so a guest can draw it (design note v14_programmed_effects, section 6).
    event.b = request->sourceId;
    event.flags = static_cast<uint8_t>(std::clamp(request->sourceBodyNode, 0, static_cast<int>(net::kMaxVfxBodyNode)));
    _pendingEvents.push_back(event);
    _hostVfx.push_back({event.handle, request});
}

void CoopSession::onProjectileFired(const Creature &attacker, const Object &target, bool offhand, bool miss) {
    if (!isHost() || _pendingEvents.size() >= kMaxEvents / 2) {
        return;
    }
    NetEvent event;
    event.kind = net::EventKind::Projectile;
    event.a = attacker.id();
    event.b = target.id();
    event.flags = static_cast<uint8_t>((offhand ? 1 : 0) | (miss ? 2 : 0));
    _pendingEvents.push_back(event);
}

void CoopSession::afterTick() {
    if (!isHost()) {
        return;
    }
    hostDetectModuleChange();
    hostUpdateSlots();
    if (freeMode()) {
        hostUpdateGuests();
        hostSendRoster();
        hostUpdateTerminals();
    } else {
        hostUpdateStandIns();
    }
    hostUpdateServiceLock();
    if (_miniGame.running() && _game.tickCount() % kMiniGameStateEveryTicks == 0) {
        hostBroadcastMiniGame();
    }
    hostCollectVfxEnds();
    hostUpdateContainers();
    hostCheckClientExits();
    hostUpdateReadyCheck();
    hostUpdatePause();
    hostUpdateScaling();
    hostSendInventories();
    hostSendSheets();
    bool anyReady = hasHostModule() && std::any_of(_clients.begin(), _clients.end(), [this](const auto &entry) {
        return entry.second.welcomed && entry.second.moduleGeneration == _generation;
    });
    if (!anyReady) {
        _pendingEvents.clear();
        return;
    }
    auto entities = hostCaptureEntities();
    auto world = hostCaptureWorld();
    EventBatch batch;
    batch.generation = _generation;
    batch.tick = static_cast<uint32_t>(_game.tickCount());
    batch.events = std::move(_pendingEvents);
    _pendingEvents.clear();
    std::vector<uint8_t> eventBytes;
    if (!batch.events.empty()) {
        if (batch.events.size() > kMaxEvents) {
            batch.events.resize(kMaxEvents);
        }
        eventBytes = encode(batch);
    }
    for (auto &[connection, client] : _clients) {
        if (client.welcomed && client.moduleGeneration == _generation) {
            hostSendSnapshot(client, entities, world);
            if (!eventBytes.empty()) {
                // Events are one-shot (a VFX starting, a bolt flying): they
                // go reliably, and the client plays them at their tick.
                send(connection, eventBytes);
            }
        }
    }
}

namespace {

/** Snapshots a client may leave unacknowledged before the host starts over with a full one. */
constexpr size_t kMaxHistory = 90;
/** Without a newer tick, the latest snapshot goes again this often until acknowledged. */
constexpr auto kResendInterval = std::chrono::milliseconds(120);

} // namespace

void CoopSession::hostSendSnapshot(HostClient &client, const std::map<uint32_t, EntityState> &entities,
                                   const WorldState &world) {
    uint32_t tick = static_cast<uint32_t>(_game.tickCount());
    if (tick == 0 || (!client.history.empty() && client.history.back().tick >= tick)) {
        return; // this tick went out already
    }
    const HostClient::Sent *latest = client.history.empty() ? nullptr : &client.history.back();

    // What the client will hold for this tick: the latest state sent, with
    // every field that changed past the replication tolerances replaced.
    // Fields within tolerance keep their last sent value, so the delta
    // against any base the client has is exact.
    HostClient::Sent next;
    next.tick = tick;
    if (latest) {
        next.world = latest->world;
        uint16_t fields = 0;
        const auto &base = latest->world;
        if (base.screen != world.screen) {
            fields |= kWorldScreen;
        }
        if (!sameWorldFade(base, world)) {
            fields |= kWorldFade;
        }
        if (base.movie != world.movie || base.movieName != world.movieName) {
            fields |= kWorldMovie;
        }
        if (base.cameraType != world.cameraType || base.cameraId != world.cameraId) {
            fields |= kWorldCamera;
        }
        if (base.conversation != world.conversation) {
            fields |= kWorldConversation;
        }
        if (!(base.party == world.party) || base.leader != world.leader) {
            fields |= kWorldParty;
        }
        if (base.paused != world.paused || base.pausedBy != world.pausedBy) {
            fields |= kWorldPause;
        }
        mergeWorld(next.world, world, fields);
    } else {
        next.world = world;
    }
    float dt = _game.tickSeconds();
    for (const auto &[id, current] : entities) {
        const EntityState *previous = nullptr;
        if (latest) {
            auto it = latest->entities.find(id);
            previous = it != latest->entities.end() ? &it->second : nullptr;
        }
        if (!previous) {
            EntityState state = current;
            state.animSerial = ++client.animSerials[id];
            state.animSentTick = tick;
            next.entities.emplace(id, std::move(state));
            continue;
        }
        EntityState state = *previous;
        uint16_t mask = diffEntity(state, current);
        // The client advances animation time on its own; resend it only
        // when the host's clip has drifted from where the client will be.
        float expected = state.animTime + static_cast<float>(tick - state.animSentTick) * dt * state.animSpeed;
        if (!(mask & kFieldAnim) && !current.anim.empty() && std::fabs(current.animTime - expected) > kAnimResync) {
            mask |= kFieldAnim;
        }
        if (mask) {
            mergeEntity(state, current, mask);
            if (mask & kFieldAnim) {
                state.animSerial = ++client.animSerials[id];
                state.animSentTick = tick;
            }
        }
        next.entities.emplace(id, std::move(state));
    }
    client.history.push_back(std::move(next));
    if (client.history.size() > kMaxHistory) {
        // Nothing acknowledged for seconds: start over from a full snapshot.
        bool lostBase = client.history.front().tick == client.ackedTick;
        client.history.pop_front();
        if (lostBase) {
            client.ackedTick = 0;
        }
    }

    auto bytes = hostEncodeSnapshot(client, client.history.back());
    if (_transport) {
        _transport->send(client.connection, bytes, SendMode::Unreliable);
    }
    client.lastSnapshotSent = std::chrono::steady_clock::now();
    ++client.snapshotsSent;
    client.snapshotBytes += bytes.size();
    client.maxSnapshotBytes = std::max<uint64_t>(client.maxSnapshotBytes, bytes.size());
    if (client.ackedTick == 0) {
        client.fullSnapshotBytes = bytes.size();
    }
}

std::vector<uint8_t> CoopSession::hostEncodeSnapshot(const HostClient &client, const HostClient::Sent &target) const {
    const HostClient::Sent *base = nullptr;
    if (client.ackedTick) {
        for (const auto &sent : client.history) {
            if (sent.tick == client.ackedTick) {
                base = &sent;
                break;
            }
        }
    }
    Snapshot snapshot;
    snapshot.tick = target.tick;
    snapshot.generation = client.moduleGeneration;
    snapshot.baseTick = base && base->tick < target.tick ? base->tick : 0;
    if (!snapshot.baseTick) {
        base = nullptr;
    }
    snapshot.worldFields = base ? exactDiffWorld(base->world, target.world) : static_cast<uint16_t>(kWorldAllMask);
    snapshot.world = target.world;
    for (const auto &[id, state] : target.entities) {
        const EntityState *was = nullptr;
        if (base) {
            auto it = base->entities.find(id);
            was = it != base->entities.end() ? &it->second : nullptr;
        }
        uint16_t mask = was ? exactDiffEntity(*was, state) : static_cast<uint16_t>(kFieldAllMask & ~kFieldRemoved);
        if (mask) {
            EntityRecord record;
            record.fields = mask;
            record.state = state;
            snapshot.entities.push_back(std::move(record));
        }
    }
    if (base) {
        for (const auto &[id, state] : base->entities) {
            if (!target.entities.count(id)) {
                EntityRecord record;
                record.fields = kFieldRemoved;
                record.state.id = id;
                snapshot.entities.push_back(std::move(record));
            }
        }
    }
    return encode(snapshot);
}

void CoopSession::hostResendSnapshots() {
    // An idle host (paused ticks, a tick-gated harness) sends no new
    // snapshot, so a lost last one would leave the client behind for good:
    // it goes again until the client acknowledges it.
    auto now = std::chrono::steady_clock::now();
    for (auto &[connection, client] : _clients) {
        if (!client.welcomed || client.moduleGeneration != _generation || client.history.empty() ||
            client.history.back().tick <= client.ackedTick || now - client.lastSnapshotSent < kResendInterval) {
            continue;
        }
        auto bytes = hostEncodeSnapshot(client, client.history.back());
        if (_transport) {
            _transport->send(connection, bytes, SendMode::Unreliable);
        }
        client.lastSnapshotSent = now;
        ++client.resends;
    }
}

// Both

void CoopSession::onModuleStructureLoaded() {
    auto module = _game.module();
    if (!module || !module->area()) {
        return;
    }
    std::vector<LoadEntry> entries;
    for (const auto &object : module->area()->objects()) {
        if (!object || !CoopReplication::isReplicated(*object)) {
            continue;
        }
        LoadEntry entry;
        entry.id = object->id();
        entry.type = static_cast<int>(object->type());
        entry.tag = object->tag();
        entry.resref = object->blueprintResRef();
        entry.x = object->position().x;
        entry.y = object->position().y;
        entries.push_back(std::move(entry));
    }
    auto keys = assignLoadKeys(entries);
    if (isHost()) {
        _hostKeys.clear();
        for (auto &[id, key] : keys) {
            _hostKeys[id] = key;
        }
        info("Co-op: host load keys for " + module->name() + ": " + std::to_string(keys.size()) + " objects");
    } else {
        _localKeys.clear();
        _localKeyById.clear();
        for (auto &[id, key] : keys) {
            _localKeys[key] = id;
            _localKeyById[id] = key;
        }
        info("Co-op: client load keys for " + module->name() + ": " + std::to_string(keys.size()) + " objects");
    }
}

// Client

void CoopSession::clientHandleMessage(const std::vector<uint8_t> &data) {
    auto type = peekType(data);
    if (!type || !isHostMessage(*type) || data.size() > kMaxHostMessageBytes) {
        ++_hostMessageViolations;
        warn("Co-op: the host sent an invalid message");
        return;
    }
    bool ok = true;
    switch (*type) {
    case MessageType::Welcome:
        ok = decode(data, _welcome);
        if (ok) {
            _clientState = ClientState::Joined;
            bool back = _reconnecting;
            _reconnecting = false;
            _everJoined = true;
            _rejoinToken = _welcome.rejoinToken;
            saveRejoinToken(_config.rejoinStore, _config.sessionKey, _config.playerName, _rejoinToken, unixNow());
            if (_config.mode.empty()) {
                _config.mode = _welcome.mode; // the host's mode
            }
            info("Co-op: " + std::string(back ? "rejoined" : "joined") + " as client " + std::to_string(_welcome.clientId) +
                 "; asking for a companion");
            SlotRequest request;
            // After a drop, the companion this player had (the host kept it for them).
            request.preferredTag = back && !_slot.tag.empty() ? _slot.tag : _config.preferredTag;
            if (back) {
                // The companion is ours again only when the host says so.
                _slot.status = SlotStatus::Waiting;
                _slot.creatureId = 0;
            }
            send(_hostConnection, encode(request));
            if (freeMode()) {
                clientAfterWelcomeFree();
            }
            if (back) {
                showNotice(NoticeKind::Info, "Reconnected to the host");
            }
        }
        break;
    case MessageType::Reject:
        ok = decode(data, _reject);
        if (ok) {
            _clientState = ClientState::Rejected;
            _rejoinToken = 0;
            forgetRejoinToken(_config.rejoinStore, _config.sessionKey, _config.playerName);
            warn(std::string("Co-op: the host rejected the join: ") + rejectReasonName(_reject.reason) + " " + _reject.detail);
        }
        break;
    case MessageType::SlotAssign: {
        SlotAssign slot;
        ok = decode(data, slot);
        if (ok) {
            _slot = slot;
            info(std::string("Co-op: slot ") + slotStatusName(slot.status) +
                 (slot.status == SlotStatus::Assigned ? " -> " + slot.tag + " (host id " + std::to_string(slot.creatureId) + ")" : ""));
            if (slot.status == SlotStatus::Released && _lastSlotStatus == SlotStatus::Assigned) {
                // Design, "Lost companion": a stand-in until a companion is
                // free, or watching where the section says so.
                showNotice(NoticeKind::Info, "Your companion left the party");
            } else if (slot.status == SlotStatus::Guest && _lastSlotStatus != SlotStatus::Guest) {
                showNotice(NoticeKind::Info, "You play " + _guest.name);
            } else if (slot.status == SlotStatus::StandIn && _lastSlotStatus != SlotStatus::StandIn) {
                showNotice(NoticeKind::Info, "You play a stand-in until a companion is free");
            } else if (slot.status == SlotStatus::Assigned && _lastSlotStatus != SlotStatus::Assigned) {
                showNotice(NoticeKind::Info, "You control " + slot.tag);
            }
            bool sameCreature = slot.creatureId == _lastSlotCreature;
            _lastSlotStatus = slot.status;
            _lastSlotCreature = slot.creatureId;
            if (_clientViewing && (slot.status != SlotStatus::Assigned || !sameCreature)) {
                clientCloseContainerScreen(true);
            }
            clientMirrorParty();
        }
        break;
    }
    case MessageType::ModuleInfo: {
        net::ModuleInfo module;
        ok = decode(data, module);
        if (ok) {
            _module = module;
            if (freeMode() && !_guestSent) {
                // Free Mode: character generation first; the host's module
                // loads once this player's character exists.
                _moduleHeld = true;
            } else if (_miniGameResultHold > 0.0f) {
                // A minigame just ended here: its result stays up a moment first (clientTick).
                _moduleHeldForResult = true;
            } else {
                clientEnterModule();
            }
        }
        break;
    }
    case MessageType::Snapshot: {
        Snapshot snapshot;
        ok = decode(data, snapshot);
        if (ok) {
            clientReceiveSnapshot(snapshot);
        }
        break;
    }
    case MessageType::Events: {
        EventBatch batch;
        ok = decode(data, batch);
        if (ok && batch.generation == _module.generation) {
            if (_applied && batch.tick <= _appliedTick) {
                for (const auto &event : batch.events) {
                    clientApplyEvent(event);
                }
            } else {
                _eventQueue.push_back(std::move(batch));
                while (_eventQueue.size() > 512) {
                    _eventQueue.pop_front();
                }
            }
        }
        break;
    }
    case MessageType::CommandResult: {
        CommandResult result;
        ok = decode(data, result);
        if (ok) {
            _results.push_back(result);
            while (_results.size() > kMaxResults) {
                _results.pop_front();
            }
            if (!result.accepted) {
                warn(std::string("Co-op: command ") + std::to_string(result.seq) + " rejected: " + commandRejectName(result.reason));
                clientShowRejection(result);
            }
        }
        break;
    }
    case MessageType::Bye: {
        Bye bye;
        ok = decode(data, bye);
        if (ok) {
            _disconnectReason = bye.reason;
            _hostEnded = true;
            _rejoinToken = 0;
            forgetRejoinToken(_config.rejoinStore, _config.sessionKey, _config.playerName);
            info("Co-op: the host ended the session: " + bye.reason);
        }
        break;
    }
    case MessageType::Notice: {
        Notice notice;
        ok = decode(data, notice);
        if (ok) {
            clientHandleNotice(notice);
        }
        break;
    }
    case MessageType::ContainerView: {
        ContainerView view;
        ok = decode(data, view);
        if (ok) {
            clientHandleContainer(view);
        }
        break;
    }
    case MessageType::SharedInventory: {
        SharedInventory inventory;
        ok = decode(data, inventory);
        if (ok) {
            clientHandleInventory(inventory);
        }
        break;
    }
    case MessageType::CharacterSheet: {
        CharacterSheet sheet;
        ok = decode(data, sheet);
        if (ok) {
            clientHandleSheet(sheet);
        }
        break;
    }
    case MessageType::Bark: {
        Bark bark;
        ok = decode(data, bark);
        if (ok) {
            clientHandleBark(bark);
        }
        break;
    }
    case MessageType::GuestCharacter: {
        GuestCharacter guest;
        ok = decode(data, guest);
        if (ok) {
            clientHandleGuest(guest);
        }
        break;
    }
    case MessageType::StoreView: {
        StoreView view;
        ok = decode(data, view);
        if (ok) {
            clientHandleStore(view);
        }
        break;
    }
    case MessageType::BenchView: {
        BenchView view;
        ok = decode(data, view);
        if (ok) {
            clientHandleBench(view);
        }
        break;
    }
    case MessageType::MiniGameState: {
        MiniGameState state;
        ok = decode(data, state);
        if (ok) {
            clientHandleMiniGameState(state);
        }
        break;
    }
    case MessageType::GuestRoster: {
        GuestRoster roster;
        ok = decode(data, roster);
        if (ok) {
            _roster = std::move(roster.entries);
        }
        break;
    }
    case MessageType::Offer: {
        Offer offer;
        ok = decode(data, offer);
        if (ok) {
            clientHandleOffer(offer);
        }
        break;
    }
    case MessageType::ReadyCheck: {
        ReadyCheck check;
        ok = decode(data, check);
        if (ok) {
            clientHandleReady(check);
        }
        break;
    }
    default:
        ok = false;
        break;
    }
    if (!ok) {
        ++_hostMessageViolations;
        warn(std::string("Co-op: malformed ") + messageTypeName(*type) + " from the host");
    }
}

void CoopSession::clientEnterModule() {
    if (_defeatShown) {
        _defeatShown = false;
        if (_game._confirmPopup && _game._confirmPopup->isVisible()) {
            _game._confirmPopup->hide();
        }
        showNotice(NoticeKind::Info, "The host loaded: back in the game");
    }
    // The host's snapshot history starts over with every ModuleInfo.
    _recon.clear();
    _buffer.clear();
    // Events of this visit may have arrived already (the load waited on a minigame's result).
    _eventQueue.erase(std::remove_if(_eventQueue.begin(), _eventQueue.end(),
                                     [this](const EventBatch &batch) { return batch.generation != _module.generation; }),
                      _eventQueue.end());
    if (_moduleLoaded && _loadedGeneration == _module.generation && _game.module() && _game.module()->name() == _module.module) {
        // Back after a drop, in the same module visit: keep the world. The
        // host's first snapshot is full and is applied as a delta against
        // what the world shows now, so nothing jumps.
        info("Co-op: back in " + _module.module + " (generation " + std::to_string(_module.generation) + "); resyncing");
        return;
    }
    _applied.reset();
    info("Co-op: the host is in " + _module.module + " (generation " + std::to_string(_module.generation) +
         "); loading it from the local install");
    // Party creatures travel with the party into the new module: remember
    // which host ids they stand for, so they are rebound, not spawned again.
    _carried.clear();
    for (const auto &member : _game.party().members()) {
        if (member.creature && member.npc != kNpcPlayer) {
            if (auto host = _ids.host(member.creature->id())) {
                _carried[*host] = member.creature->id();
            }
        }
    }
    _ids.clear();
    _replicas.clear();
    _clientViewing = 0;
    _clientItems.clear();
    if (_game._conversation && _game._conversation->isReplica()) {
        _game._conversation->endReplica();
    }
    _replicaDialog.clear();
    for (auto &[handle, request] : _clientVfx) {
        request->ended = true;
    }
    _clientVfx.clear();
    for (auto &projectile : _projectiles) {
        projectile.projectile->reset();
    }
    _projectiles.clear();
    _moduleLoaded = false;
    _idsEqual = 0;
    _idsBoundByKey = 0;
    _unmatchedSpawns = 0;
    _spawnFailures = 0;

    if (!_game.party().player()) {
        // First entry: build the host's player character from its look, as
        // character generation would. It stands in for the host; its state
        // comes from the snapshots.
        _game.retireRuntimeSession();
        std::vector<std::shared_ptr<Object>> noObsolete;
        std::shared_ptr<Creature> player;
        _game.replaceRuntimeObjectGraph(
            noObsolete,
            [&]() {
                player = _game.newCreature();
                player->setTag(kObjectTagPlayer);
                player->setName(_module.player.name);
                player->setGender(static_cast<Gender>(_module.player.gender));
                player->setAppearance(_module.player.appearance);
                player->loadAppearance();
                player->setFaction(Faction::Player);
                player->setIsPC(true);
            },
            []() noexcept {});
        auto &party = _game.party();
        party.reset();
        party.initializeNewGameState();
        party.addMember(kNpcPlayer, player);
        party.setPlayer(player);
        party.setActualPlayer(player);
    }
    _moduleLoaded = _game.loadModule(_module.module);
    _loadedGeneration = _module.generation;
    if (!_moduleLoaded) {
        warn("Co-op: could not load " + _module.module + " from the local install");
    }
}

namespace {

/** Decoded snapshots the client keeps as possible delta bases. */
constexpr size_t kMaxRecon = 96;
/** Jitter buffer depth, in host ticks: at least one lost snapshot's worth. */
constexpr int kMinDelayTicks = 2;
constexpr int kMaxDelayTicks = 8;
/** Further behind than this the playhead jumps instead of catching up. */
constexpr int kSnapTicks = 30;

} // namespace

void CoopSession::clientReceiveSnapshot(const Snapshot &snapshot) {
    if (snapshot.generation != _module.generation || !_moduleLoaded || _loadedGeneration != _module.generation) {
        ++_snapshotsDropped; // another module visit, or ours failed to load
        return;
    }
    if (_recon.count(snapshot.tick)) {
        // A resend of one we hold: the ack was lost, so repeat it.
        SnapshotAck ack {snapshot.generation, snapshot.tick};
        _transport->send(_hostConnection, encode(ack), SendMode::Unreliable);
        ++_acksSent;
        return;
    }
    auto recon = std::make_shared<Recon>();
    if (!snapshot.full()) {
        auto base = _recon.find(snapshot.baseTick);
        if (base == _recon.end()) {
            // Its base is gone (we dropped it, or its snapshot never came):
            // the host sends against our newest ack soon.
            ++_baseMissing;
            ++_snapshotsDropped;
            return;
        }
        *recon = *base->second;
    }
    recon->tick = snapshot.tick;
    for (const auto &record : snapshot.entities) {
        uint32_t id = record.state.id;
        if (record.fields & kFieldRemoved) {
            recon->entities.erase(id);
            continue;
        }
        auto it = recon->entities.find(id);
        if (it == recon->entities.end()) {
            if (!(record.fields & kFieldIdentity) || recon->entities.size() >= kMaxEntities) {
                ++_hostMessageViolations; // a delta for an object the base does not have
                ++_snapshotsDropped;
                return;
            }
            EntityState state;
            mergeEntity(state, record.state, record.fields);
            recon->entities.emplace(id, std::move(state));
        } else {
            mergeEntity(it->second, record.state, record.fields);
        }
    }
    mergeWorld(recon->world, snapshot.world, snapshot.worldFields);
    _recon.emplace(snapshot.tick, recon);
    while (_recon.size() > kMaxRecon) {
        _recon.erase(_recon.begin());
    }
    SnapshotAck ack {snapshot.generation, snapshot.tick};
    _transport->send(_hostConnection, encode(ack), SendMode::Unreliable);
    ++_acksSent;

    // Jitter: how unevenly snapshots arrive against the ticks they carry.
    if (_lastArrivalTick && snapshot.tick > _lastArrivalTick) {
        float arrival = static_cast<float>(_clientTicks - _lastArrivalClientTick);
        float spacing = static_cast<float>(snapshot.tick - _lastArrivalTick);
        _jitter += (std::fabs(arrival - spacing) - _jitter) * 0.05f;
        _delayTicks = std::clamp(kMinDelayTicks + static_cast<int>(std::ceil(_jitter * 2.0f)), kMinDelayTicks, kMaxDelayTicks);
    }
    if (snapshot.tick > _lastArrivalTick) {
        _lastArrivalTick = snapshot.tick;
        _lastArrivalClientTick = _clientTicks;
    }
    if (!_applied || snapshot.tick > _appliedTick) {
        _buffer.emplace(snapshot.tick, recon);
    }
}

void CoopSession::clientTick(float dt) {
    if (_miniGameResultHold > 0.0f) {
        _miniGameResultHold -= std::max(0.0f, dt);
        if (_miniGameResultHold <= 0.0f && _moduleHeldForResult) {
            _moduleHeldForResult = false;
            clientEnterModule();
        }
    }
    clientSteer(dt);
    if (!isClient()) {
        return;
    }
    ++_clientTicks;
    // Interpolation runs from where each object stood at the start of this
    // tick to where this tick's state puts it.
    for (auto &[id, replica] : _replicas) {
        replica.previousPosition = replica.state.position;
        replica.previousFacing = replica.state.facing;
        replica.interpolate = false;
    }
    if (_moduleLoaded && !_buffer.empty()) {
        // The jitter buffer: play a few ticks behind the newest state, so a
        // late or lost snapshot does not stall the picture. The playhead
        // speeds up when it falls behind and slows down when it runs short,
        // and never passes the newest state.
        uint32_t newest = _buffer.rbegin()->first;
        if (!_applied) {
            _playTick = newest; // the first state of a visit shows at once
        } else {
            int gap = static_cast<int>(newest) - static_cast<int>(_playTick);
            int advance = 1;
            if (gap > _delayTicks + kSnapTicks) {
                _playTick = newest - static_cast<uint32_t>(_delayTicks);
                advance = 0;
            } else if (gap > _delayTicks + 2) {
                advance = 2;
            } else if (gap < _delayTicks - 1 && (_clientTicks & 1)) {
                advance = 0;
            }
            _playTick = std::min(_playTick + static_cast<uint32_t>(advance), newest);
        }
        auto due = _buffer.upper_bound(_playTick);
        if (due != _buffer.begin()) {
            --due;
            auto recon = due->second;
            _buffer.erase(_buffer.begin(), std::next(due));
            clientApplyRecon(recon);
        }
    }
    if (_moduleLoaded) {
        if (auto module = _game.module(); module && module->area()) {
            // What Area::update does for a viewer: removals, room and object
            // visibility, and what the pointer can select. Nothing else.
            auto &area = *module->area();
            flushDestroyedObjects(area);
            area.updateRoomVisibility();
            area.updateVisibility();
            area.updateObjectSelection();
        }
    }
    for (auto &[id, replica] : _replicas) {
        replica.interpolate = replica.state.type == static_cast<uint8_t>(ObjectType::Creature) &&
                              (replica.previousPosition != replica.state.position || replica.previousFacing != replica.state.facing);
    }
    clientUpdateProjectiles(dt);
    clientPromptReady();
}

void CoopSession::clientApplyRecon(const std::shared_ptr<const Recon> &recon) {
    bool first = !_applied;
    // Apply what differs from what the world shows: a snapshot skipped by
    // the jitter buffer or lost on the way costs nothing but smoothness.
    for (const auto &[id, state] : recon->entities) {
        const EntityState *was = nullptr;
        if (_applied) {
            auto it = _applied->entities.find(id);
            was = it != _applied->entities.end() ? &it->second : nullptr;
        }
        uint16_t mask = was ? exactDiffEntity(*was, state) : static_cast<uint16_t>(kFieldAllMask & ~kFieldRemoved);
        if (mask) {
            EntityRecord record;
            record.fields = mask;
            record.state = state;
            record.state.id = id;
            clientApplyEntity(record);
        }
    }
    if (_applied) {
        for (const auto &[id, state] : _applied->entities) {
            if (!recon->entities.count(id)) {
                EntityRecord record;
                record.fields = kFieldRemoved;
                record.state.id = id;
                clientApplyEntity(record);
            }
        }
    }
    uint16_t worldFields = first ? static_cast<uint16_t>(kWorldAllMask) : exactDiffWorld(_applied->world, recon->world);
    if (worldFields) {
        clientApplyWorld(worldFields, recon->world);
    }
    _applied = recon;
    _appliedTick = recon->tick;
    ++_snapshotsApplied;
    clientApplyDueEvents();
    if (first) {
        clientRetireUnbound();
        clientMirrorParty();
        info("Co-op: first state at host tick " + std::to_string(recon->tick) + ": " + std::to_string(_replicas.size()) +
             " objects, " + std::to_string(_idsBoundByKey) + " bound by load key (" + std::to_string(_idsEqual) +
             " with the same id), " + std::to_string(_unmatchedSpawns) + " spawned, " + std::to_string(_spawnFailures) + " unmatched");
    }
}

void CoopSession::clientApplyDueEvents() {
    while (!_eventQueue.empty() && _eventQueue.front().tick <= _appliedTick) {
        auto batch = std::move(_eventQueue.front());
        _eventQueue.pop_front();
        for (const auto &event : batch.events) {
            clientApplyEvent(event);
        }
    }
}

std::shared_ptr<Object> CoopSession::clientBind(Replica &replica) {
    const auto &s = replica.state;
    auto module = _game.module();
    if (!module || !module->area()) {
        return nullptr;
    }
    std::shared_ptr<Object> object;
    bool byKey = false;
    if (auto carried = _carried.find(s.id); carried != _carried.end()) {
        // A party member that came along from the previous module.
        auto local = _game.getObjectById(carried->second);
        _carried.erase(carried);
        if (local && local->isRuntimeLive()) {
            object = local;
        }
    }
    if (object) {
        // bound to the carried party creature above
    } else if (s.key == kPlayerLoadKey) {
        object = _game.party().player();
    } else if (!s.key.empty()) {
        auto it = _localKeys.find(s.key);
        if (it != _localKeys.end()) {
            object = _game.getObjectById(it->second);
            byKey = object != nullptr;
        }
    }
    auto type = static_cast<ObjectType>(s.type);
    bool standIn = type == ObjectType::Creature && s.resref.rfind(kStandInPrefix, 0) == 0;
    if (!object && standIn) {
        // A player's stand-in (design item 12): the look the host announced.
        object = clientSpawnStandIn(replica);
        if (object) {
            ++_standInSpawns;
        }
    } else if (!object && type == ObjectType::Placeable && s.resref.rfind(kAppearancePrefix, 0) == 0) {
        // A placeable the host built in code (a body bag): build the same shell.
        int appearance = 0;
        try {
            appearance = std::stoi(s.resref.substr(std::strlen(kAppearancePrefix)));
        } catch (const std::exception &) {
            appearance = -1;
        }
        if (appearance >= 0 && appearance < 100000) {
            auto gff = resource::Gff::Builder()
                           .field(resource::Gff::Field::newCExoString("Tag", s.tag))
                           .field(resource::Gff::Field::newDword("Appearance", static_cast<uint32_t>(appearance)))
                           .field(resource::Gff::Field::newByte("Useable", 1))
                           .field(resource::Gff::Field::newByte("HasInventory", 1))
                           .field(resource::Gff::Field::newFloat("Bearing", s.facing))
                           .field(resource::Gff::Field::newFloat("X", s.position.x))
                           .field(resource::Gff::Field::newFloat("Y", s.position.y))
                           .field(resource::Gff::Field::newFloat("Z", s.position.z))
                           .build();
            if (auto placeable = _game.newPlaceableFromGff(*gff, "coopshell")) {
                module->area()->add(placeable);
                object = placeable;
                replica.spawned = true;
                ++_unmatchedSpawns;
            }
        }
    } else if (!object && !s.resref.empty() && s.resref[0] != '@' && (type == ObjectType::Creature || type == ObjectType::Placeable)) {
        // Created at runtime on the host: spawn our own copy from the same
        // template. Its spawn script does not run (a client runs no scripts).
        auto location = std::make_shared<Location>(glm::vec3(s.position.x, s.position.y, s.position.z), s.facing);
        try {
            object = module->area()->createObject(type, boost::to_lower_copy(s.resref), location);
        } catch (const std::exception &e) {
            warn("Co-op: cannot spawn " + s.resref + ": " + e.what());
            object.reset();
        }
        if (object) {
            replica.spawned = true;
            ++_unmatchedSpawns;
        }
    }
    if (!object) {
        ++_spawnFailures;
        return nullptr;
    }
    if (auto previous = _ids.host(object->id()); previous && *previous != s.id) {
        _replicas.erase(*previous);
    }
    replica.localId = object->id();
    _ids.bind(s.id, object->id());
    if (byKey) {
        ++_idsBoundByKey;
        if (object->id() == s.id) {
            ++_idsEqual;
        }
    }
    return object;
}

void CoopSession::clientApplyEntity(const EntityRecord &record) {
    uint32_t id = record.state.id;
    if (record.fields & kFieldRemoved) {
        auto it = _replicas.find(id);
        if (it == _replicas.end()) {
            return;
        }
        auto object = it->second.localId ? _game.getObjectById(it->second.localId) : nullptr;
        auto module = _game.module();
        if (object && object != _game.party().player() && module && module->area() &&
            object->type() != ObjectType::Door) {
            module->area()->destroyObject(*object);
        }
        _ids.unbindHost(id);
        _replicas.erase(it);
        return;
    }
    auto &replica = _replicas[id];
    bool unbound = replica.localId == 0;
    if (unbound && !(record.fields & kFieldIdentity)) {
        // A delta for an object we could not bind: nothing to apply it to.
        mergeEntity(replica.state, record.state, record.fields);
        return;
    }
    mergeEntity(replica.state, record.state, record.fields);
    std::shared_ptr<Object> object = replica.localId ? _game.getObjectById(replica.localId) : nullptr;
    if (!object) {
        replica.localId = 0;
        object = clientBind(replica);
        if (!object) {
            return;
        }
        replica.previousPosition = replica.state.position;
        replica.previousFacing = replica.state.facing;
        CoopReplication::apply(_game, *object, replica.state, static_cast<uint16_t>(kFieldAllMask & ~kFieldRemoved & ~kFieldIdentity));
        if (id == controlledHostId()) {
            // The creature this player controls arrived after its slot (and
            // perhaps its sheet) did.
            clientMirrorParty();
            if (auto creature = std::dynamic_pointer_cast<Creature>(object); creature && _sheetValid && _sheet.creature == id) {
                applyCoopCharacterSheet(_game, *creature, _sheet);
            }
        }
        return;
    }
    CoopReplication::apply(_game, *object, replica.state, record.fields);
    if ((record.fields & kFieldPosition) && object->type() == ObjectType::Creature) {
        if (auto module = _game.module(); module && module->area()) {
            module->area()->determineObjectRoom(*object);
        }
    }
}

void CoopSession::clientRetireUnbound() {
    auto module = _game.module();
    if (!module || !module->area()) {
        return;
    }
    auto player = _game.party().player();
    std::vector<std::shared_ptr<Object>> retire;
    for (const auto &object : module->area()->objects()) {
        if (!object || object == player || !CoopReplication::isReplicated(*object) || object->type() == ObjectType::Door) {
            continue;
        }
        if (!_ids.host(object->id())) {
            retire.push_back(object);
        }
    }
    for (auto &object : retire) {
        module->area()->destroyObject(*object);
    }
    if (!retire.empty()) {
        info("Co-op: removed " + std::to_string(retire.size()) + " local objects the host no longer has");
    }
}

void CoopSession::clientApplyWorld(uint16_t fields, const WorldState &world) {
    if (fields & kWorldScreen) {
        _world.screen = world.screen;
    }
    if (fields & kWorldFade) {
        _world.fadeOpacity = world.fadeOpacity;
        _world.fadeColor = world.fadeColor;
        _game._globalFade.applyReplicated(world.fadeOpacity, glm::vec3(world.fadeColor.x, world.fadeColor.y, world.fadeColor.z));
    }
    if (fields & kWorldMovie) {
        _world.movie = world.movie;
        _world.movieName = world.movieName;
        clientShowMovie();
    }
    if (fields & kWorldCamera) {
        _world.cameraType = world.cameraType;
        _world.cameraId = world.cameraId;
    }
    if (fields & kWorldConversation) {
        _world.conversation = world.conversation;
        clientShowConversation(world.conversation);
    }
    if (fields & kWorldPause) {
        _world.paused = world.paused;
        _world.pausedBy = world.pausedBy;
        clientShowPause();
    }
    if (fields & kWorldParty) {
        _world.party = world.party;
        _world.leader = world.leader;
        clientMirrorParty();
    }
}

void CoopSession::clientApplyEvent(const NetEvent &event) {
    switch (event.kind) {
    case net::EventKind::VfxBegin: {
        auto desc = _game._services.game.visualEffects.get(event.row);
        if (!desc || !*desc || _clientVfx.size() > 512) {
            break;
        }
        auto request = std::make_shared<VisualEffectPresenter::Request>();
        request->desc = *desc;
        request->location = glm::vec3(event.location.x, event.location.y, event.location.z);
        request->facing = event.facing;
        if (event.a) {
            auto local = _ids.local(event.a);
            if (!local) {
                break;
            }
            request->targetId = *local;
        }
        if (event.b) {
            // The beam's source; without it the row still shows on the target.
            if (auto source = _ids.local(event.b)) {
                request->sourceId = *source;
                request->sourceBodyNode = event.flags;
            }
        }
        _game.visualEffectPresenter().begin(request);
        _clientVfx[event.handle] = request;
        break;
    }
    case net::EventKind::VfxEnd: {
        auto it = _clientVfx.find(event.handle);
        if (it != _clientVfx.end()) {
            it->second->ended = true;
            _clientVfx.erase(it);
        }
        break;
    }
    case net::EventKind::Projectile: {
        auto attacker = std::dynamic_pointer_cast<Creature>(localObjectForHost(event.a));
        auto target = localObjectForHost(event.b);
        if (!attacker || !target || _projectiles.size() > 64) {
            break;
        }
        ClientProjectile flying;
        flying.projectile = std::make_unique<Projectile>((event.flags & 1) ? Projectile::Offhand : Projectile::Main,
                                                         (event.flags & 2) != 0);
        flying.projectile->fire(*attacker, *target, _game._services.scene.graphs.get(kSceneMain));
        _projectiles.push_back(std::move(flying));
        break;
    }
    }
}

void CoopSession::clientShowConversation(const ConversationState &conversation) {
    auto *current = _game._conversation;
    bool replicaRunning = current && current->isReplica();
    if (!conversation.active || conversation.entry < 0) {
        if (replicaRunning) {
            current->endReplica();
        }
        _replicaDialog.clear();
        return;
    }
    // The host leads; the client watches the full conversation screen: text,
    // speaker camera, animations and the replies, which it cannot pick. The
    // lines come from the client's own install; only indices cross the wire.
    Conversation *gui = conversation.kind == 1 ? static_cast<Conversation *>(_game._computer.get())
                                               : static_cast<Conversation *>(_game._dialog.get());
    auto module = _game.module();
    if (!gui || !module || !module->area()) {
        return;
    }
    if (!replicaRunning || current != gui || _replicaDialog != conversation.dialog) {
        std::shared_ptr<resource::Dialog> dialog;
        try {
            dialog = _game._services.resource.dialogs.get(conversation.dialog);
        } catch (const std::exception &e) {
            warn("Co-op: cannot load conversation " + conversation.dialog + ": " + e.what());
        }
        if (!dialog || conversation.entry >= static_cast<int>(dialog->entries.size())) {
            warn("Co-op: the host's conversation " + conversation.dialog + " is not in the local install");
            return;
        }
        if (replicaRunning) {
            current->endReplica();
        }
        auto owner = localObjectForHost(conversation.owner);
        _game._conversation = gui;
        gui->startReplica(dialog, owner);
        _replicaDialog = conversation.dialog;
    }
    gui->showReplicaEntry(conversation.entry, conversation.replies, conversation.entryEnded);
    if (_game._screen != Game::Screen::Conversation) {
        if (_game._container && _game._screen == Game::Screen::Container) {
            clientCloseContainerScreen(true);
        }
        _game.stopMovement();
        _game.setRelativeMouseMode(false);
        _game.setCursorType(resource::CursorType::Default);
        _game.changeScreen(Game::Screen::Conversation);
    }
}

void CoopSession::clientShowMovie() {
    // Everyone watches in sync; only the host can skip (design item 7).
    if (_world.movie && !_game._movie && !_world.movieName.empty()) {
        if (!_game.startVideo(_world.movieName)) {
            warn("Co-op: the host's movie " + _world.movieName + " is not in the local install");
        }
    } else if (!_world.movie && _game._movie) {
        _game._movie.reset();
        _game._globalFade.setMovieOverride(false);
        _game._timingDiscontinuity = true;
    }
}

void CoopSession::clientShowPause() {
    _game._paused = _world.paused;
    if (_world.paused) {
        showNotice(NoticeKind::Pause, "Paused by " + (_world.pausedBy.empty() ? std::string("the host") : _world.pausedBy));
    } else {
        showNotice(NoticeKind::Pause, std::string());
    }
}

void CoopSession::showNotice(NoticeKind kind, const std::string &text) {
    _lastNotice = text;
    if (kind == NoticeKind::Defeat) {
        // Party wipe: the host is choosing. The message stays until the
        // host's load arrives (clientEnterModule takes it down).
        _defeatShown = !text.empty();
        if (_defeatShown && showsPrompts()) {
            if (!_game._confirmPopup) {
                _game._confirmPopup = _game.tryLoadGUI<ConfirmPopup>();
            }
            if (_game._confirmPopup) {
                _game._confirmPopup->showChoices(text, {});
            }
        }
    }
    if (kind == NoticeKind::Pause) {
        // Held while the pause lasts; cleared on resume.
        bark(text, text.empty() ? 0.0f : 1.0e6f);
    } else if (!text.empty()) {
        bark(text, 4.0f);
    }
    if (!text.empty()) {
        _notices.push_back(text);
        while (_notices.size() > 16) {
            _notices.pop_front();
        }
        info("Co-op notice: " + text);
    }
}

void CoopSession::onMovieStarted(const std::string &name) {
    if (isHost()) {
        _hostMovieName = name.substr(0, kMaxStringBytes);
    }
}

void CoopSession::flushDestroyedObjects(Area &area) {
    area.doDestroyObjects();
}

void CoopSession::clientUpdateProjectiles(float dt) {
    for (auto it = _projectiles.begin(); it != _projectiles.end();) {
        it->age += dt;
        if (it->projectile->update(dt) || it->age > 5.0f) {
            it->projectile->reset();
            it = _projectiles.erase(it);
        } else {
            ++it;
        }
    }
}

void CoopSession::clientMirrorParty() {
    if (!isClient() || !_moduleLoaded) {
        return;
    }
    // The client's own party is presentation: its leader is the creature this
    // player controls, so the camera and HUD follow it.
    auto &party = _game.party();
    auto controlled = std::dynamic_pointer_cast<Creature>(localObjectForHost(controlledHostId()));
    try {
        std::vector<int> stale;
        for (const auto &member : party.members()) {
            if (member.npc != kNpcPlayer && (!controlled || member.creature != controlled)) {
                stale.push_back(member.npc);
            }
        }
        if (!stale.empty()) {
            party.setPartyLeader(kNpcPlayer);
            for (int npc : stale) {
                party.removeMember(npc);
            }
        }
        if (controlled && (_slot.status == SlotStatus::StandIn || _slot.status == SlotStatus::Guest)) {
            // A stand-in is outside the host's party; in this presentation
            // party it takes a free roster number so it can lead.
            if (!party.isMember(*controlled)) {
                int npc = -1;
                for (int candidate = static_cast<int>(Party::kK1NpcCount) - 1; candidate >= 0; --candidate) {
                    if (!party.getMemberByNPC(candidate)) {
                        npc = candidate;
                        break;
                    }
                }
                if (npc >= 0) {
                    party.addAvailableMember(npc, controlled);
                    party.addMember(npc, controlled);
                }
            }
            for (const auto &member : party.members()) {
                if (member.creature == controlled) {
                    party.setPartyLeader(member.npc);
                    break;
                }
            }
        } else if (controlled && _slot.npc >= 0) {
            if (!party.isMember(*controlled)) {
                if (!party.isMemberAvailable(_slot.npc)) {
                    party.addAvailableMember(_slot.npc, controlled);
                }
                party.addMember(_slot.npc, controlled);
            }
            if (party.isMember(*controlled)) {
                party.setPartyLeader(_slot.npc);
            }
        }
    } catch (const std::exception &e) {
        warn(std::string("Co-op: could not mirror the party: ") + e.what());
    }
}

void CoopSession::present(float alpha) {
    if (!isClient() || !_moduleLoaded) {
        return;
    }
    for (auto &[id, replica] : _replicas) {
        if (!replica.interpolate || !replica.localId) {
            continue;
        }
        auto object = _game.getObjectById(replica.localId);
        if (!object) {
            continue;
        }
        const auto &from = replica.previousPosition;
        const auto &to = replica.state.position;
        Vec3 position {from.x + (to.x - from.x) * alpha, from.y + (to.y - from.y) * alpha, from.z + (to.z - from.z) * alpha};
        CoopReplication::presentTransform(*object, position, lerpAngle(replica.previousFacing, replica.state.facing, alpha));
    }
}

bool CoopSession::interceptClick(const std::shared_ptr<Object> &object) {
    if (!isClient()) {
        return false;
    }
    if (!object) {
        return true;
    }
    auto hostId = _ids.host(object->id());
    uint32_t controlled = controlledHostId();
    if (!hostId || !controlled) {
        return true;
    }
    Command command;
    command.actor = controlled;
    command.target = *hostId;
    switch (object->type()) {
    case ObjectType::Creature: {
        auto creature = std::static_pointer_cast<Creature>(object);
        auto actor = std::dynamic_pointer_cast<Creature>(localObjectForHost(controlled));
        bool hostile = actor && !creature->isDead() && _game._services.game.reputes.getIsEnemy(*actor, *creature);
        command.type = hostile ? CommandType::Attack : CommandType::UseObject;
        break;
    }
    case ObjectType::Door:
        command.type = CommandType::OpenDoor;
        break;
    default:
        command.type = CommandType::UseObject;
        break;
    }
    submitCommand(command);
    return true;
}

uint32_t CoopSession::submitCommand(Command command) {
    if (!isClient() || _clientState != ClientState::Joined || !_hostConnection) {
        return 0;
    }
    command.seq = _nextSeq++;
    if (!command.actor) {
        command.actor = controlledHostId();
    }
    send(_hostConnection, encode(command));
    return command.seq;
}

const CommandResult *CoopSession::commandResult(uint32_t seq) const {
    for (auto it = _results.rbegin(); it != _results.rend(); ++it) {
        if (it->seq == seq) {
            return &*it;
        }
    }
    return nullptr;
}

std::shared_ptr<Object> CoopSession::localObjectForHost(uint32_t hostId) const {
    if (!hostId) {
        return nullptr;
    }
    auto local = _ids.local(hostId);
    return local ? _game.getObjectById(*local) : nullptr;
}

uint32_t CoopSession::resolveHostId(const std::string &idOrTag) const {
    if (idOrTag.empty()) {
        return 0;
    }
    bool numeric = std::all_of(idOrTag.begin(), idOrTag.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    if (numeric) {
        uint32_t id = static_cast<uint32_t>(std::stoul(idOrTag));
        return _replicas.count(id) ? id : 0;
    }
    for (const auto &[id, replica] : _replicas) {
        if (boost::iequals(replica.state.tag, idOrTag)) {
            return id;
        }
    }
    return 0;
}

// Harness

json::object CoopSession::statusJson() const {
    json::object root;
    root["role"] = coopRoleName(_config.role);
    root["mode"] = _config.mode;
    root["transport"] = _transport ? _transport->describe() : std::string("none");
    root["build"] = _config.build;
    root["k1"] = describeFingerprint(_config.k1);
    root["k2"] = describeFingerprint(_config.k2);
    root["tick"] = _game.tickCount();
    root["partyDefeated"] = partyDefeated();
    if (_transport) {
        auto online = _transport->onlineStatus();
        if (online.online) {
            json::object code;
            code["state"] = online.state;
            code["code"] = online.code;
            code["display"] = online.code.empty() ? std::string() : displayJoinCode(online.code);
            code["error"] = online.error;
            code["detail"] = online.detail;
            code["service"] = online.service;
            root["online"] = std::move(code);
        }
        auto stats = _transport->stats();
        json::object transport;
        transport["bytesSent"] = stats.bytesSent;
        transport["bytesReceived"] = stats.bytesReceived;
        transport["messagesSent"] = stats.messagesSent;
        transport["messagesReceived"] = stats.messagesReceived;
        root["traffic"] = std::move(transport);
    }
    if (isHost()) {
        root["generation"] = _generation;
        auto module = _game.module();
        root["module"] = module ? json::value(module->name()) : json::value(nullptr);
        root["loadKeys"] = _hostKeys.size();
        json::array clients;
        for (const auto &[connection, client] : _clients) {
            json::object entry;
            entry["id"] = client.clientId;
            entry["name"] = client.name;
            entry["address"] = client.address;
            entry["welcomed"] = client.welcomed;
            json::object slot;
            slot["status"] = client.slotRequested ? slotStatusName(client.slot.status) : "not_requested";
            slot["npc"] = client.slot.npc;
            slot["id"] = client.slot.creatureId;
            slot["tag"] = client.slot.tag;
            entry["slot"] = std::move(slot);
            entry["generation"] = client.moduleGeneration;
            entry["commandsAccepted"] = client.commandsAccepted;
            entry["commandsRejected"] = client.commandsRejected;
            json::object rejects;
            for (const auto &[reason, count] : client.rejectCounts) {
                rejects[commandRejectName(static_cast<CommandReject>(reason))] = count;
            }
            entry["rejects"] = std::move(rejects);
            entry["violations"] = client.violations;
            entry["snapshots"] = client.snapshotsSent;
            entry["snapshotBytes"] = client.snapshotBytes;
            entry["avgSnapshotBytes"] = client.snapshotsSent ? static_cast<double>(client.snapshotBytes) / client.snapshotsSent : 0.0;
            entry["maxSnapshotBytes"] = client.maxSnapshotBytes;
            entry["fullSnapshotBytes"] = client.fullSnapshotBytes;
            entry["history"] = client.history.size();
            entry["ackedTick"] = client.ackedTick;
            entry["acks"] = client.acks;
            entry["resends"] = client.resends;
            entry["pingMs"] = _transport ? _transport->pingMs(connection) : -1;
            static const char *kApproval[] {"none", "pending", "accepted", "declined"};
            entry["approval"] = kApproval[static_cast<int>(client.approval)];
            entry["trusted"] = client.trusted;
            if (_transport) {
                auto route = _transport->routeDescription(connection);
                if (!route.empty()) {
                    entry["route"] = route.substr(0, 1500);
                }
            }
            entry["viewing"] = client.viewing;
            entry["controlled"] = isPlayerControlled(client.slot.creatureId);
            {
                json::array handed;
                for (uint32_t id : client.handedOver) {
                    handed.push_back(id);
                }
                entry["handedOver"] = std::move(handed);
            }
            if (client.guestReceived) {
                json::object guest;
                auto record = _guests.find(client.name);
                guest["id"] = client.guestId;
                guest["store"] = client.storeOpen;
                guest["bench"] = client.benchOpen;
                if (record != _guests.end()) {
                    guest["name"] = record->second.name;
                    guest["xp"] = record->second.xp;
                    guest["credits"] = record->second.credits;
                    guest["items"] = record->second.items.size();
                }
                if (auto creature = client.guestId ? _game.getObjectById<Creature>(client.guestId) : nullptr) {
                    guest["position"] = json::array {creature->position().x, creature->position().y, creature->position().z};
                    guest["hp"] = creature->currentHitPoints();
                    guest["level"] = creature->attributes().getAggregateLevel();
                    guest["inParty"] = _game.party().isMember(*creature);
                    guest["puppet"] = creature->isPuppet();
                    // Items carrying upgrades (template@bits), from the bag and the equipment.
                    json::array upgraded;
                    auto addUpgraded = [&upgraded](const std::shared_ptr<Item> &item) {
                        if (item && item->upgrades()) {
                            upgraded.push_back(json::value(item->blueprintResRef() + "@" + std::to_string(item->upgrades())));
                        }
                    };
                    for (const auto &item : creature->items()) {
                        addUpgraded(item);
                    }
                    for (const auto &[slot, item] : creature->equipment()) {
                        addUpgraded(item);
                    }
                    guest["upgraded"] = std::move(upgraded);
                    guest["carried"] = creature->items().size();
                }
                entry["guest"] = std::move(guest);
            }
            if (client.standIn) {
                json::object standIn;
                standIn["id"] = client.standIn;
                auto source = _standInSource.find(client.standIn);
                standIn["source"] = source != _standInSource.end() ? source->second : 0u;
                if (auto creature = _game.getObjectById<Creature>(client.standIn)) {
                    standIn["tag"] = creature->tag();
                    standIn["position"] = json::array {creature->position().x, creature->position().y, creature->position().z};
                    standIn["hp"] = creature->currentHitPoints();
                    standIn["maxHp"] = creature->maxHitPoints();
                    standIn["inParty"] = _game.party().isMember(*creature);
                }
                entry["standIn"] = std::move(standIn);
            } else {
                entry["standIn"] = nullptr;
            }
            clients.push_back(std::move(entry));
        }
        root["clients"] = std::move(clients);
        root["askToJoin"] = _config.askToJoin;
        root["guestRecords"] = _guests.size();
        root["rejoinTokens"] = _rejoinTokens.size();
        json::object reservations;
        for (const auto &[name, tag] : _reservations) {
            reservations[name] = tag;
        }
        root["reservations"] = std::move(reservations);
        json::object controlled;
        for (const auto &[connection, client] : _clients) {
            if (isPlayerControlled(client.slot.creatureId)) {
                controlled[client.slot.tag] = client.name;
            }
        }
        root["controlled"] = std::move(controlled);
        root["players"] = hostPlayerCount();
        root["barksSent"] = _barksSent;
        root["partyGold"] = _game.party().gold();
        root["pcSpeaker"] = _pcSpeaker;
        if (freeMode()) {
            json::array list;
            for (const auto &entry : guestList()) {
                list.push_back(json::object {{"name", entry.name}, {"hp", entry.hp}, {"maxHp", entry.maxHp}, {"level", entry.level},
                                             {"present", entry.present}});
            }
            root["guestList"] = std::move(list);
        }
        {
            json::object standIns;
            auto module = _game.module();
            standIns["pause"] = _standInPause;
            standIns["rule"] = module ? coopStandInRuleName(coopStandInRule(module->name())) : "play";
            standIns["soloMode"] = _game.party().isSoloMode();
            standIns["partySize"] = _game.party().getSize();
            standIns["count"] = _standInSource.size();
            root["standIns"] = std::move(standIns);
        }
        json::object scale;
        scale["hp"] = _scale.hp;
        scale["damage"] = _scale.damage;
        scale["scaledCreatures"] = _scaled.size();
        root["enemyScale"] = std::move(scale);
        json::object options;
        options["hostOnlyTransitions"] = _config.hostOnlyTransitions;
        options["hostOnlyTrading"] = _config.hostOnlyTrading;
        options["noPause"] = _config.noPause;
        options["pauseCooldown"] = _config.pauseCooldown;
        options["enemyScaling"] = _config.enemyScaling;
        root["options"] = std::move(options);
        root["paused"] = _game.isPaused();
        root["pausedBy"] = _game.isPaused() ? _pausedBy : std::string();
        {
            auto message = _ready.message(hostNow());
            json::object ready;
            ready["id"] = message.id;
            ready["active"] = _ready.active();
            ready["state"] = static_cast<int>(message.state);
            ready["destination"] = message.destination;
            ready["initiator"] = message.initiator;
            ready["secondsLeft"] = message.secondsLeft;
            json::array players;
            for (const auto &player : message.players) {
                players.push_back(json::object {{"name", player.name}, {"answer", static_cast<int>(player.answer)}, {"busy", player.busy}});
            }
            ready["players"] = std::move(players);
            root["readyCheck"] = std::move(ready);
        }
    } else {
        static const char *kStates[] {"connecting", "handshaking", "joined", "rejected", "disconnected", "reconnecting"};
        root["state"] = kStates[static_cast<int>(_clientState)];
        static const char *kProgress[] {"connecting", "waiting", "joined", "reconnecting", "failed"};
        root["progress"] = kProgress[static_cast<int>(joinProgress())];
        if (joinProgress() == JoinProgress::Failed) {
            root["failure"] = failureMessage();
        }
        root["reconnects"] = _reconnects;
        root["reconnecting"] = _reconnecting;
        if (_clientState == ClientState::Rejected) {
            json::object reject;
            reject["reason"] = rejectReasonName(_reject.reason);
            reject["detail"] = _reject.detail;
            root["reject"] = std::move(reject);
        }
        if (!_disconnectReason.empty()) {
            root["disconnect"] = _disconnectReason;
        }
        root["clientId"] = _welcome.clientId;
        root["standInSpawns"] = _standInSpawns;
        root["steering"] = _steering;
        root["steerCommands"] = _steerCommands;
        if (freeMode()) {
            json::object guest;
            guest["sent"] = _guestSent;
            guest["name"] = _guest.name;
            guest["xp"] = _guest.xp;
            guest["credits"] = _guest.credits;
            int level = 0;
            for (const auto &entry : _guest.classes) {
                level += entry.level;
            }
            guest["level"] = level;
            guest["items"] = _guest.items.size();
            guest["saves"] = _guestSaves;
            guest["file"] = guestPath().string();
            root["guest"] = std::move(guest);
            json::object offer;
            offer["kind"] = static_cast<int>(_offer.kind);
            offer["target"] = _offer.target;
            offer["name"] = _offer.name;
            offer["pazaak"] = _guestPazaak;
            offer["terminal"] = _clientTerminal;
            offer["terminalRead"] = _terminalRead;
            root["offer"] = std::move(offer);
        }
        {
            // Stores: Free Mode (a guest's own wallet) and Story Mode (the
            // shared party pool, "host-only trading" permitting) both open
            // through the same StoreView.
            json::object store;
            store["id"] = _storeView.store;
            store["open"] = _storeView.open;
            store["credits"] = _storeView.credits;
            json::array stock;
            for (const auto &entry : _storeView.stock) {
                if (stock.size() < 40) {
                    stock.push_back(json::value(entry.resref + "@" + std::to_string(entry.price)));
                }
            }
            store["stock"] = std::move(stock);
            store["sellable"] = _storeView.sellable.size();
            root["store"] = std::move(store);
        }
        {
            // Workbenches and labs: the same view for Free and Story Mode guests.
            json::object bench;
            bench["id"] = _benchView.bench;
            bench["open"] = _benchView.open;
            bench["kind"] = _benchView.kind == BenchKind::Lab ? "lab" : "workbench";
            bench["components"] = _benchView.components;
            bench["chemicals"] = _benchView.chemicals;
            bench["items"] = _benchView.items.size();
            bench["parts"] = _benchView.parts.size();
            bench["breakdown"] = _benchView.breakdown.size();
            bench["recipes"] = _benchView.recipes.size();
            root["bench"] = std::move(bench);
        }
        if (freeMode()) {
            json::array list;
            for (const auto &entry : guestList()) {
                list.push_back(json::object {{"name", entry.name}, {"hp", entry.hp}, {"maxHp", entry.maxHp}, {"level", entry.level},
                                             {"present", entry.present}});
            }
            root["guestList"] = std::move(list);
        }
        {
            json::object inventory;
            inventory["items"] = _invHost.size();
            inventory["updates"] = _inventoryUpdates;
            inventory["credits"] = _inventoryCredits;
            json::array usable;
            if (auto player = _game.party().player()) {
                for (const auto &item : player->items()) {
                    if (item && _invLocal.count(item->id()) && item->activateSpell() && usable.size() < 32) {
                        usable.push_back(json::value(item->blueprintResRef() + "x" + std::to_string(item->stackSize())));
                    }
                }
            }
            inventory["usable"] = std::move(usable);
            root["inventory"] = std::move(inventory);
            json::object sheet;
            sheet["valid"] = _sheetValid;
            sheet["updates"] = _sheetUpdates;
            if (_sheetValid) {
                sheet["creature"] = _sheet.creature;
                sheet["xp"] = _sheet.xp;
                sheet["levelUp"] = _sheet.levelUp;
                int level = 0;
                for (const auto &entry : _sheet.classes) {
                    level += entry.level;
                }
                sheet["level"] = level;
                sheet["feats"] = _sheet.feats.size();
                sheet["spells"] = _sheet.spells.size();
                json::object equipment;
                for (const auto &entry : _sheet.equipment) {
                    equipment[std::to_string(entry.slot)] = entry.resref;
                }
                sheet["equipment"] = std::move(equipment);
            }
            root["sheet"] = std::move(sheet);
            json::object barks;
            barks["received"] = _barksReceived;
            barks["last"] = _lastBark;
            root["barks"] = std::move(barks);
        }
        json::object slot;
        slot["status"] = slotStatusName(_slot.status);
        slot["npc"] = _slot.npc;
        slot["id"] = _slot.creatureId;
        slot["tag"] = _slot.tag;
        root["slot"] = std::move(slot);
        root["module"] = _module.module;
        root["generation"] = _module.generation;
        root["loaded"] = _moduleLoaded;
        root["appliedTick"] = _appliedTick;
        root["queued"] = _buffer.size();
        root["playTick"] = _playTick;
        root["delayTicks"] = _delayTicks;
        root["jitter"] = _jitter;
        root["baseMissing"] = _baseMissing;
        root["acksSent"] = _acksSent;
        root["eventsQueued"] = _eventQueue.size();
        if (_transport) {
            auto route = _transport->routeDescription(_hostConnection);
            if (!route.empty() && _hostConnection) {
                root["route"] = route.substr(0, 1500);
            }
            root["pingMs"] = _hostConnection ? _transport->pingMs(_hostConnection) : -1;
        }
        root["snapshotsApplied"] = _snapshotsApplied;
        root["snapshotsDropped"] = _snapshotsDropped;
        root["bytesReceived"] = _bytesReceived;
        root["replicas"] = _replicas.size();
        root["bound"] = _ids.size();
        root["boundByKey"] = _idsBoundByKey;
        root["idsEqual"] = _idsEqual;
        root["spawned"] = _unmatchedSpawns;
        root["unmatched"] = _spawnFailures;
        root["violations"] = _hostMessageViolations;
        root["scriptsSuppressed"] = _game.scriptRunner().isSuppressed();
        root["paused"] = _world.paused;
        root["pausedBy"] = _world.pausedBy;
        root["screen"] = static_cast<int>(_game._screen);
        root["conversationReplica"] = _game._conversation && _game._conversation->isReplica() ? json::value(_replicaDialog) : json::value(nullptr);
        if (_game._conversation && _game._conversation->isReplica()) {
            root["conversationEntry"] = _game._conversation->currentEntryIndex();
        }
        root["movie"] = static_cast<bool>(_game._movie);
        root["viewing"] = _clientViewing;
        if (_clientViewing) {
            json::array items;
            if (auto local = localObjectForHost(_clientViewing)) {
                for (const auto &item : local->items()) {
                    items.push_back(json::object {{"local", item->id()}, {"host", hostItemId(item->id())}, {"tag", item->tag()},
                                                  {"stack", item->stackSize()}});
                }
            }
            root["containerItems"] = std::move(items);
        }
        {
            json::object ready;
            ready["id"] = _clientReady.id;
            ready["state"] = static_cast<int>(_clientReady.state);
            ready["destination"] = _clientReady.destination;
            ready["initiator"] = _clientReady.initiator;
            ready["secondsLeft"] = _clientReady.secondsLeft;
            ready["answered"] = _clientReadyAnswered;
            root["readyCheck"] = std::move(ready);
        }
        json::array notices;
        for (const auto &notice : _notices) {
            notices.push_back(json::value(notice));
        }
        root["notices"] = std::move(notices);
        if (auto leader = _game.party().getLeader()) {
            root["localLeader"] = leader->tag();
        }
        if (!_results.empty()) {
            const auto &last = _results.back();
            json::object result;
            result["seq"] = last.seq;
            result["accepted"] = last.accepted;
            result["reason"] = commandRejectName(last.reason);
            result["hostTick"] = last.hostTick;
            root["lastResult"] = std::move(result);
        }
    }
    return root;
}

json::object CoopSession::dumpJson() const {
    json::object root;
    root["role"] = coopRoleName(_config.role);
    if (!isClient()) {
        return root;
    }
    root["tick"] = _appliedTick;
    root["generation"] = _loadedGeneration;
    root["controlled"] = controlledHostId();
    json::array objects;
    for (const auto &[id, replica] : _replicas) {
        const auto &s = replica.state;
        json::object entry;
        entry["id"] = id;
        entry["local"] = replica.localId;
        entry["type"] = s.type;
        entry["tag"] = s.tag;
        if (!s.key.empty()) {
            entry["key"] = s.key;
        }
        entry["pos"] = vec3Json(s.position);
        entry["facing"] = s.facing;
        if (!s.anim.empty()) {
            entry["anim"] = s.anim;
        }
        entry["hp"] = s.hp;
        entry["maxhp"] = s.maxHp;
        entry["dead"] = s.dead;
        if (s.type == static_cast<uint8_t>(ObjectType::Door) || s.type == static_cast<uint8_t>(ObjectType::Placeable)) {
            entry["open"] = s.open;
            entry["locked"] = s.locked;
        }
        if (s.attackTarget) {
            entry["attackTarget"] = s.attackTarget;
        }
        if (!s.rightWeapon.empty()) {
            entry["weapon"] = s.rightWeapon;
        }
        if (!s.armor.empty()) {
            entry["armor"] = s.armor;
        }
        if (!s.headgear.empty()) {
            entry["headgear"] = s.headgear;
        }
        if (replica.spawned) {
            entry["spawned"] = true;
        }
        // Where the client's own copy stands: must equal the host state.
        if (auto object = localObjectForHost(id)) {
            entry["localPos"] = json::array {object->position().x, object->position().y, object->position().z};
            if (auto creature = std::dynamic_pointer_cast<Creature>(object)) {
                entry["localHp"] = creature->currentHitPoints();
                entry["localDead"] = creature->isDead();
                auto equipped = [&](int slot) {
                    auto item = creature->getEquippedItem(slot);
                    return item ? item->blueprintResRef() : std::string();
                };
                entry["localArmor"] = equipped(InventorySlots::body);
                entry["localWeapon"] = equipped(InventorySlots::rightWeapon);
                entry["localAppearance"] = creature->appearance();
            }
            if (auto door = std::dynamic_pointer_cast<Door>(object)) {
                entry["localOpen"] = door->isOpen();
            }
        } else {
            entry["unbound"] = true;
        }
        objects.push_back(std::move(entry));
    }
    root["objects"] = std::move(objects);
    json::object world;
    world["screen"] = _world.screen;
    world["fade"] = _world.fadeOpacity;
    world["movie"] = _world.movie;
    world["camera"] = _world.cameraType;
    json::array party;
    for (const auto &entry : _world.party) {
        party.push_back(json::object {{"npc", entry.npc}, {"id", entry.id}});
    }
    world["party"] = std::move(party);
    world["leader"] = _world.leader;
    const auto &conversation = _world.conversation;
    if (conversation.active) {
        json::object dialog;
        dialog["dialog"] = conversation.dialog;
        dialog["entry"] = conversation.entry;
        dialog["owner"] = conversation.owner;
        dialog["speaker"] = conversation.speaker;
        json::array replies;
        for (auto reply : conversation.replies) {
            replies.push_back(reply);
        }
        dialog["replies"] = std::move(replies);
        dialog["entryEnded"] = conversation.entryEnded;
        world["conversation"] = std::move(dialog);
    } else {
        world["conversation"] = nullptr;
    }
    root["world"] = std::move(world);
    return root;
}

} // namespace game

} // namespace reone
