/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "reone/game/coopchat.h"

using namespace reone::game;
using namespace reone::net;

namespace {

ChatLine line(double time, const std::string &text, uint32_t sender = 1) {
    ChatLine l;
    l.time = time;
    l.sender = sender;
    l.name = "P";
    l.text = text;
    return l;
}

} // namespace

// Validation

TEST(CoopChatPolicy, accepts_a_plain_line_and_cleans_it) {
    CoopChatPolicy policy;
    std::string out;
    EXPECT_EQ(ChatVerdict::Ok, policy.checkChat(1, "  hello   there \t", 0.0, out));
    EXPECT_EQ("hello there", out);
}

TEST(CoopChatPolicy, strips_control_characters) {
    CoopChatPolicy policy;
    std::string out;
    EXPECT_EQ(ChatVerdict::Ok, policy.checkChat(1, std::string("a\x01" "b\x1B[31m\x7F" "c\r\nd\xC2\x85" "e"), 0.0, out));
    EXPECT_EQ("ab[31mcde", out);
    EXPECT_EQ("h\xC3\xA9", stripChatControls("h\xC3\xA9")) << "other two-byte characters are kept";
    EXPECT_EQ(ChatVerdict::Empty, policy.checkChat(1, "\x01\x02 \t\n", 0.0, out));
    EXPECT_TRUE(out.empty());
}

TEST(CoopChatPolicy, refuses_a_line_that_is_too_long) {
    CoopChatPolicy policy;
    std::string out;
    EXPECT_EQ(ChatVerdict::Ok, policy.checkChat(1, std::string(kMaxChatBytes, 'a'), 0.0, out));
    EXPECT_EQ(ChatVerdict::TooLong, policy.checkChat(1, std::string(kMaxChatBytes + 1, 'a'), 1.0, out));
    EXPECT_TRUE(out.empty());
    // The limit is in bytes, not characters: 101 two-byte characters are too long.
    std::string wide;
    for (int i = 0; i < 101; ++i) {
        wide += "\xC3\xA9";
    }
    EXPECT_EQ(ChatVerdict::TooLong, policy.checkChat(1, wide, 2.0, out));
}

TEST(CoopChatPolicy, refuses_invalid_utf8) {
    CoopChatPolicy policy;
    std::string out;
    double t = 0;
    for (const char *bad : {"\xFF", "a\xC3", "\xC0\x80", "\xE0\x80\x80", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\x80", "\xF8\x88\x80\x80\x80"}) {
        EXPECT_EQ(ChatVerdict::InvalidUtf8, policy.checkChat(1, bad, t += 10, out)) << bad;
    }
    EXPECT_EQ(ChatVerdict::Ok, policy.checkChat(1, "\xE2\x9C\x93 \xF0\x9F\x98\x80", t += 10, out));
}

TEST(CoopChatPolicy, limits_the_rate_per_player_over_a_window) {
    CoopChatPolicy policy;
    std::string out;
    for (int i = 0; i < kChatMessagesPerWindow; ++i) {
        EXPECT_EQ(ChatVerdict::Ok, policy.checkChat(1, "hi", i * 0.1, out)) << i;
    }
    EXPECT_EQ(ChatVerdict::RateLimited, policy.checkChat(1, "flood", 1.0, out));
    EXPECT_EQ(ChatVerdict::Ok, policy.checkChat(2, "me too", 1.0, out)) << "another player has its own allowance";
    EXPECT_EQ(ChatVerdict::Ok, policy.checkChat(1, "later", kChatWindowSeconds + 0.1, out));
}

TEST(CoopChatPolicy, the_host_is_not_rate_limited_or_muted) {
    CoopChatPolicy policy;
    std::string out;
    policy.setName(0, "Host");
    policy.mute("Host");
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(ChatVerdict::Ok, policy.checkChat(0, "hi", 0.0, out));
    }
}

TEST(CoopChatPolicy, a_muted_player_cannot_chat_or_ping_until_unmuted) {
    CoopChatPolicy policy;
    std::string out;
    policy.setName(4, "Bastila");
    EXPECT_EQ(ChatVerdict::Ok, policy.checkChat(4, "hi", 0.0, out));
    policy.mute("bastila");
    EXPECT_TRUE(policy.isMuted("BASTILA"));
    EXPECT_EQ(ChatVerdict::Muted, policy.checkChat(4, "hi", 10.0, out));
    EXPECT_EQ(PingVerdict::Muted, policy.checkPing(4, false, false, 10.0));
    EXPECT_EQ(ChatVerdict::Ok, policy.checkChat(5, "hi", 10.0, out)) << "others are not affected";
    EXPECT_TRUE(policy.unmute("Bastila"));
    EXPECT_FALSE(policy.unmute("Bastila"));
    EXPECT_EQ(ChatVerdict::Ok, policy.checkChat(4, "hi", 20.0, out));
}

TEST(CoopChatPolicy, a_mute_survives_a_reconnect_under_a_new_id) {
    CoopChatPolicy policy;
    std::string out;
    policy.setName(4, "Carth");
    policy.mute("Carth");
    policy.forget(4);
    policy.setName(9, "Carth");
    EXPECT_EQ(ChatVerdict::Muted, policy.checkChat(9, "hi", 0.0, out));
}

TEST(CoopChatPolicy, pings_name_an_object_that_exists) {
    CoopChatPolicy policy;
    EXPECT_EQ(PingVerdict::Ok, policy.checkPing(1, false, false, 0.0)) << "a bare point needs no object";
    EXPECT_EQ(PingVerdict::Ok, policy.checkPing(1, true, true, 1.0));
    EXPECT_EQ(PingVerdict::UnknownTarget, policy.checkPing(1, true, false, 2.0));
}

TEST(CoopChatPolicy, limits_the_ping_rate_and_a_refused_ping_does_not_use_the_allowance) {
    CoopChatPolicy policy;
    for (int i = 0; i < kPingsPerWindow; ++i) {
        EXPECT_EQ(PingVerdict::UnknownTarget, policy.checkPing(1, true, false, i * 0.1));
    }
    for (int i = 0; i < kPingsPerWindow; ++i) {
        EXPECT_EQ(PingVerdict::Ok, policy.checkPing(1, false, false, i * 0.1));
    }
    EXPECT_EQ(PingVerdict::RateLimited, policy.checkPing(1, false, false, 1.0));
    EXPECT_EQ(PingVerdict::Ok, policy.checkPing(1, false, false, kPingWindowSeconds + 0.1));
    EXPECT_EQ(PingVerdict::Ok, policy.checkPing(0, false, false, 0.0)) << "the host";
}

// Relay

TEST(CoopChatRelay, story_mode_sends_every_channel_to_everyone) {
    std::vector<ChatPeer> peers {{0, ChatSide::Party}, {1, ChatSide::Party}, {2, ChatSide::Party}, {3, ChatSide::Guest}};
    for (auto channel : {ChatChannel::All, ChatChannel::Team}) {
        auto to = chatRecipients(false, channel, peers[1], peers);
        EXPECT_EQ((std::vector<uint32_t> {0, 1, 2, 3}), to);
    }
}

TEST(CoopChatRelay, free_mode_all_channel_reaches_everyone_including_the_sender) {
    std::vector<ChatPeer> peers {{0, ChatSide::Party}, {1, ChatSide::Guest}, {2, ChatSide::Guest}};
    EXPECT_EQ((std::vector<uint32_t> {0, 1, 2}), chatRecipients(true, ChatChannel::All, peers[1], peers));
}

TEST(CoopChatRelay, free_mode_team_channel_stays_on_the_senders_side) {
    std::vector<ChatPeer> peers {{0, ChatSide::Party}, {1, ChatSide::Party}, {2, ChatSide::Guest}, {3, ChatSide::Guest}, {4, ChatSide::Guest}};
    EXPECT_EQ((std::vector<uint32_t> {2, 3, 4}), chatRecipients(true, ChatChannel::Team, peers[2], peers));
    EXPECT_EQ((std::vector<uint32_t> {0, 1}), chatRecipients(true, ChatChannel::Team, peers[0], peers));
    EXPECT_EQ((std::vector<uint32_t> {0, 1}), chatRecipients(true, ChatChannel::Team, peers[1], peers));
}

TEST(CoopChatRelay, colours_are_stable_in_range_and_the_host_is_distinct) {
    EXPECT_EQ(0, chatColourFor(0));
    for (uint32_t id = 1; id < 100; ++id) {
        EXPECT_GE(chatColourFor(id), 1);
        EXPECT_LT(chatColourFor(id), kChatColours);
    }
    EXPECT_NE(chatColourFor(1), chatColourFor(2));
    auto c = chatColour(250);
    EXPECT_GE(c.r, 0.0f);
    EXPECT_LE(c.r, 1.0f);
}

// Overlay

TEST(CoopChatLog, a_line_shows_then_fades_then_goes) {
    ChatLog log;
    log.add(line(10.0, "hi"));
    EXPECT_EQ(1u, log.visible(10.0).size());
    EXPECT_FLOAT_EQ(1.0f, log.visible(10.0 + ChatLog::kShowSeconds - 0.01)[0].alpha);
    auto half = log.visible(10.0 + ChatLog::kShowSeconds + ChatLog::kFadeSeconds / 2);
    ASSERT_EQ(1u, half.size());
    EXPECT_NEAR(0.5f, half[0].alpha, 1e-4);
    EXPECT_TRUE(log.visible(10.0 + ChatLog::kShowSeconds + ChatLog::kFadeSeconds).empty());
    EXPECT_EQ(1u, log.lines().size()) << "the log keeps it";
}

TEST(CoopChatLog, the_overlay_shows_only_the_newest_lines_in_order) {
    ChatLog log;
    for (int i = 0; i < 10; ++i) {
        log.add(line(0.0, std::to_string(i)));
    }
    auto shown = log.visible(1.0);
    ASSERT_EQ(ChatLog::kOverlayLines, shown.size());
    EXPECT_EQ("4", shown.front().line->text);
    EXPECT_EQ("9", shown.back().line->text);
}

TEST(CoopChatLog, an_open_log_keeps_old_lines_opaque_and_scrolls) {
    ChatLog log;
    for (int i = 0; i < 20; ++i) {
        log.add(line(0.0, std::to_string(i)));
    }
    EXPECT_TRUE(log.visible(1000.0).empty()) << "faded";
    log.setOpen(true);
    auto shown = log.visible(1000.0);
    ASSERT_EQ(ChatLog::kOverlayLines, shown.size());
    EXPECT_EQ("19", shown.back().line->text);
    EXPECT_FLOAT_EQ(1.0f, shown.back().alpha);

    EXPECT_EQ(3u, log.scroll(3));
    EXPECT_EQ("16", log.visible(1000.0).back().line->text);
    EXPECT_EQ(0u, log.scroll(-10)) << "forward past the newest stops at the newest";
    log.scroll(1000);
    EXPECT_EQ(20u - ChatLog::kOverlayLines, log.offset()) << "back past the oldest stops at the oldest";
    EXPECT_EQ("0", log.visible(1000.0).front().line->text);
}

TEST(CoopChatLog, a_scrolled_window_holds_still_while_new_lines_arrive) {
    ChatLog log;
    for (int i = 0; i < 20; ++i) {
        log.add(line(0.0, std::to_string(i)));
    }
    log.setOpen(true);
    log.scroll(4);
    auto before = log.visible(5.0).back().line->text;
    log.add(line(5.0, "new"));
    EXPECT_EQ(before, log.visible(5.0).back().line->text);
    EXPECT_EQ(5u, log.offset());
    log.scrollToEnd();
    EXPECT_EQ("new", log.visible(5.0).back().line->text);
}

TEST(CoopChatLog, closing_returns_to_the_newest_and_the_history_is_bounded) {
    ChatLog log;
    for (size_t i = 0; i < ChatLog::kHistory + 50; ++i) {
        log.add(line(0.0, std::to_string(i)));
    }
    EXPECT_EQ(ChatLog::kHistory, log.lines().size());
    EXPECT_EQ("50", log.lines().front().text);
    log.setOpen(true);
    log.scroll(30);
    log.setOpen(false);
    EXPECT_EQ(0u, log.offset());
    log.setOpen(true);
    log.scroll(30);
    for (int i = 0; i < 5; ++i) {
        log.add(line(0.0, "x"));
    }
    EXPECT_EQ(30u, log.offset()) << "a full history drops an old line for each new one, so the window stays put";
}

TEST(CoopPingBoard, a_marker_lives_its_time_then_fades_out) {
    PingBoard board;
    PingMarker m;
    m.sender = 1;
    m.name = "A";
    m.seconds = 6.0f;
    board.add(m, 100.0);
    auto now = board.active(101.0);
    ASSERT_EQ(1u, now.size());
    EXPECT_FLOAT_EQ(1.0f, now[0].alpha);
    EXPECT_EQ("A", now[0].marker->name);
    auto fading = board.active(100.0 + 6.0 - PingBoard::kFadeSeconds / 2);
    ASSERT_EQ(1u, fading.size());
    EXPECT_NEAR(0.5f, fading[0].alpha, 1e-3);
    EXPECT_TRUE(board.active(106.0).empty());
    EXPECT_EQ(0u, board.size());
}

TEST(CoopPingBoard, a_new_ping_replaces_the_senders_old_one_but_not_others) {
    PingBoard board;
    PingMarker m;
    m.name = "A";
    m.seconds = 6.0f;
    m.sender = 1;
    m.target = 5;
    board.add(m, 0.0);
    m.sender = 2;
    board.add(m, 0.0);
    m.sender = 1;
    m.target = 6;
    board.add(m, 3.0);
    auto shown = board.active(4.0);
    ASSERT_EQ(2u, shown.size());
    EXPECT_EQ(6u, shown[0].marker->target);
    EXPECT_EQ(5u, shown[1].marker->target);
    EXPECT_EQ(1u, board.active(6.5).size()) << "sender 1's second ping lasts to 9";
}
