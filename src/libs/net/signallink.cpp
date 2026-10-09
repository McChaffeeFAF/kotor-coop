/* Copyright (c) 2026 The reone project contributors */

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#undef max
#undef min
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "reone/net/signallink.h"

#include <chrono>

namespace reone {

namespace net {

namespace {

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kNoSocket = INVALID_SOCKET;
void closeSocket(Socket s) { closesocket(s); }
bool setNonBlocking(Socket s) {
    u_long on = 1;
    return ioctlsocket(s, FIONBIO, &on) == 0;
}
int lastError() { return WSAGetLastError(); }
bool inProgress(int error) { return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS; }
bool wouldBlock(int error) { return error == WSAEWOULDBLOCK; }
#else
using Socket = int;
constexpr Socket kNoSocket = -1;
void closeSocket(Socket s) { ::close(s); }
bool setNonBlocking(Socket s) {
    int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}
int lastError() { return errno; }
bool inProgress(int error) { return error == EINPROGRESS || error == EWOULDBLOCK; }
bool wouldBlock(int error) { return error == EWOULDBLOCK || error == EAGAIN; }
#endif

constexpr auto kConnectTimeout = std::chrono::seconds(8);
constexpr auto kPingInterval = std::chrono::seconds(20);
constexpr size_t kMaxOutbox = 256;

timeval shortWait() {
    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 50 * 1000;
    return tv;
}

} // namespace

SignalLink::SignalLink(std::string host, uint16_t port) :
    _host(std::move(host)),
    _port(port) {
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
    _thread = std::thread([this]() { run(); });
}

SignalLink::~SignalLink() {
    _stop = true;
    if (_thread.joinable()) {
        _thread.join();
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

void SignalLink::send(const boost::json::object &message) {
    std::string line = boost::json::serialize(message);
    line.push_back('\n');
    std::lock_guard<std::mutex> lock(_mutex);
    // Best effort, as the signals themselves are: drop the oldest when backed up.
    while (_outbox.size() >= kMaxOutbox) {
        _outbox.pop_front();
    }
    _outbox.push_back(std::move(line));
}

void SignalLink::receive(std::vector<boost::json::object> &out) {
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto &message : _inbox) {
        out.push_back(std::move(message));
    }
    _inbox.clear();
}

std::string SignalLink::error() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _error;
}

void SignalLink::fail(const std::string &error) {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_error.empty()) {
            _error = error;
        }
    }
    _state = State::Closed;
}

void SignalLink::run() {
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo *found = nullptr;
    if (getaddrinfo(_host.c_str(), std::to_string(_port).c_str(), &hints, &found) != 0 || !found) {
        fail("unreachable: cannot resolve " + _host);
        return;
    }
    Socket sock = kNoSocket;
    auto deadline = std::chrono::steady_clock::now() + kConnectTimeout;
    for (addrinfo *ai = found; ai && sock == kNoSocket && !_stop; ai = ai->ai_next) {
        Socket s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == kNoSocket) {
            continue;
        }
        if (!setNonBlocking(s)) {
            closeSocket(s);
            continue;
        }
        int rc = ::connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen));
        if (rc != 0 && !inProgress(lastError())) {
            closeSocket(s);
            continue;
        }
        bool connected = rc == 0;
        while (!connected && !_stop && std::chrono::steady_clock::now() < deadline) {
            fd_set writable;
            fd_set failed;
            FD_ZERO(&writable);
            FD_ZERO(&failed);
            FD_SET(s, &writable);
            FD_SET(s, &failed);
            timeval tv = shortWait();
            int ready = ::select(static_cast<int>(s) + 1, nullptr, &writable, &failed, &tv);
            if (ready < 0) {
                break;
            }
            if (ready > 0) {
                int error = 0;
                socklen_t length = sizeof(error);
                getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &length);
                connected = error == 0 && FD_ISSET(s, &writable);
                break;
            }
        }
        if (connected) {
            sock = s;
        } else {
            closeSocket(s);
        }
    }
    freeaddrinfo(found);
    if (sock == kNoSocket) {
        fail("unreachable: no answer from " + describe());
        return;
    }
    _state = State::Open;

    std::string incoming;
    std::string pending; // the part of the current outgoing line not yet written
    auto lastSend = std::chrono::steady_clock::now();
    char buffer[4096];
    while (!_stop) {
        if (pending.empty()) {
            std::lock_guard<std::mutex> lock(_mutex);
            while (!_outbox.empty() && pending.size() < 64 * 1024) {
                pending += _outbox.front();
                _outbox.pop_front();
            }
        }
        auto now = std::chrono::steady_clock::now();
        if (pending.empty() && now - lastSend >= kPingInterval) {
            pending = "{\"op\":\"ping\"}\n";
        }
        fd_set readable;
        fd_set writable;
        FD_ZERO(&readable);
        FD_ZERO(&writable);
        FD_SET(sock, &readable);
        if (!pending.empty()) {
            FD_SET(sock, &writable);
        }
        timeval tv = shortWait();
        int ready = ::select(static_cast<int>(sock) + 1, &readable, pending.empty() ? nullptr : &writable, nullptr, &tv);
        if (ready < 0) {
            fail("closed: select failed");
            break;
        }
        if (!pending.empty() && FD_ISSET(sock, &writable)) {
            int sent = ::send(sock, pending.data(), static_cast<int>(pending.size()), 0);
            if (sent < 0 && !wouldBlock(lastError())) {
                fail("closed: send failed");
                break;
            }
            if (sent > 0) {
                pending.erase(0, static_cast<size_t>(sent));
                lastSend = std::chrono::steady_clock::now();
            }
        }
        if (FD_ISSET(sock, &readable)) {
            int got = ::recv(sock, buffer, sizeof(buffer), 0);
            if (got == 0) {
                fail("closed: the service closed the link");
                break;
            }
            if (got < 0) {
                if (wouldBlock(lastError())) {
                    continue;
                }
                fail("closed: receive failed");
                break;
            }
            incoming.append(buffer, static_cast<size_t>(got));
            size_t start = 0;
            for (;;) {
                auto end = incoming.find('\n', start);
                if (end == std::string::npos) {
                    break;
                }
                std::string line = incoming.substr(start, end - start);
                start = end + 1;
                boost::system::error_code ec;
                auto value = boost::json::parse(line, ec);
                if (ec || !value.is_object()) {
                    fail("protocol: the service sent something that is not a JSON object");
                    break;
                }
                std::lock_guard<std::mutex> lock(_mutex);
                if (_inbox.size() < 1024) {
                    _inbox.push_back(std::move(value.as_object()));
                }
            }
            if (_state == State::Closed) {
                break;
            }
            incoming.erase(0, start);
            if (incoming.size() > kMaxLineBytes) {
                fail("protocol: a line from the service is too long");
                break;
            }
        }
    }
    closeSocket(sock);
    if (_state != State::Closed) {
        _state = State::Closed;
    }
}

} // namespace net

} // namespace reone
