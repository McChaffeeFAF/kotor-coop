/* Copyright (c) 2026 The reone project contributors */

#include "reone/net/transport.h"

#ifdef REONE_HAVE_GNS

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <mutex>
#include <random>
#include <set>

#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingcustomsignaling.h>
#include <steam/steamnetworkingsockets.h>

#include "reone/net/joincode.h"
#include "reone/net/protocol.h"
#include "reone/net/signallink.h"
#include "reone/system/logutil.h"

namespace reone {

namespace net {

namespace {

class GnsTransport;

std::mutex g_mutex;
std::vector<GnsTransport *> g_instances;
int g_initCount = 0;
NetworkSimulation g_simulation;

// Drop detection: a connection that hears nothing for this long is closed,
// and the client starts reconnecting. P2P setup (ICE checks, a relay
// allocation) takes longer than a direct connect, so its budget is larger.
constexpr int kTimeoutConnectedMs = 12000;
constexpr int kTimeoutInitialMs = 20000;
constexpr int kServiceProtocol = 1;
// Per-poll receive budget (review R3): far above what real play sends in a
// frame (a few commands per client, a snapshot or two), small enough that a
// flood cannot stall the frame copying it.
constexpr size_t kPollMessages = 512;
constexpr size_t kHostPollBytes = 1024 * 1024;
constexpr size_t kClientPollBytes = 8 * 1024 * 1024;
constexpr auto kServiceRetry = std::chrono::seconds(5);

void debugOutput(ESteamNetworkingSocketsDebugOutputType type, const char *message) {
    if (type <= k_ESteamNetworkingSocketsDebugOutputType_Warning) {
        warn(std::string("GNS: ") + message);
    } else {
        info(std::string("GNS: ") + message);
    }
}

void applySimulation() {
    auto *utils = SteamNetworkingUtils();
    const auto &s = g_simulation;
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketLag_Send, s.lagMs);
    utils->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLoss_Send, s.lossPercent);
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketJitter_Send_Avg, s.jitterMs);
    utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketJitter_Send_Max, s.jitterMs * 3);
    utils->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketJitter_Send_Pct, s.jitterMs > 0 ? 100.0f : 0.0f);
    if (s.lagMs || s.lossPercent > 0.0f || s.jitterMs) {
        info("Co-op: simulating " + std::to_string(s.lagMs) + " ms send lag, " + std::to_string(s.lossPercent) +
             "% send loss, " + std::to_string(s.jitterMs) + " ms jitter");
    }
}

bool acquireLibrary(std::string &error) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initCount == 0) {
        SteamNetworkingErrMsg message;
        if (!GameNetworkingSockets_Init(nullptr, message)) {
            error = std::string("GameNetworkingSockets_Init failed: ") + message;
            return false;
        }
        // REONE_GNS_VERBOSE=1: GameNetworkingSockets' own P2P and ICE trace in
        // engine.log (diagnosing a relay or NAT problem on a tester's machine).
        const char *verbose = std::getenv("REONE_GNS_VERBOSE");
        bool trace = verbose && *verbose && *verbose != '0';
        SteamNetworkingUtils()->SetDebugOutputFunction(
            trace ? k_ESteamNetworkingSocketsDebugOutputType_Verbose : k_ESteamNetworkingSocketsDebugOutputType_Warning, debugOutput);
        if (trace) {
            SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_LogLevel_P2PRendezvous,
                                                              k_ESteamNetworkingSocketsDebugOutputType_Verbose);
        }
        applySimulation();
    }
    ++g_initCount;
    return true;
}

void releaseLibrary() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (--g_initCount == 0) {
        GameNetworkingSockets_Kill();
    }
}

void onStatusChanged(SteamNetConnectionStatusChangedCallback_t *info);

std::string randomIdentity() {
    std::random_device device;
    std::uniform_int_distribution<uint32_t> dist;
    char text[32];
    std::snprintf(text, sizeof(text), "kc-%08x%08x", dist(device), dist(device));
    return text;
}

bool validIdentity(const std::string &id) {
    if (id.size() != 19 || id.compare(0, 3, "kc-") != 0) {
        return false;
    }
    return std::all_of(id.begin() + 3, id.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) || (c >= 'a' && c <= 'f');
    });
}

std::string text(const boost::json::object &message, const char *key, size_t limit = 256) {
    auto value = message.if_contains(key);
    if (!value || !value->is_string() || value->as_string().size() > limit) {
        return std::string();
    }
    return std::string(value->as_string());
}

int64_t integer(const boost::json::object &message, const char *key) {
    auto value = message.if_contains(key);
    if (!value || !value->is_int64()) {
        return -1;
    }
    return value->as_int64();
}

/** One peer's signalling: GameNetworkingSockets hands us blobs, the service carries them. */
class ServiceSignaling : public ISteamNetworkingConnectionSignaling {
public:
    ServiceSignaling(std::shared_ptr<SignalLink> link, int64_t peer) :
        _link(std::move(link)),
        _peer(peer) {
    }

    bool SendSignal(HSteamNetConnection, const SteamNetConnectionInfo_t &, const void *message, int size) override {
        return send(message, size);
    }

    bool send(const void *message, int size) {
        if (!_link || size <= 0 || size > 8 * 1024) {
            return false;
        }
        boost::json::object signal;
        signal["op"] = "signal";
        if (_peer > 0) {
            signal["peer"] = _peer;
        }
        signal["data"] = base64Encode(static_cast<const uint8_t *>(message), static_cast<size_t>(size));
        _link->send(signal);
        return true;
    }

    void Release() override { delete this; }

private:
    std::shared_ptr<SignalLink> _link;
    int64_t _peer; // 0: the host (a client's only peer)
};

class GnsTransport : public ITransport {
public:
    GnsTransport(bool host, int maxClients) :
        _host(host),
        _maxClients(maxClients) {
        _sockets = SteamNetworkingSockets();
        std::lock_guard<std::mutex> lock(g_mutex);
        g_instances.push_back(this);
    }

    ~GnsTransport() override {
        for (auto connection : _connections) {
            _sockets->CloseConnection(connection, 0, "shutdown", true);
        }
        if (_listen != k_HSteamListenSocket_Invalid) {
            _sockets->CloseListenSocket(_listen);
        }
        if (_listenP2P != k_HSteamListenSocket_Invalid) {
            _sockets->CloseListenSocket(_listenP2P);
        }
        if (_pollGroup != k_HSteamNetPollGroup_Invalid) {
            _sockets->DestroyPollGroup(_pollGroup);
        }
        if (_outgoing != k_HSteamNetConnection_Invalid && !_connections.count(_outgoing)) {
            _sockets->CloseConnection(_outgoing, 0, "shutdown", false);
        }
        if (_link) {
            _link->send(boost::json::object {{"op", "bye"}});
        }
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_instances.erase(std::remove(g_instances.begin(), g_instances.end(), this), g_instances.end());
        }
        _link.reset();
        releaseLibrary();
    }

    bool listen(uint16_t port, std::string &error) {
        SteamNetworkingIPAddr address;
        address.Clear();
        // Test runs set REONE_COOP_LOOPBACK=1: the host then listens on 127.0.0.1
        // only, which keeps local two-engine checks off the network (and out of
        // the OS firewall's prompts). Players never set it.
        const char *loopback = std::getenv("REONE_COOP_LOOPBACK");
        if (loopback && *loopback && std::string(loopback) != "0") {
            address.SetIPv4(0x7F000001u, port);
        }
        address.m_port = port;
        auto options = connectionOptions();
        _listen = _sockets->CreateListenSocketIP(address, static_cast<int>(options.size()), options.data());
        if (_listen == k_HSteamListenSocket_Invalid) {
            error = "cannot listen on UDP port " + std::to_string(port);
            return false;
        }
        if (!createPollGroup(error)) {
            return false;
        }
        _description = "host on UDP port " + std::to_string(port);
        return true;
    }

    bool connect(const std::string &address, std::string &error) {
        SteamNetworkingIPAddr remote;
        remote.Clear();
        if (!remote.ParseString(address.c_str()) || remote.m_port == 0) {
            error = "not an ip:port address: " + address;
            return false;
        }
        auto options = connectionOptions();
        _outgoing = _sockets->ConnectByIPAddress(remote, static_cast<int>(options.size()), options.data());
        if (_outgoing == k_HSteamNetConnection_Invalid) {
            error = "cannot connect to " + address;
            return false;
        }
        _description = "client of " + address;
        return true;
    }

    // Join codes

    bool startOnline(const OnlineConfig &online, const std::string &code, std::string &error) {
        if (!parseServiceAddress(online.service, _serviceHost, _servicePort)) {
            error = "not a join-code service address: " + online.service;
            return false;
        }
        _onlineConfig = online;
        _online.online = true;
        _online.service = _serviceHost + ":" + std::to_string(_servicePort);
        if (_identity.empty()) {
            resetIdentity();
        }
        if (_host) {
            if (!createPollGroup(error)) {
                return false;
            }
            _description = _description.empty() ? "host by join code" : _description + " and by join code";
        } else {
            _joinCode = code;
            _description = "client by join code " + displayJoinCode(code);
        }
        openServiceLink();
        return true;
    }

    /**
     * A fresh P2P identity for this process. ResetIdentity closes every
     * socket, listen socket and poll group, so it comes before any of them.
     */
    void resetIdentity() {
        _identity = randomIdentity();
        SteamNetworkingIdentity identity;
        identity.Clear();
        identity.SetGenericString(_identity.c_str());
        _sockets->ResetIdentity(&identity);
    }

    OnlineStatus onlineStatus() const override { return _online; }

    void reissueCode() override {
        if (_host && _link && _link->state() != SignalLink::State::Closed) {
            _link->send(boost::json::object {{"op", "reissue"}});
        }
    }

    void setJoinLocked(bool locked) override {
        if (!_host || locked == _locked) {
            return;
        }
        _locked = locked;
        if (_link) {
            _link->send(boost::json::object {{"op", "lock"}, {"on", locked}});
        }
    }

    std::string routeDescription(ConnectionId connection) const override {
        char status[2048];
        if (_sockets->GetDetailedConnectionStatus(connection, status, sizeof(status)) != 0) {
            return std::string();
        }
        return status;
    }

    /** A P2P request through the service: a signalling object when we take it, else null. */
    ISteamNetworkingConnectionSignaling *acceptSignalledConnection(const SteamNetworkingIdentity &identity, int64_t peer) {
        if (!_host || peer <= 0) {
            return nullptr; // a client accepts no incoming connections
        }
        // Only the identity the service vouched for when this peer resolved our code.
        auto it = _peers.find(peer);
        const char *claimed = identity.GetGenericString();
        if (it == _peers.end() || !claimed || it->second != claimed) {
            warn("Co-op: refusing a P2P request whose identity the service did not announce");
            return nullptr;
        }
        return new ServiceSignaling(_link, peer);
    }

    void sendRejection(int64_t peer, const void *message, int size) {
        if (peer > 0 && _link) {
            ServiceSignaling(_link, peer).send(message, size);
        }
    }

    bool isHost() const override { return _host; }

    void poll(std::vector<TransportEvent> &events) override {
        pollService(events);
        _sockets->RunCallbacks();
        for (auto &event : _pending) {
            events.push_back(std::move(event));
        }
        _pending.clear();

        // A bounded drain: at most budget() per poll, the rest stays queued in
        // the library; a client message over its limit is refused uncopied.
        SteamNetworkingMessage_t *messages[64];
        int count = 0;
        auto receive = [&](int max, std::vector<InboundMessage> &batch) {
            count = _host ? _sockets->ReceiveMessagesOnPollGroup(_pollGroup, messages, max)
                          : (_connections.count(_outgoing) ? _sockets->ReceiveMessagesOnConnection(_outgoing, messages, max) : 0);
            for (int i = 0; i < count; ++i) {
                batch.push_back({messages[i]->m_conn, static_cast<const uint8_t *>(messages[i]->m_pData),
                                 static_cast<size_t>(std::max(0, messages[i]->m_cbSize))});
            }
        };
        auto release = [&]() {
            for (int i = 0; i < count; ++i) {
                messages[i]->Release();
            }
            count = 0;
        };
        auto handle = [&](const InboundMessage &message) -> size_t {
            _stats.bytesReceived += static_cast<uint64_t>(message.size);
            ++_stats.messagesReceived;
            size_t limit = _host ? clientMessageLimit(message.size ? message.data[0] : 0) : kMaxHostMessageBytes;
            TransportEvent event;
            event.type = TransportEventType::Message;
            event.connection = message.connection;
            size_t copied = 0;
            if (message.size > limit) {
                event.oversizedBytes = message.size;
            } else {
                event.data.assign(message.data, message.data + message.size);
                copied = message.size;
            }
            if (!_connections.count(event.connection) && _accepted.count(event.connection)) {
                // A peer may send as soon as it is connected, before
                // our own Connected callback: the connection counts as
                // connected now, or its first reliable message (the
                // Hello) would be dropped here for good.
                _connections.insert(event.connection);
                TransportEvent connected;
                connected.type = TransportEventType::Connected;
                connected.connection = event.connection;
                connected.reason = "p2p (data before the connected callback)";
                events.push_back(std::move(connected));
            }
            if (_connections.count(event.connection)) {
                events.push_back(std::move(event));
            }
            return copied;
        };
        ReceiveBudget budget;
        budget.maxMessages = kPollMessages;
        budget.maxBytes = _host ? kHostPollBytes : kClientPollBytes;
        budget.maxMessageBytes = _host ? kMaxGuestCharacterBytes : kMaxHostMessageBytes;
        drainInbound(budget, receive, handle, release);
    }

    bool send(ConnectionId connection, const std::vector<uint8_t> &data, SendMode mode) override {
        if (!_connections.count(connection)) {
            return false;
        }
        int flags = mode == SendMode::Reliable ? (k_nSteamNetworkingSend_Reliable | k_nSteamNetworkingSend_NoNagle)
                                               : k_nSteamNetworkingSend_UnreliableNoNagle;
        auto result = _sockets->SendMessageToConnection(connection, data.data(), static_cast<uint32>(data.size()), flags, nullptr);
        if (result != k_EResultOK) {
            return false;
        }
        _stats.bytesSent += data.size();
        ++_stats.messagesSent;
        return true;
    }

    void close(ConnectionId connection, const std::string &reason) override {
        if (!_connections.erase(connection)) {
            return;
        }
        // Linger so a final reject or bye still reaches the peer.
        _sockets->CloseConnection(connection, 0, reason.c_str(), true);
        TransportEvent event;
        event.type = TransportEventType::Disconnected;
        event.connection = connection;
        event.reason = reason;
        _pending.push_back(std::move(event));
    }

    TransportStats stats() const override { return _stats; }

    int pingMs(ConnectionId connection) const override {
        SteamNetConnectionRealTimeStatus_t status;
        if (_sockets->GetConnectionRealTimeStatus(connection, &status, 0, nullptr) != k_EResultOK) {
            return -1;
        }
        return status.m_nPing;
    }

    std::string describe() const override { return _description; }

    void handleStatus(SteamNetConnectionStatusChangedCallback_t *info) {
        auto listen = info->m_info.m_hListenSocket;
        bool viaP2P = listen != k_HSteamListenSocket_Invalid && listen == _listenP2P;
        bool mine = _host ? (listen != k_HSteamListenSocket_Invalid && (listen == _listen || viaP2P))
                          : info->m_hConn == _outgoing;
        if (!mine) {
            return;
        }
        auto connection = info->m_hConn;
        switch (info->m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connecting:
            if (_host) {
                if (static_cast<int>(_connections.size()) >= _maxClients) {
                    _sockets->CloseConnection(connection, 0, "session full", false);
                    break;
                }
                auto accepted = _sockets->AcceptConnection(connection);
                if (accepted != k_EResultOK) {
                    warn("Co-op: cannot accept " + std::string(info->m_info.m_szConnectionDescription) + ": result " +
                         std::to_string(static_cast<int>(accepted)));
                    _sockets->CloseConnection(connection, 0, "accept failed", false);
                } else if (!_sockets->SetConnectionPollGroup(connection, _pollGroup)) {
                    warn("Co-op: cannot add a connection to the poll group");
                    _sockets->CloseConnection(connection, 0, "accept failed", false);
                } else {
                    _accepted.insert(connection);
                }
            }
            break;
        case k_ESteamNetworkingConnectionState_Connected: {
            _accepted.erase(connection);
            if (!_connections.insert(connection).second) {
                break; // announced already, when its first message came in
            }
            TransportEvent event;
            event.type = TransportEventType::Connected;
            event.connection = connection;
            if (viaP2P || (!_host && _online.online)) {
                event.reason = std::string("p2p ") + info->m_info.m_szConnectionDescription;
                if (!_host) {
                    _online.state = "connected";
                    _serviceDone = true;
                }
            } else {
                char address[SteamNetworkingIPAddr::k_cchMaxString];
                info->m_info.m_addrRemote.ToString(address, sizeof(address), true);
                event.reason = address;
            }
            _pending.push_back(std::move(event));
            break;
        }
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: {
            bool known = _connections.erase(connection) > 0;
            _accepted.erase(connection);
            reone::info(std::string("Co-op: connection ") + info->m_info.m_szConnectionDescription + " closed (" +
                 std::to_string(info->m_info.m_eEndReason) + "): " + info->m_info.m_szEndDebug);
            if (known || !_host) {
                TransportEvent event;
                event.type = TransportEventType::Disconnected;
                event.connection = connection;
                event.reason = info->m_info.m_szEndDebug;
                if (!_host && !known && _online.online && !_serviceDone) {
                    // The P2P attempt failed before it connected: ICE found no path.
                    _serviceDone = true;
                    _online.state = "failed";
                    _online.error = "no_route";
                    _online.detail = info->m_info.m_szEndDebug;
                    event.reason = std::string("service:no_route ") + info->m_info.m_szEndDebug;
                }
                _pending.push_back(std::move(event));
            }
            _sockets->CloseConnection(connection, 0, nullptr, false);
            if (connection == _outgoing) {
                _outgoing = k_HSteamNetConnection_Invalid;
            }
            break;
        }
        default:
            break;
        }
    }

private:
    bool _host;
    int _maxClients;
    ISteamNetworkingSockets *_sockets {nullptr};
    HSteamListenSocket _listen {k_HSteamListenSocket_Invalid};
    HSteamListenSocket _listenP2P {k_HSteamListenSocket_Invalid};
    HSteamNetPollGroup _pollGroup {k_HSteamNetPollGroup_Invalid};
    HSteamNetConnection _outgoing {k_HSteamNetConnection_Invalid};
    std::set<HSteamNetConnection> _connections;
    std::set<HSteamNetConnection> _accepted; // host: accepted, not yet connected
    std::vector<TransportEvent> _pending;
    TransportStats _stats;
    std::string _description;

    // Join codes
    OnlineConfig _onlineConfig;
    OnlineStatus _online;
    std::string _serviceHost;
    uint16_t _servicePort {0};
    std::string _identity;
    std::string _joinCode;
    std::shared_ptr<SignalLink> _link;
    std::chrono::steady_clock::time_point _linkOpened;
    std::map<int64_t, std::string> _peers; // host: peer number -> identity the service announced
    bool _haveIce {false};
    bool _locked {false};
    bool _serviceDone {false}; // client: the service part is over (connected or failed)

    bool createPollGroup(std::string &error) {
        if (_pollGroup != k_HSteamNetPollGroup_Invalid) {
            return true;
        }
        _pollGroup = _sockets->CreatePollGroup();
        if (_pollGroup == k_HSteamNetPollGroup_Invalid) {
            error = "cannot create a poll group";
            return false;
        }
        return true;
    }

    std::vector<SteamNetworkingConfigValue_t> connectionOptions() const {
        std::vector<SteamNetworkingConfigValue_t> options(3);
        options[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void *>(onStatusChanged));
        options[1].SetInt32(k_ESteamNetworkingConfig_TimeoutConnected, kTimeoutConnectedMs);
        options[2].SetInt32(k_ESteamNetworkingConfig_TimeoutInitial, kTimeoutInitialMs);
        return options;
    }

    void openServiceLink() {
        _link = std::make_shared<SignalLink>(_serviceHost, _servicePort);
        _linkOpened = std::chrono::steady_clock::now();
        boost::json::object hello;
        hello["v"] = kServiceProtocol;
        hello["id"] = _identity;
        if (_host) {
            hello["op"] = "host";
            hello["max"] = std::clamp(_maxClients, 1, 7);
            _online.state = "registering";
        } else {
            hello["op"] = "join";
            hello["code"] = _joinCode;
            _online.state = "resolving";
        }
        _link->send(hello);
        if (_host && _locked) {
            _link->send(boost::json::object {{"op", "lock"}, {"on", true}});
        }
        info("Co-op: join-code service " + _online.service + ": " + _online.state);
    }

    void applyIce(const boost::json::object &ice) {
        // The service's settings hold for every P2P connection this process
        // makes from now on (one co-op transport per process).
        std::string stun;
        if (auto list = ice.if_contains("stun"); list && list->is_array()) {
            for (const auto &entry : list->as_array()) {
                if (entry.is_string() && entry.as_string().size() < 256 && std::string_view(entry.as_string()).find(',') == std::string_view::npos) {
                    stun += (stun.empty() ? "" : ",") + std::string(entry.as_string());
                }
            }
        }
        std::string turn;
        std::string users;
        std::string passes;
        if (auto list = ice.if_contains("turn"); list && list->is_array()) {
            for (const auto &entry : list->as_array()) {
                if (!entry.is_object()) {
                    continue;
                }
                const auto &server = entry.as_object();
                auto url = text(server, "url");
                auto user = text(server, "user");
                auto pass = text(server, "pass");
                if (url.empty() || user.empty() || pass.empty() || url.find(',') != std::string::npos ||
                    user.find(',') != std::string::npos || pass.find(',') != std::string::npos) {
                    continue;
                }
                turn += (turn.empty() ? "" : ",") + url;
                users += (users.empty() ? "" : ",") + user;
                passes += (passes.empty() ? "" : ",") + pass;
            }
        }
        auto *utils = SteamNetworkingUtils();
        utils->SetGlobalConfigValueString(k_ESteamNetworkingConfig_P2P_STUN_ServerList, stun.c_str());
        utils->SetGlobalConfigValueString(k_ESteamNetworkingConfig_P2P_TURN_ServerList, turn.c_str());
        utils->SetGlobalConfigValueString(k_ESteamNetworkingConfig_P2P_TURN_UserList, users.c_str());
        utils->SetGlobalConfigValueString(k_ESteamNetworkingConfig_P2P_TURN_PassList, passes.c_str());
        int enable = _onlineConfig.relayOnly ? k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Relay
                                             : k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_All;
        utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable, enable);
        _haveIce = true;
        info("Co-op: ICE settings: STUN [" + stun + "], TURN [" + turn + "]" + (_onlineConfig.relayOnly ? ", relay only" : ""));
    }

    void pollService(std::vector<TransportEvent> &events) {
        if (!_link) {
            return;
        }
        std::vector<boost::json::object> messages;
        _link->receive(messages);
        for (const auto &message : messages) {
            handleServiceMessage(message, events);
        }
        if (!_link || _link->state() != SignalLink::State::Closed) {
            return;
        }
        auto why = _link->error();
        if (_host) {
            if (_online.state != "offline") {
                warn("Co-op: lost the join-code service (" + why + "); the code is gone, retrying");
                _online.state = "offline";
                _online.code.clear();
                _online.error = "unreachable";
                _online.detail = why;
                _peers.clear();
            }
            if (std::chrono::steady_clock::now() - _linkOpened >= kServiceRetry) {
                openServiceLink();
            }
        } else if (!_serviceDone) {
            // Before the P2P connection exists the service is the only way in.
            failClient(events, "unreachable", why);
        } else {
            _link.reset(); // connected already: the link is no longer needed
        }
    }

    void failClient(std::vector<TransportEvent> &events, const std::string &error, const std::string &detail) {
        if (_serviceDone) {
            return;
        }
        _serviceDone = true;
        _online.state = "failed";
        _online.error = error;
        _online.detail = detail;
        TransportEvent event;
        event.type = TransportEventType::Disconnected;
        event.connection = 0;
        event.reason = "service:" + error + (detail.empty() ? "" : " " + detail);
        events.push_back(std::move(event));
        if (_outgoing != k_HSteamNetConnection_Invalid) {
            _sockets->CloseConnection(_outgoing, 0, "service failed", false);
            _outgoing = k_HSteamNetConnection_Invalid;
        }
    }

    void handleServiceMessage(const boost::json::object &message, std::vector<TransportEvent> &events) {
        auto op = text(message, "op", 32);
        if (op == "pong") {
            return;
        }
        if (op == "error") {
            auto error = text(message, "error", 32);
            auto detail = text(message, "detail", 200);
            warn("Co-op: join-code service: " + error + (detail.empty() ? "" : " (" + detail + ")"));
            if (_host) {
                _online.error = error;
                _online.detail = detail;
                if (_online.state == "registering") {
                    _online.state = "offline";
                }
            } else {
                failClient(events, error.empty() ? "service_error" : error, detail);
            }
            return;
        }
        if (_host) {
            handleHostServiceMessage(op, message);
            return;
        }
        if (op == "joined") {
            auto host = text(message, "host", 32);
            auto ice = message.if_contains("ice");
            if (!validIdentity(host) || !ice || !ice->is_object() || _outgoing != k_HSteamNetConnection_Invalid) {
                failClient(events, "service_error", "malformed answer");
                return;
            }
            applyIce(ice->as_object());
            SteamNetworkingIdentity remote;
            remote.Clear();
            remote.SetGenericString(host.c_str());
            auto options = connectionOptions();
            auto *signaling = new ServiceSignaling(_link, 0);
            _outgoing = _sockets->ConnectP2PCustomSignaling(signaling, &remote, 0, static_cast<int>(options.size()), options.data());
            if (_outgoing == k_HSteamNetConnection_Invalid) {
                failClient(events, "connect_failed", "cannot start the P2P connection");
                return;
            }
            _online.state = "connecting";
            info("Co-op: code resolved; connecting to the host P2P");
        } else if (op == "signal") {
            receiveSignal(message, 0);
        } else if (op == "host_left") {
            if (!_connections.count(_outgoing)) {
                failClient(events, "host_left", "the host closed the session");
            }
        }
    }

    void handleHostServiceMessage(const std::string &op, const boost::json::object &message) {
        if (op == "hosted") {
            auto code = normalizeJoinCode(text(message, "code", 16));
            auto ice = message.if_contains("ice");
            if (code.empty() || !ice || !ice->is_object()) {
                warn("Co-op: join-code service sent a malformed code");
                return;
            }
            if (!_haveIce) {
                applyIce(ice->as_object());
            }
            if (_listenP2P == k_HSteamListenSocket_Invalid) {
                auto options = connectionOptions();
                _listenP2P = _sockets->CreateListenSocketP2P(0, static_cast<int>(options.size()), options.data());
                if (_listenP2P == k_HSteamListenSocket_Invalid) {
                    warn("Co-op: cannot open the P2P listen socket");
                }
            }
            _online.state = "hosted";
            _online.code = code;
            _online.error.clear();
            _online.detail.clear();
            info("Co-op: join code " + displayJoinCode(code));
        } else if (op == "expired") {
            _online.state = "expired";
            _online.code.clear();
            info("Co-op: the join code expired; reissue it for a new one");
        } else if (op == "peer") {
            auto peer = integer(message, "peer");
            auto id = text(message, "id", 32);
            if (peer > 0 && validIdentity(id) && _peers.size() < 64) {
                _peers[peer] = id;
            }
        } else if (op == "peer_left") {
            _peers.erase(integer(message, "peer"));
        } else if (op == "signal") {
            auto peer = integer(message, "peer");
            if (peer > 0 && _peers.count(peer)) {
                receiveSignal(message, peer);
            }
        }
    }

    void receiveSignal(const boost::json::object &message, int64_t peer) {
        std::vector<uint8_t> data;
        auto encoded = text(message, "data", 12 * 1024);
        if (encoded.empty() || !base64Decode(encoded, data) || data.empty()) {
            return;
        }
        struct Context : ISteamNetworkingSignalingRecvContext {
            GnsTransport *transport {nullptr};
            int64_t peer {0};

            ISteamNetworkingConnectionSignaling *OnConnectRequest(HSteamNetConnection, const SteamNetworkingIdentity &identity, int) override {
                return transport->acceptSignalledConnection(identity, peer);
            }

            void SendRejectionSignal(const SteamNetworkingIdentity &, const void *message, int size) override {
                transport->sendRejection(peer, message, size);
            }
        } context;
        context.transport = this;
        context.peer = peer;
        _sockets->ReceivedP2PCustomSignal(data.data(), static_cast<int>(data.size()), &context);
    }
};

void onStatusChanged(SteamNetConnectionStatusChangedCallback_t *info) {
    // Callbacks run inside RunCallbacks on the polling thread, so the
    // instance list cannot change under us.
    for (auto *instance : g_instances) {
        instance->handleStatus(info);
    }
}

} // namespace

bool socketTransportAvailable() {
    return true;
}

void setNetworkSimulation(const NetworkSimulation &simulation) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_simulation = simulation;
    if (g_initCount > 0) {
        applySimulation();
    }
}

std::unique_ptr<ITransport> createHostTransport(uint16_t port, int maxClients, std::string &error) {
    if (!acquireLibrary(error)) {
        return nullptr;
    }
    auto transport = std::make_unique<GnsTransport>(true, maxClients);
    if (!transport->listen(port, error)) {
        return nullptr;
    }
    return transport;
}

std::unique_ptr<ITransport> createOnlineHostTransport(uint16_t port, int maxClients, const OnlineConfig &online, std::string &error) {
    if (!acquireLibrary(error)) {
        return nullptr;
    }
    auto transport = std::make_unique<GnsTransport>(true, maxClients);
    transport->resetIdentity();
    if (port > 0 && !transport->listen(port, error)) {
        return nullptr;
    }
    if (!transport->startOnline(online, std::string(), error)) {
        return nullptr;
    }
    return transport;
}

std::unique_ptr<ITransport> createClientTransport(const std::string &address, std::string &error) {
    if (!acquireLibrary(error)) {
        return nullptr;
    }
    auto transport = std::make_unique<GnsTransport>(false, 1);
    if (!transport->connect(address, error)) {
        return nullptr;
    }
    return transport;
}

std::unique_ptr<ITransport> createCodeClientTransport(const std::string &code, const OnlineConfig &online, std::string &error) {
    auto canonical = normalizeJoinCode(code);
    if (canonical.empty()) {
        error = "not a join code: " + code;
        return nullptr;
    }
    if (!acquireLibrary(error)) {
        return nullptr;
    }
    auto transport = std::make_unique<GnsTransport>(false, 1);
    if (!transport->startOnline(online, canonical, error)) {
        return nullptr;
    }
    return transport;
}

} // namespace net

} // namespace reone

#else

namespace reone {

namespace net {

namespace {
const char *kNoTransport = "this build has no network transport (configure with REONE_NETWORK=ON and GameNetworkingSockets installed)";
}

bool socketTransportAvailable() {
    return false;
}

void setNetworkSimulation(const NetworkSimulation &) {
}

std::unique_ptr<ITransport> createHostTransport(uint16_t, int, std::string &error) {
    error = kNoTransport;
    return nullptr;
}

std::unique_ptr<ITransport> createOnlineHostTransport(uint16_t, int, const OnlineConfig &, std::string &error) {
    error = kNoTransport;
    return nullptr;
}

std::unique_ptr<ITransport> createClientTransport(const std::string &, std::string &error) {
    error = kNoTransport;
    return nullptr;
}

std::unique_ptr<ITransport> createCodeClientTransport(const std::string &, const OnlineConfig &, std::string &error) {
    error = kNoTransport;
    return nullptr;
}

} // namespace net

} // namespace reone

#endif
