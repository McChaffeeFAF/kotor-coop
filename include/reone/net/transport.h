/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace reone {

namespace net {

using ConnectionId = uint32_t;

enum class TransportEventType {
    Connected,
    Disconnected,
    Message
};

struct TransportEvent {
    TransportEventType type {TransportEventType::Message};
    ConnectionId connection {0};
    std::vector<uint8_t> data;
    std::string reason;
    size_t oversizedBytes {0}; // a message refused for its size before it was copied (data stays empty)
};

/**
 * What one poll may take off the network (review R3). Whatever is left
 * stays queued in the network library for the next poll, so a peer that
 * sends faster than the host drains cannot hold the frame.
 */
struct ReceiveBudget {
    size_t maxMessages {512};
    size_t maxBytes {1024 * 1024};
    size_t maxMessageBytes {1024 * 1024}; // the largest message that is copied rather than refused
};

/** A message as the network library holds it, before anything is copied. */
struct InboundMessage {
    ConnectionId connection {0};
    const uint8_t *data {nullptr};
    size_t size {0};
};

struct DrainStats {
    size_t messages {0};
    size_t bytes {0};
    bool budgetSpent {false}; // stopped on the budget: more may be waiting
};

/**
 * Take one poll's worth of messages. receive(max, batch) moves at most max
 * messages off the network into batch; handle(message) deals with each and
 * returns the bytes it copied (0 for a refused one); release() follows each
 * batch. A batch is never larger than the bytes left in the budget divided by
 * maxMessageBytes, so the byte budget holds without taking a message back
 * (only a budget smaller than one message is exceeded, by that message).
 */
template <class Receive, class Handle, class Release>
DrainStats drainInbound(const ReceiveBudget &budget, Receive &&receive, Handle &&handle, Release &&release) {
    constexpr size_t kBatch = 64;
    DrainStats stats;
    std::vector<InboundMessage> batch;
    size_t largest = std::max<size_t>(1, budget.maxMessageBytes);
    while (stats.messages < budget.maxMessages && stats.bytes < budget.maxBytes) {
        size_t want = std::min({kBatch, budget.maxMessages - stats.messages, (budget.maxBytes - stats.bytes) / largest});
        if (want == 0) {
            if (stats.messages > 0) {
                break; // the next message might not fit the bytes left
            }
            want = 1; // a budget below one message still takes one
        }
        batch.clear();
        receive(static_cast<int>(want), batch);
        for (const auto &message : batch) {
            stats.bytes += handle(message);
            ++stats.messages;
        }
        release();
        if (batch.size() < want) {
            return stats; // the queue is empty
        }
    }
    stats.budgetSpent = true;
    return stats;
}

enum class SendMode {
    Reliable,  // ordered, retransmitted
    Unreliable // may drop; for state that the next message supersedes
};

/**
 * The join-code side of a transport (design note coop_online): the host's
 * current code, or where a client is in resolving one. Empty for direct
 * ip:port sessions.
 */
struct OnlineStatus {
    bool online {false};
    // host: registering, hosted, expired, offline (the service link dropped; retrying)
    // client: resolving, connecting, connected, failed
    std::string state;
    std::string code;   // host: the current code (canonical, no dash)
    std::string error;  // bad_code, host_full, rate_limited, version, unreachable, server_full, ...
    std::string detail;
    std::string service; // host:port of the join-code service
};

/** What a join-code session needs besides the code. */
struct OnlineConfig {
    std::string service;     // --coop-server, "host[:port]"
    bool relayOnly {false};  // ICE: share and use relay candidates only (TURN-forced tests)
};

/**
 * Simulated network trouble for tests (GameNetworkingSockets fake lag and
 * loss on everything this process sends). All zero: off.
 */
struct NetworkSimulation {
    int lagMs {0};
    float lossPercent {0.0f};
    int jitterMs {0};
};

/** Applies to every GameNetworkingSockets transport in this process, now and later. */
void setNetworkSimulation(const NetworkSimulation &simulation);

struct TransportStats {
    uint64_t bytesSent {0};
    uint64_t bytesReceived {0};
    uint64_t messagesSent {0};
    uint64_t messagesReceived {0};
    int pingMs {-1};
};

/**
 * Message transport under the co-op session.
 *
 * A host transport listens and reports each client as a connection; a client
 * transport has exactly one connection, to the host. poll() is called once a
 * frame and never blocks.
 */
class ITransport {
public:
    virtual ~ITransport() = default;

    virtual bool isHost() const = 0;
    virtual void poll(std::vector<TransportEvent> &events) = 0;
    virtual bool send(ConnectionId connection, const std::vector<uint8_t> &data, SendMode mode) = 0;
    virtual void close(ConnectionId connection, const std::string &reason) = 0;
    virtual TransportStats stats() const = 0;
    virtual int pingMs(ConnectionId connection) const { return -1; }
    virtual std::string describe() const = 0;

    // Join codes (online transports only)

    virtual OnlineStatus onlineStatus() const { return OnlineStatus(); }
    /** Host: a new code; the old one stops working at once. */
    virtual void reissueCode() {}
    /** Host: a full session refuses joins at the service, before any connection. */
    virtual void setJoinLocked(bool locked) {}
    /** How a connection is carried (direct, relayed), for diagnostics. */
    virtual std::string routeDescription(ConnectionId connection) const { return std::string(); }
};

/** True when the engine was built with GameNetworkingSockets. */
bool socketTransportAvailable();

/**
 * Listen for co-op clients on every interface at this UDP port.
 * Returns null and sets error when the transport is unavailable or the port
 * cannot be bound. maxClients bounds accepted connections.
 */
std::unique_ptr<ITransport> createHostTransport(uint16_t port, int maxClients, std::string &error);

/**
 * Host a join-code session: register with the service for a code and accept
 * P2P connections signalled through it (ICE, with a TURN relay fallback).
 * A port above 0 also listens for direct ip:port joins.
 */
std::unique_ptr<ITransport> createOnlineHostTransport(uint16_t port, int maxClients, const OnlineConfig &online,
                                                      std::string &error);

/** Connect to a host at "ip:port". */
std::unique_ptr<ITransport> createClientTransport(const std::string &address, std::string &error);

/**
 * Join by code: resolve it at the service, then connect P2P. Failures to
 * resolve (bad code, host full, service unreachable) arrive as a
 * Disconnected event whose reason starts with "service:".
 */
std::unique_ptr<ITransport> createCodeClientTransport(const std::string &code, const OnlineConfig &online,
                                                      std::string &error);

/**
 * Two in-memory transports wired to each other: first is the host, second a
 * client. Messages are delivered on the receiver's next poll, in order. For
 * tests, and for a same-process co-op harness.
 */
std::pair<std::unique_ptr<ITransport>, std::unique_ptr<ITransport>> createLoopbackPair();

} // namespace net

} // namespace reone
