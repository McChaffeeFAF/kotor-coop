/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "reone/game/coopprompts.h"

using namespace reone::game;

// No coop-server (the default for self-built copies): join by IP:port, and
// a host is told which UDP port to forward.
TEST(CoopPrompts, without_a_server_join_is_by_ip_and_port) {
    auto prompts = CoopPrompts::forServer("");
    EXPECT_FALSE(prompts.service);
    EXPECT_EQ(27015, prompts.port);
    EXPECT_EQ("Join by IP:port", prompts.joinButton());
    EXPECT_EQ("Type or paste the host's IP:port:", prompts.entryTitle());
    auto help = prompts.idleHelp(true);
    EXPECT_NE(std::string::npos, help.find("UDP port 27015"));
    EXPECT_NE(std::string::npos, help.find("Forward UDP 27015 on your router"));
    EXPECT_EQ(std::string::npos, help.find("join code"));
    auto host = prompts.hostWithoutService();
    EXPECT_NE(std::string::npos, host.find("Hosting on UDP port 27015"));
    EXPECT_NE(std::string::npos, host.find("share your public IP and port"));
}

TEST(CoopPrompts, host_port_follows_the_configured_port) {
    auto prompts = CoopPrompts::forServer("", 28000);
    EXPECT_EQ(28000, prompts.port);
    EXPECT_NE(std::string::npos, prompts.hostWithoutService().find("UDP port 28000"));
    EXPECT_NE(std::string::npos, prompts.portForwardHint().find("Forward UDP 28000"));
    EXPECT_EQ("203.0.113.7:28000", prompts.joinTarget("203.0.113.7"));
}

// With a server (official builds packaged with -Server): join codes, and the
// wording is what it always was.
TEST(CoopPrompts, with_a_server_join_is_by_code) {
    auto prompts = CoopPrompts::forServer("join.example.org:27200");
    EXPECT_TRUE(prompts.service);
    EXPECT_EQ("Join a game", prompts.joinButton());
    EXPECT_EQ("Type or paste the join code:", prompts.entryTitle());
    EXPECT_EQ("Host: start a new game or load a save; your join code shows here and on the in-game Co-op page.\n\n"
              "Join: type or paste the code your host gave you.",
              prompts.idleHelp(true));
    EXPECT_EQ("Let friends join this game. You get a join code to give them.", prompts.idleHelp(false));
}

TEST(CoopPrompts, join_target_adds_the_default_port_only_without_a_server) {
    auto none = CoopPrompts::forServer("");
    EXPECT_EQ("203.0.113.7:27015", none.joinTarget("203.0.113.7"));
    EXPECT_EQ("HOST.EXAMPLE.ORG:27015", none.joinTarget("HOST.EXAMPLE.ORG"));
    EXPECT_EQ("203.0.113.7:27016", none.joinTarget("203.0.113.7:27016"));
    EXPECT_EQ("K7QM-4TXR", none.joinTarget("K7QM-4TXR"));
    EXPECT_EQ("", none.joinTarget(""));

    auto server = CoopPrompts::forServer("join.example.org");
    EXPECT_EQ("203.0.113.7", server.joinTarget("203.0.113.7"));
    EXPECT_EQ("K7QM-4TXR", server.joinTarget("K7QM-4TXR"));
    EXPECT_EQ("127.0.0.1:27015", server.joinTarget("127.0.0.1:27015"));
}
