/* Copyright (c) 2026 The reone project contributors */

// The co-op side of the dedicated host (docs/dedicated_host.md): kicking a
// player, and the rules for a session with no local leader. The story driver
// is the guest longest in the session; conversations and terminals the world
// opens are handed to it, and a conversation nobody answers moves on by itself.

#include "reone/game/coopsession.h"

#include <algorithm>

#include "reone/game/game.h"
#include "reone/game/gui/conversation.h"
#include "reone/game/object/placeable.h"
#include "reone/system/logutil.h"

using namespace reone::net;

namespace reone {

namespace game {

bool CoopSession::kick(uint32_t clientId) {
    if (!isHost()) {
        return false;
    }
    for (auto &[connection, client] : _clients) {
        if (client.clientId != clientId) {
            continue;
        }
        // Not a ban: the player may join again. The Reject names the kick to the player.
        hostReject(client, RejectReason::Kicked, "removed by the host");
        return true;
    }
    return false;
}

net::CommandReject CoopSession::hostDriverConversation(HostClient &client, const std::shared_ptr<Object> &target) {
    if (_game.currentScreen() == Game::Screen::Conversation || client.terminalControl) {
        return CommandReject::Busy;
    }
    // As a handed-over terminal runs (hostGuestTerminal): the host's own
    // session of the conversation, shown to everyone, with this guest making
    // the choices and its character's skills making the checks.
    _pcSpeaker = client.guestId;
    client.terminalControl = target->id();
    client.terminalStartTick = _game.tickCount();
    _game.startDialog(target, target->conversation());
    hostNotice(client, NoticeKind::TerminalControl, "on");
    info("Co-op: " + client.name + " leads the conversation with " + target->tag() + " (dedicated host)");
    return CommandReject::None;
}

void CoopSession::hostUpdateDedicated() {
    // Who drives the story: the guest longest in the session whose character is in.
    std::vector<uint32_t> present;
    for (const auto &[connection, client] : _clients) {
        bool admitted = client.approval != HostClient::Approval::Pending && client.approval != HostClient::Approval::Declined;
        if (client.welcomed && admitted && client.guestReceived) {
            present.push_back(client.clientId);
        }
    }
    if (_storyDrivers.update(present)) {
        uint32_t next = _storyDrivers.driver();
        info("Co-op: the story driver is " + (next ? "client " + std::to_string(next) : std::string("nobody")));
    }
    HostClient *driver = nullptr;
    for (auto &[connection, client] : _clients) {
        if (client.clientId == _storyDrivers.driver() && client.welcomed) {
            driver = &client;
        }
    }
    if (driver && _driverNoticed != driver->clientId) {
        _driverNoticed = driver->clientId;
        hostNotice(*driver, NoticeKind::Info, "You lead the story: the server has no leader");
    } else if (!driver) {
        _driverNoticed = 0;
    }

    bool talking = _game.currentScreen() == Game::Screen::Conversation && _game._conversation && _game._conversation->isRunning();
    if (!talking) {
        _conversationFallback.reset();
        bool anyControl = std::any_of(_clients.begin(), _clients.end(), [](const auto &entry) {
            return entry.second.terminalControl != 0;
        });
        if (!anyControl) {
            _pcSpeaker = 0; // a driver that left mid-conversation leaves no speaker behind
        }
        return;
    }
    auto state = _game._conversation->inspectionState();
    uint32_t owner = state.owner ? state.owner->id() : 0;
    bool controlled = std::any_of(_clients.begin(), _clients.end(), [owner](const auto &entry) {
        return entry.second.terminalControl != 0 && entry.second.terminalControl == owner;
    });
    if (!controlled && driver && owner && !driver->terminalControl) {
        // A conversation the world started (a script, a cutscene, a companion):
        // the driver answers it, as the leader would.
        _pcSpeaker = driver->guestId;
        driver->terminalControl = owner;
        driver->terminalStartTick = _game.tickCount();
        hostNotice(*driver, NoticeKind::TerminalControl, "on");
        info("Co-op: " + driver->name + " answers the conversation (dedicated host)");
        controlled = true;
    }
    float dt = 1.0f / static_cast<float>(std::max(1, _game.tickRate()));
    if (_conversationFallback.update(dt, _game._conversation->isReplyListShown(), controlled)) {
        info("Co-op: nobody answered; the conversation takes its first reply (dedicated host)");
        _game._conversation->pickReplyAtRow(0);
    }
}

} // namespace game

} // namespace reone
