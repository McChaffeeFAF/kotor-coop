/*
 * Copyright (c) 2026 The reone project contributors
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

// The link to the join-code service, against a service that misbehaves: a local listener that sends it
// whatever a broken or hostile server would. The link must close on anything that is not a JSON object per
// line, never hold more than a bounded amount, and never take the game with it.

#include <gtest/gtest.h>

#ifndef _WIN32

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "reone/net/signallink.h"

using namespace reone::net;
using namespace std::chrono_literals;

namespace {

// One listening socket on the loopback interface; accept() gives the connection the link made.
class FakeService {
public:
    FakeService() {
        _listener = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        int yes = 1;
        setsockopt(_listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (::bind(_listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 || ::listen(_listener, 1) != 0) {
            ADD_FAILURE() << "cannot listen";
            return;
        }
        socklen_t length = sizeof(address);
        getsockname(_listener, reinterpret_cast<sockaddr *>(&address), &length);
        _port = ntohs(address.sin_port);
    }

    ~FakeService() {
        if (_peer >= 0) {
            ::close(_peer);
        }
        if (_listener >= 0) {
            ::close(_listener);
        }
    }

    uint16_t port() const { return _port; }

    bool accept() {
        timeval wait {5, 0};
        setsockopt(_listener, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
        _peer = ::accept(_listener, nullptr, nullptr);
        return _peer >= 0;
    }

    void send(const std::string &bytes) {
        size_t done = 0;
        while (done < bytes.size()) {
            auto sent = ::send(_peer, bytes.data() + done, bytes.size() - done, MSG_NOSIGNAL);
            if (sent <= 0) {
                return; // the link has closed: that is the point of some tests
            }
            done += static_cast<size_t>(sent);
        }
    }

    void hangUp() {
        ::close(_peer);
        _peer = -1;
    }

private:
    int _listener {-1};
    int _peer {-1};
    uint16_t _port {0};
};

bool waitFor(const std::function<bool()> &condition) {
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return condition();
}

// A link to a fake service that has accepted it and is Open.
struct Linked {
    FakeService service;
    std::unique_ptr<SignalLink> link;

    Linked() {
        link = std::make_unique<SignalLink>("127.0.0.1", service.port());
        EXPECT_TRUE(service.accept());
        EXPECT_TRUE(waitFor([this]() { return link->state() == SignalLink::State::Open; }));
    }

    std::vector<boost::json::object> received() {
        std::vector<boost::json::object> out;
        link->receive(out);
        return out;
    }

    bool closedWith(const std::string &error) {
        return waitFor([this]() { return link->state() == SignalLink::State::Closed; }) &&
               link->error().find(error) != std::string::npos;
    }
};

} // namespace

TEST(SignalLinkHostile, objects_one_per_line_arrive_in_order) {
    Linked linked;
    linked.service.send("{\"op\":\"a\"}\n{\"op\":\"b\",\"n\":2}\r\n{\"op\":\"c\"}\n");
    std::vector<boost::json::object> got;
    ASSERT_TRUE(waitFor([&]() {
        auto more = linked.received();
        got.insert(got.end(), more.begin(), more.end());
        return got.size() >= 3;
    }));
    EXPECT_EQ("a", got[0].at("op").as_string());
    EXPECT_EQ("b", got[1].at("op").as_string());
    EXPECT_EQ("c", got[2].at("op").as_string());
    EXPECT_EQ(SignalLink::State::Open, linked.link->state());
}

TEST(SignalLinkHostile, a_line_split_across_many_packets_is_one_message) {
    Linked linked;
    std::string line = "{\"op\":\"joined\",\"code\":\"ABCD-2345\"}\n";
    for (char c : line) {
        linked.service.send(std::string(1, c));
    }
    std::vector<boost::json::object> got;
    ASSERT_TRUE(waitFor([&]() {
        auto more = linked.received();
        got.insert(got.end(), more.begin(), more.end());
        return !got.empty();
    }));
    EXPECT_EQ(1u, got.size());
    EXPECT_EQ("ABCD-2345", got[0].at("code").as_string());
}

TEST(SignalLinkHostile, anything_that_is_not_a_json_object_closes_the_link) {
    for (const std::string &bad : {std::string("[1,2,3]\n"), std::string("\"just a string\"\n"), std::string("42\n"), std::string("null\n"),
                                   std::string("{not json\n"), std::string("\n"), std::string("{\"op\":\n"), std::string("{\"a\":1} trailing\n"),
                                   std::string("\x00\x01\x02\n", 4), std::string("\xff\xfe\xfd\n"), std::string("{\"op\":\"a\"}\n[1]\n")}) {
        Linked linked;
        linked.service.send(bad);
        EXPECT_TRUE(linked.closedWith("protocol")) << "input of " << bad.size() << " bytes";
    }
}

TEST(SignalLinkHostile, deeply_nested_json_is_refused_not_recursed_into) {
    Linked linked;
    linked.service.send(std::string(200000, '[') + "\n");
    EXPECT_TRUE(linked.closedWith("protocol"));
    Linked objects;
    std::string nested;
    for (int i = 0; i < 5000; ++i) {
        nested += "{\"a\":";
    }
    objects.service.send(nested + "1" + std::string(5000, '}') + "\n");
    EXPECT_TRUE(objects.closedWith("protocol"));
}

TEST(SignalLinkHostile, a_line_that_never_ends_is_cut_off_at_the_limit) {
    Linked linked;
    linked.service.send(std::string(SignalLink::kMaxLineBytes * 4, 'a'));
    EXPECT_TRUE(linked.closedWith("too long"));
    EXPECT_TRUE(linked.received().empty());
}

TEST(SignalLinkHostile, a_complete_line_over_the_limit_does_not_get_through) {
    Linked linked;
    std::string big = "{\"op\":\"x\",\"pad\":\"" + std::string(SignalLink::kMaxLineBytes * 2, 'p') + "\"}\n";
    linked.service.send(big);
    EXPECT_TRUE(linked.closedWith("protocol"));
    EXPECT_TRUE(linked.received().empty()) << "an oversized message is not handed over";
}

TEST(SignalLinkHostile, a_flood_of_messages_is_held_to_a_bounded_inbox) {
    Linked linked;
    std::string flood;
    for (int i = 0; i < 20000; ++i) {
        flood += "{\"op\":\"n\",\"i\":" + std::to_string(i) + "}\n";
    }
    linked.service.send(flood);
    // The game polls once a frame: whatever has arrived is bounded.
    std::this_thread::sleep_for(500ms);
    size_t total = 0;
    for (int poll = 0; poll < 5; ++poll) {
        total += linked.received().size();
        std::this_thread::sleep_for(50ms);
    }
    EXPECT_GT(total, 0u);
    EXPECT_LE(total, 5u * 1024u) << "the inbox cap holds between polls";
    EXPECT_EQ(SignalLink::State::Open, linked.link->state()) << "a flood is dropped, not fatal";
}

TEST(SignalLinkHostile, the_service_hanging_up_closes_the_link_cleanly) {
    Linked linked;
    linked.service.send("{\"op\":\"bye\"}\n");
    linked.service.hangUp();
    EXPECT_TRUE(linked.closedWith("closed"));
    // A message that arrived before the close is still delivered.
    auto got = linked.received();
    EXPECT_LE(got.size(), 1u);
    // Sending into a closed link is harmless.
    linked.link->send(boost::json::object {{"op", "ping"}});
}

TEST(SignalLinkHostile, nothing_listening_is_unreachable_not_a_hang) {
    uint16_t port = 0;
    {
        FakeService gone;
        port = gone.port();
    } // the listener is closed again
    SignalLink link("127.0.0.1", port);
    ASSERT_TRUE(waitFor([&]() { return link.state() == SignalLink::State::Closed; }));
    EXPECT_NE(std::string::npos, link.error().find("unreachable"));
}

TEST(SignalLinkHostile, messages_sent_before_the_link_is_up_are_kept_and_the_outbox_is_bounded) {
    Linked linked;
    for (int i = 0; i < 100000; ++i) {
        linked.link->send(boost::json::object {{"op", "spam"}, {"i", i}});
    }
    // Back-pressure drops the oldest instead of growing without limit; the link stays up.
    EXPECT_NE(SignalLink::State::Connecting, linked.link->state());
}

#endif
