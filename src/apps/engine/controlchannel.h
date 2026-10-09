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

#pragma once

#include "controlprotocol.h"

namespace reone {

/**
 * Localhost command channel. A background thread listens on 127.0.0.1,
 * accepts one client at a time and queues each received line as a command.
 * The main thread polls commands and sends replies; commands are never
 * executed on the network thread.
 */
class ControlChannel : boost::noncopyable {
public:
    ~ControlChannel() {
        stop();
    }

    void start(int port);
    void stop();

    std::optional<ControlRequest> poll() {
        return _queue.pop();
    }

    /**
     * Sends one reply line to the connected client, if any. Called from the
     * main thread.
     */
    void send(const std::string &line);

private:
    using Socket = intptr_t;

    std::thread _thread;
    std::atomic_bool _running {false};
    ControlQueue _queue;

    Socket _listenSocket {-1};
    std::mutex _clientMutex;
    Socket _clientSocket {-1};

    void run();
    void serveClient();
    void closeClient();
};

} // namespace reone
