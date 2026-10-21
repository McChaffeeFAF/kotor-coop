/* Copyright (c) 2026 The reone project contributors */

// Text chat and pings. A client asks the host to relay a line or a marker; the
// host checks it (length, UTF-8, control characters, rate, mute, a real object),
// then sends it to the players its channel reaches. The rules themselves live in
// coopchat.cpp; this file connects them to the session.

#include "reone/game/coopsession.h"

#include <algorithm>
#include <cmath>

#include "reone/game/game.h"
#include "reone/game/gui/hud.h"
#include "reone/game/object.h"
#include "reone/system/logutil.h"

using namespace reone::net;

namespace reone {

namespace game {

namespace {

/** At most max bytes of a UTF-8 string, cut on a character boundary. */
std::string cutUtf8(const std::string &text, size_t max) {
    if (text.size() <= max) {
        return text;
    }
    size_t end = max;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        --end;
    }
    return text.substr(0, end);
}

/** A display name the wire accepts: valid UTF-8, no controls, 1..32 bytes. */
std::string chatName(const std::string &name) {
    std::string out = validUtf8(name) ? cutUtf8(stripChatControls(name), 32) : std::string();
    return out.empty() ? "Player" : out;
}

} // namespace

double CoopSession::chatNow() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - _chatEpoch).count();
}

bool CoopSession::chatSay(const std::string &text, ChatChannel channel) {
    if (isHost()) {
        std::string cleaned;
        auto verdict = _chatPolicy.checkChat(0, text, chatNow(), cleaned);
        if (verdict != ChatVerdict::Ok) {
            return false;
        }
        hostRelayChat(0, chatName(_config.playerName.empty() ? "Host" : _config.playerName), channel, cleaned);
        return true;
    }
    if (!isClient() || text.empty() || text.size() > kMaxChatWireBytes) {
        return false;
    }
    ChatSend chat;
    chat.channel = channel;
    chat.text = text;
    if (_clientState != ClientState::Joined || !_hostConnection) {
        return false;
    }
    send(_hostConnection, encode(chat));
    return true;
}

bool CoopSession::pingAt(const glm::vec3 &point, uint32_t objectId) {
    if (isHost()) {
        auto object = objectId ? _game.getObjectById(objectId) : nullptr;
        auto verdict = _chatPolicy.checkPing(0, objectId != 0, object != nullptr, chatNow());
        if (verdict != PingVerdict::Ok) {
            return false;
        }
        hostRelayPing(0, chatName(_config.playerName.empty() ? "Host" : _config.playerName), point, objectId);
        return true;
    }
    if (!isClient()) {
        return false;
    }
    PingSend ping;
    ping.position = {point.x, point.y, point.z};
    if (objectId) {
        auto host = _ids.host(objectId);
        if (!host) {
            return false; // an object the host does not know of
        }
        ping.target = *host;
    }
    if (_clientState != ClientState::Joined || !_hostConnection) {
        return false;
    }
    send(_hostConnection, encode(ping));
    return true;
}

bool CoopSession::chatMute(const std::string &name, bool muted) {
    if (!isHost()) {
        return false;
    }
    if (muted) {
        _chatPolicy.mute(name);
        info("Co-op: muted " + name);
        return true;
    }
    bool was = _chatPolicy.unmute(name);
    if (was) {
        info("Co-op: unmuted " + name);
    }
    return was;
}

// Host

void CoopSession::hostHandleChat(HostClient &client, const ChatSend &chat) {
    _chatPolicy.setName(client.clientId, client.name);
    std::string cleaned;
    auto verdict = _chatPolicy.checkChat(client.clientId, chat.text, chatNow(), cleaned);
    if (verdict != ChatVerdict::Ok) {
        ++_chatsRefused;
        hostNotice(client, NoticeKind::Refusal, chatVerdictText(verdict));
        return;
    }
    hostRelayChat(client.clientId, chatName(client.name), chat.channel, cleaned);
}

void CoopSession::hostHandlePing(HostClient &client, const PingSend &ping) {
    _chatPolicy.setName(client.clientId, client.name);
    std::shared_ptr<Object> object = ping.target ? _game.getObjectById(ping.target) : nullptr;
    auto verdict = _chatPolicy.checkPing(client.clientId, ping.target != 0, object != nullptr, chatNow());
    if (verdict != PingVerdict::Ok) {
        hostNotice(client, NoticeKind::Refusal, pingVerdictText(verdict));
        return;
    }
    hostRelayPing(client.clientId, chatName(client.name), glm::vec3(ping.position.x, ping.position.y, ping.position.z), ping.target);
}

void CoopSession::hostRelayChat(uint32_t sender, const std::string &name, ChatChannel channel, const std::string &text) {
    ChatPeer from {sender, ChatSide::Party};
    std::vector<ChatPeer> peers {{0, ChatSide::Party}};
    std::map<uint32_t, ConnectionId> connections;
    for (const auto &[connection, client] : _clients) {
        if (!client.welcomed) {
            continue;
        }
        ChatSide side = freeMode() && client.slot.status == SlotStatus::Guest ? ChatSide::Guest : ChatSide::Party;
        peers.push_back({client.clientId, side});
        connections[client.clientId] = connection;
        if (client.clientId == sender) {
            from.side = side;
        }
    }
    ChatMessage message;
    message.sender = sender;
    message.name = name;
    message.colour = chatColourFor(sender);
    message.channel = channel;
    message.text = text;
    auto bytes = encode(message);
    for (uint32_t id : chatRecipients(freeMode(), channel, from, peers)) {
        if (id == 0) {
            chatReceived(message);
        } else if (auto it = connections.find(id); it != connections.end()) {
            send(it->second, bytes);
        }
    }
    ++_chatsRelayed;
}

void CoopSession::hostRelayPing(uint32_t sender, const std::string &name, const glm::vec3 &point, uint32_t objectId) {
    PingMarker marker;
    marker.sender = sender;
    marker.name = name;
    marker.colour = chatColourFor(sender);
    marker.position = {point.x, point.y, point.z};
    marker.seconds = kPingLifetime;
    if (objectId) {
        // The host places the marker: on the object itself, whatever point the client sent.
        if (auto object = _game.getObjectById(objectId)) {
            const auto &position = object->position();
            marker.target = objectId;
            marker.position = {position.x, position.y, position.z};
            std::string label = object->name().empty() ? object->tag() : object->name();
            marker.targetName = validUtf8(label) ? cutUtf8(stripChatControls(label), kMaxStringBytes) : std::string();
        }
    }
    auto bytes = encode(marker);
    for (auto &[connection, client] : _clients) {
        if (client.welcomed) {
            send(connection, bytes);
        }
    }
    pingReceived(marker);
    ++_pingsRelayed;
}

// Client (and the host's own view)

void CoopSession::chatReceived(const ChatMessage &message) {
    ++_chatsReceived;
    ChatLine line;
    line.time = chatNow();
    line.sender = message.sender;
    line.name = message.name;
    line.colour = message.colour;
    line.channel = message.channel;
    line.text = message.text;
    _chatLog.add(std::move(line)); // the HUD's chat overlay draws it (hudchat.cpp)
}

void CoopSession::pingReceived(const PingMarker &marker) {
    _pingBoard.add(marker, chatNow());
}

} // namespace game

} // namespace reone
