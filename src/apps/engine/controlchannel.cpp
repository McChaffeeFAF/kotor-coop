/*
 * Copyright (c) 2020-2023 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#undef max
#undef min
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "controlchannel.h"

#include "reone/system/logutil.h"

namespace reone {

namespace {

constexpr intptr_t kNoSocket = -1;
constexpr long kPollIntervalUsec = 100 * 1000;

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr int kSendFlags = 0;
constexpr int kShutdownSend = SD_SEND;
void closeNative(intptr_t s) { closesocket(static_cast<SOCKET>(s)); }
#else
using NativeSocket = int;
constexpr int kSendFlags = MSG_NOSIGNAL;
constexpr int kShutdownSend = SHUT_WR;
void closeNative(intptr_t s) { close(static_cast<int>(s)); }
#endif

NativeSocket native(intptr_t s) { return static_cast<NativeSocket>(s); }

// True when the socket has data (or a pending connection) before the poll
// interval expires. Lets the thread notice stop() without blocking forever.
bool waitReadable(intptr_t s) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(native(s), &set);
    timeval timeout {0, kPollIntervalUsec};
    return select(static_cast<int>(native(s)) + 1, &set, nullptr, nullptr, &timeout) > 0;
}

} // namespace

void ControlChannel::start(int port) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
#endif
    auto s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == static_cast<NativeSocket>(kNoSocket)) {
        throw std::runtime_error("Control channel: socket() failed");
    }
    int reuse = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof(reuse));

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1 only
    if (bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 || listen(s, 1) != 0) {
        closeNative(static_cast<intptr_t>(s));
        throw std::runtime_error("Control channel: cannot listen on 127.0.0.1:" + std::to_string(port));
    }
    _listenSocket = static_cast<intptr_t>(s);
    _running = true;
    _thread = std::thread(&ControlChannel::run, this);
    info("Control channel listening on 127.0.0.1:" + std::to_string(port));
}

void ControlChannel::stop() {
    if (!_running) {
        return;
    }
    _running = false;
    if (_thread.joinable()) {
        _thread.join();
    }
    closeClient();
    closeNative(_listenSocket);
    _listenSocket = kNoSocket;
#ifdef _WIN32
    WSACleanup();
#endif
}

void ControlChannel::send(const std::string &line) {
    std::lock_guard<std::mutex> lock(_clientMutex);
    if (_clientSocket == kNoSocket) {
        return;
    }
    std::string data = line + "\n";
    const char *ptr = data.data();
    size_t left = data.size();
    while (left > 0) {
        int sent = ::send(native(_clientSocket), ptr, static_cast<int>(left), kSendFlags);
        if (sent <= 0) {
            break; // client went away; its reader side will notice and close
        }
        ptr += sent;
        left -= static_cast<size_t>(sent);
    }
}

void ControlChannel::run() {
    while (_running) {
        if (!waitReadable(_listenSocket)) {
            continue;
        }
        auto client = accept(native(_listenSocket), nullptr, nullptr);
        if (client == static_cast<NativeSocket>(kNoSocket)) {
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(_clientMutex);
            _clientSocket = static_cast<intptr_t>(client);
        }
        serveClient();
        closeClient();
    }
}

void ControlChannel::serveClient() {
    ControlLineSplitter splitter;
    char buf[4096];
    while (_running) {
        if (!waitReadable(_clientSocket)) {
            continue;
        }
        int received = recv(native(_clientSocket), buf, sizeof(buf), 0);
        if (received <= 0) {
            return; // disconnected
        }
        splitter.append(std::string_view(buf, static_cast<size_t>(received)));
        while (auto line = splitter.next()) {
            _queue.push(std::move(*line));
        }
    }
}

void ControlChannel::closeClient() {
    std::lock_guard<std::mutex> lock(_clientMutex);
    if (_clientSocket == kNoSocket) {
        return;
    }
    shutdown(native(_clientSocket), kShutdownSend);
    closeNative(_clientSocket);
    _clientSocket = kNoSocket;
}

} // namespace reone
