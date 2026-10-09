/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <cstdint>
#include <string>

namespace reone {

namespace game {

/**
 * What the Co-op screen says, by build configuration.
 *
 * With a join-code service (coop-server in reone.cfg) players share a join
 * code. Without one, the default for self-built copies, players join by the
 * host's IP:port, and the host is told which UDP port to forward.
 */
struct CoopPrompts {
    bool service {false};  // a join-code service is configured
    uint16_t port {27015}; // the UDP port a host listens on

    static constexpr uint16_t kDefaultPort = 27015;

    /** Prompts for a build whose coop-server setting is `server` (empty: none). */
    static CoopPrompts forServer(const std::string &server, uint16_t hostPort = 0);

    /** Main-menu button that opens the entry box. */
    std::string joinButton() const;
    /** Title of the entry box. */
    std::string entryTitle() const;
    /** Forward-the-port advice for a host without a service. */
    std::string portForwardHint() const;
    /** Help under the main-menu buttons, or in game when not in a session. */
    std::string idleHelp(bool mainMenu) const;
    /** Host page header without a service: the port and how friends join. */
    std::string hostWithoutService() const;

    /**
     * What to join for an entry: a bare host or IP without a port gets the
     * default port when there is no service (a join code never contains a
     * dot). Anything else is returned unchanged.
     */
    std::string joinTarget(const std::string &entry) const;
};

} // namespace game

} // namespace reone
