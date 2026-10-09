/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/coopprompts.h"

#include "reone/net/joincode.h"

namespace reone {

namespace game {

CoopPrompts CoopPrompts::forServer(const std::string &server, uint16_t hostPort) {
    CoopPrompts prompts;
    prompts.service = !server.empty();
    prompts.port = hostPort ? hostPort : kDefaultPort;
    return prompts;
}

std::string CoopPrompts::joinButton() const {
    return service ? "Join a game" : "Join by IP:port";
}

std::string CoopPrompts::entryTitle() const {
    return service ? "Type or paste the join code:" : "Type or paste the host's IP:port:";
}

std::string CoopPrompts::portForwardHint() const {
    auto p = std::to_string(port);
    return "Forward UDP " + p + " on your router, then share your public IP and port (like 203.0.113.7:" + p + ").";
}

std::string CoopPrompts::idleHelp(bool mainMenu) const {
    if (service) {
        if (mainMenu) {
            return "Host: start a new game or load a save; your join code shows here and on the in-game Co-op page.\n\n"
                   "Join: type or paste the code your host gave you.";
        }
        return "Let friends join this game. You get a join code to give them.";
    }
    std::string text = mainMenu
                           ? "Host: start a new game or load a save. You host on UDP port " + std::to_string(port) + ".\n\n"
                                 "Join by IP:port: type the host's address, like 203.0.113.7:" + std::to_string(port) + "."
                           : "Let friends join this game by your IP:port. You host on UDP port " + std::to_string(port) + ".";
    return text + "\n\n" + portForwardHint();
}

std::string CoopPrompts::hostWithoutService() const {
    return "Hosting on UDP port " + std::to_string(port) + ". Friends join by IP:port.\n\n" + portForwardHint();
}

std::string CoopPrompts::joinTarget(const std::string &entry) const {
    if (service || entry.empty() || net::looksLikeAddress(entry) || entry.find(':') != std::string::npos ||
        entry.find('.') == std::string::npos) {
        return entry;
    }
    return entry + ":" + std::to_string(port);
}

} // namespace game

} // namespace reone
