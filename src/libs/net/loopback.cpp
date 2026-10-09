/* Copyright (c) 2026 The reone project contributors */

#include "reone/net/transport.h"

namespace reone {

namespace net {

namespace {

struct LoopbackLink {
    std::deque<TransportEvent> toHost;
    std::deque<TransportEvent> toClient;
    bool open {true};
};

constexpr ConnectionId kLoopbackConnection = 1;

class LoopbackTransport : public ITransport {
public:
    LoopbackTransport(std::shared_ptr<LoopbackLink> link, bool host) :
        _link(std::move(link)),
        _host(host) {
        TransportEvent connected;
        connected.type = TransportEventType::Connected;
        connected.connection = kLoopbackConnection;
        inbox().push_back(connected);
    }

    ~LoopbackTransport() override {
        if (_link->open) {
            _link->open = false;
            TransportEvent gone;
            gone.type = TransportEventType::Disconnected;
            gone.connection = kLoopbackConnection;
            gone.reason = "peer destroyed";
            outbox().push_back(gone);
        }
    }

    bool isHost() const override { return _host; }

    void poll(std::vector<TransportEvent> &events) override {
        auto &in = inbox();
        while (!in.empty()) {
            auto &event = in.front();
            if (event.type == TransportEventType::Message) {
                _stats.bytesReceived += event.data.size();
                ++_stats.messagesReceived;
            }
            events.push_back(std::move(event));
            in.pop_front();
        }
    }

    bool send(ConnectionId connection, const std::vector<uint8_t> &data, SendMode) override {
        if (!_link->open || connection != kLoopbackConnection) {
            return false;
        }
        TransportEvent event;
        event.type = TransportEventType::Message;
        event.connection = kLoopbackConnection;
        event.data = data;
        outbox().push_back(std::move(event));
        _stats.bytesSent += data.size();
        ++_stats.messagesSent;
        return true;
    }

    void close(ConnectionId connection, const std::string &reason) override {
        if (!_link->open || connection != kLoopbackConnection) {
            return;
        }
        _link->open = false;
        TransportEvent gone;
        gone.type = TransportEventType::Disconnected;
        gone.connection = kLoopbackConnection;
        gone.reason = reason;
        outbox().push_back(gone);
        TransportEvent self = gone;
        inbox().push_back(self);
    }

    TransportStats stats() const override { return _stats; }

    std::string describe() const override { return _host ? "loopback host" : "loopback client"; }

private:
    std::shared_ptr<LoopbackLink> _link;
    bool _host;
    TransportStats _stats;

    std::deque<TransportEvent> &inbox() { return _host ? _link->toHost : _link->toClient; }
    std::deque<TransportEvent> &outbox() { return _host ? _link->toClient : _link->toHost; }
};

} // namespace

std::pair<std::unique_ptr<ITransport>, std::unique_ptr<ITransport>> createLoopbackPair() {
    auto link = std::make_shared<LoopbackLink>();
    std::unique_ptr<ITransport> host = std::make_unique<LoopbackTransport>(link, true);
    std::unique_ptr<ITransport> client = std::make_unique<LoopbackTransport>(link, false);
    return {std::move(host), std::move(client)};
}

} // namespace net

} // namespace reone
