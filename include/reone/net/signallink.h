/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/json.hpp>

namespace reone {

namespace net {

/**
 * The link to the join-code service (tools/coopserver): a TCP connection
 * carrying one JSON object per line. A background thread resolves, connects,
 * reads and writes; the game polls receive() once a frame and never blocks.
 * send() may be called from any thread (GameNetworkingSockets signals from
 * its own). Every line received is size-checked and parsed before it is
 * handed over; anything else closes the link.
 */
class SignalLink {
public:
    enum class State {
        Connecting,
        Open,
        Closed
    };

    static constexpr size_t kMaxLineBytes = 16 * 1024;

    SignalLink(std::string host, uint16_t port);
    ~SignalLink();

    SignalLink(const SignalLink &) = delete;
    SignalLink &operator=(const SignalLink &) = delete;

    void send(const boost::json::object &message);
    /** Messages received since the last call, in order. */
    void receive(std::vector<boost::json::object> &out);

    State state() const { return _state.load(); }
    /** Why the link closed ("unreachable", "closed", "protocol"), with detail. */
    std::string error() const;
    std::string describe() const { return _host + ":" + std::to_string(_port); }

private:
    std::string _host;
    uint16_t _port;
    std::atomic<State> _state {State::Connecting};
    std::atomic<bool> _stop {false};
    mutable std::mutex _mutex;
    std::deque<std::string> _outbox;
    std::vector<boost::json::object> _inbox;
    std::string _error;
    std::thread _thread;

    void run();
    void fail(const std::string &error);
};

} // namespace net

} // namespace reone
