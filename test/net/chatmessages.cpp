/* Copyright (c) 2026 The reone project contributors */

// Round trips for the chat and ping messages (type ids from 100).

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "reone/net/protocol.h"

using namespace reone::net;

namespace {

const std::string kPlayerNameMax(32, 'n');

// Decodes, re-encodes to the same bytes, and rejects every strict prefix and any trailing byte.
template <class T>
void expectRoundTrip(const T &message, const char *what) {
    auto bytes = encode(message);
    ASSERT_FALSE(bytes.empty()) << what;
    T back;
    ASSERT_TRUE(decode(bytes, back)) << what;
    EXPECT_EQ(bytes, encode(back)) << what;
    for (size_t n = 0; n < bytes.size(); ++n) {
        std::vector<uint8_t> prefix(bytes.begin(), bytes.begin() + n);
        T partial;
        EXPECT_FALSE(decode(prefix, partial)) << what << ": accepted a prefix of " << n << " bytes";
    }
    auto longer = bytes;
    longer.push_back(0);
    T extra;
    EXPECT_FALSE(decode(longer, extra)) << what << ": accepted a trailing byte";
}

} // namespace

TEST(NetChat, chat_send_round_trips_and_leaves_the_text_for_the_host_to_judge) {
    ChatSend s;
    s.channel = ChatChannel::Team;
    s.text = std::string(kMaxChatBytes, 'a');
    expectRoundTrip(s, "longest line");
    expectRoundTrip(ChatSend {}, "empty line");

    // Too long, control characters and bad UTF-8 still decode: the host answers with a notice.
    ChatSend out;
    s.text = std::string(kMaxChatWireBytes, 'a');
    EXPECT_TRUE(decode(encode(s), out));
    s.text = "a\x01\xFF";
    EXPECT_TRUE(decode(encode(s), out));
    s.text = std::string(kMaxChatWireBytes + 1, 'a');
    EXPECT_FALSE(decode(encode(s), out)) << "past the wire limit";

    auto bytes = encode(ChatSend {});
    bytes[1] = 2;
    EXPECT_FALSE(decode(bytes, out)) << "unknown channel";

    EXPECT_EQ(100, static_cast<int>(MessageType::ChatSend));
    EXPECT_TRUE(isClientMessage(MessageType::ChatSend));
    EXPECT_FALSE(isHostMessage(MessageType::ChatSend));
    EXPECT_STREQ("chat_send", messageTypeName(MessageType::ChatSend));
    ASSERT_TRUE(peekType(encode(ChatSend {})));
    EXPECT_EQ(MessageType::ChatSend, *peekType(encode(ChatSend {})));
    EXPECT_LE(encode(s).size(), kMaxClientMessageBytes);
}

TEST(NetChat, chat_message_round_trips_and_refuses_bad_text) {
    ChatMessage m;
    m.sender = UINT32_MAX;
    m.name = kPlayerNameMax;
    m.colour = kChatColours - 1;
    m.channel = ChatChannel::Team;
    m.text = std::string(kMaxChatBytes, 'x');
    expectRoundTrip(m, "longest line");
    m.text = "h\xC3\xA9llo \xE2\x9C\x93";
    expectRoundTrip(m, "utf-8");

    ChatMessage out;
    auto refused = [&](const char *what, auto change) {
        ChatMessage c = m;
        change(c);
        EXPECT_FALSE(decode(encode(c), out)) << what;
    };
    refused("empty text", [](ChatMessage &c) { c.text.clear(); });
    refused("empty name", [](ChatMessage &c) { c.name.clear(); });
    refused("control character", [](ChatMessage &c) { c.text = "a\nb"; });
    refused("invalid utf-8", [](ChatMessage &c) { c.text = "a\xC3"; });
    refused("overlong utf-8", [](ChatMessage &c) { c.text = "\xC0\x80"; });
    refused("surrogate", [](ChatMessage &c) { c.text = "\xED\xA0\x80"; });
    refused("colour out of range", [](ChatMessage &c) { c.colour = kChatColours; });
    refused("text too long", [](ChatMessage &c) { c.text = std::string(kMaxChatBytes + 1, 'x'); });

    EXPECT_EQ(101, static_cast<int>(MessageType::ChatMessage));
    EXPECT_TRUE(isHostMessage(MessageType::ChatMessage));
    EXPECT_FALSE(isClientMessage(MessageType::ChatMessage));
    EXPECT_STREQ("chat_message", messageTypeName(MessageType::ChatMessage));
    EXPECT_EQ(MessageType::ChatMessage, *peekType(encode(m)));
}

TEST(NetChat, ping_messages_round_trip_and_refuse_impossible_values) {
    PingSend p;
    p.position = {kMaxPingCoordinate, -kMaxPingCoordinate, 0.5f};
    p.target = UINT32_MAX;
    expectRoundTrip(p, "extreme ping");
    expectRoundTrip(PingSend {}, "empty ping");
    PingSend pout;
    p.position.x = kMaxPingCoordinate * 2;
    EXPECT_FALSE(decode(encode(p), pout));
    p.position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(decode(encode(p), pout));
    EXPECT_EQ(102, static_cast<int>(MessageType::PingSend));
    EXPECT_TRUE(isClientMessage(MessageType::PingSend));
    EXPECT_FALSE(isHostMessage(MessageType::PingSend));
    EXPECT_EQ(MessageType::PingSend, *peekType(encode(PingSend {})));

    PingMarker m;
    m.sender = 7;
    m.name = kPlayerNameMax;
    m.colour = 3;
    m.position = {1, 2, 3};
    m.target = 99;
    m.targetName = std::string(kMaxStringBytes, 'o');
    m.seconds = 6.0f;
    expectRoundTrip(m, "marker");
    m.targetName.clear();
    expectRoundTrip(m, "bare point");

    PingMarker out;
    auto refused = [&](const char *what, auto change) {
        PingMarker c = m;
        change(c);
        EXPECT_FALSE(decode(encode(c), out)) << what;
    };
    refused("zero lifetime", [](PingMarker &c) { c.seconds = 0.0f; });
    refused("lifetime too long", [](PingMarker &c) { c.seconds = kMaxPingSeconds + 1; });
    refused("colour", [](PingMarker &c) { c.colour = 200; });
    refused("empty name", [](PingMarker &c) { c.name.clear(); });
    refused("bad target name", [](PingMarker &c) { c.targetName = "\xFF"; });
    refused("position", [](PingMarker &c) { c.position.z = 1e9f; });
    EXPECT_EQ(103, static_cast<int>(MessageType::PingMarker));
    EXPECT_TRUE(isHostMessage(MessageType::PingMarker));
    EXPECT_FALSE(isClientMessage(MessageType::PingMarker));
    EXPECT_EQ(MessageType::PingMarker, *peekType(encode(m)));
}

TEST(NetChat, ids_between_the_old_range_and_100_stay_unknown) {
    for (int value = 30; value < 100; ++value) {
        EXPECT_FALSE(peekType({static_cast<uint8_t>(value)})) << value;
    }
    EXPECT_FALSE(peekType({104}));
}

TEST(NetChat, the_protocol_version_was_bumped_for_chat) {
    EXPECT_GE(kProtocolVersion, 6) << "older peers would treat type 100+ as a violation";
}

TEST(NetChat, valid_utf8_follows_the_standard) {
    EXPECT_TRUE(validUtf8(""));
    EXPECT_TRUE(validUtf8("plain"));
    EXPECT_TRUE(validUtf8("\xC2\x80\xDF\xBF\xE0\xA0\x80\xEF\xBF\xBF\xF0\x90\x80\x80\xF4\x8F\xBF\xBF"));
    for (const char *bad : {"\x80", "\xC1\xBF", "\xE0\x9F\xBF", "\xED\xA0\x80", "\xF0\x8F\xBF\xBF", "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xE2\x82", "\xE2\x28\xA1"}) {
        EXPECT_FALSE(validUtf8(bad)) << bad;
    }
}
