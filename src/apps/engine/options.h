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

#include "reone/audio/options.h"
#include "reone/game/dedicated.h"
#include "reone/game/options.h"
#include "reone/graphics/options.h"
#include "reone/system/types.h"

namespace reone {

struct Options {
    struct Logging {
        LogSeverity severity {LogSeverity::Info};
        std::set<LogChannel> channels {LogChannel::Global, LogChannel::Render};
    };

    game::GameOptions game;
    graphics::GraphicsOptions graphics;
    audio::AudioOptions audio;

    Logging logging;

    /**
     * Execute console commands from a file at startup.
     */
    std::string commandsFile;

    /**
     * Accept console commands on 127.0.0.1 at this TCP port. 0 disables.
     */
    int controlPort {0};

    /**
     * Co-op networking. Off unless --host or --join is given.
     */
    struct Coop {
        int hostPort {0};        // --host <port>
        std::string joinAddress; // --join <ip:port>
        std::string mode {"story"};
        std::string name;
        std::string slot;
        // Host options (co-op design notes)
        bool hostOnlyTransitions {false}; // --coop-host-only-transitions
        bool hostOnlyTrading {false};     // --coop-host-only-trading
        bool noPause {false};             // --coop-no-pause
        float pauseCooldown {0.0f};       // --coop-pause-cooldown <seconds>
        float enemyScaling {1.0f};        // --coop-enemy-scaling <strength>, 0 off
        // Online play (design note coop_online)
        std::string server;   // --coop-server <host[:port]>: the join-code service
        std::string joinCode; // --join <code> (joinAddress stays empty)
        bool relayOnly {false}; // --coop-relay-only: ICE through the TURN relay only
        int askJoin {-1};       // --coop-ask-join 0|1; -1: ask unless headless
        int simLagMs {0};       // --coop-sim-lag <ms>
        float simLossPercent {0.0f}; // --coop-sim-loss <percent>
        int simJitterMs {0};    // --coop-sim-jitter <ms>
        std::string recordPath; // --coop-record <file>: the tester's once-a-second state log
        std::string password;   // --coop-password: a host requires it, a guest presents it
    };
    Coop coop;

    /**
     * The dedicated host (--dedicated, docs/dedicated_host.md): Free Mode with
     * no local player. Implies headless; the port is --host's, else 27015.
     */
    game::DedicatedSettings dedicated;

    /** The running engine executable; its hash is the co-op build identity. */
    std::filesystem::path executablePath;

    /**
     * Seconds each headless frame claims to last. Headless runs are not paced
     * by the wall clock; this sets how many frames a simulation tick spans.
     */
    float headlessFrameTime {1.0f / 60.0f};

    /**
     * Keep running while the window is not focused, instead of idling. For
     * windowed scripted runs (frame-time measurement) behind other windows.
     */
    bool runUnfocused {false};

    std::unique_ptr<game::OptionsView> toView() {
        return std::make_unique<game::OptionsView>(game, graphics, audio);
    }
};

} // namespace reone
