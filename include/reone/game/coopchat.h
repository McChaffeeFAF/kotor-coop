/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "reone/net/protocol.h"

namespace reone {

namespace game {

/*
 * Co-op text chat and pings: the parts that need no engine. The host validates
 * and relays; the client keeps a log and a set of markers it draws.
 */

// Validation

enum class ChatVerdict {
    Ok,
    Empty,       // nothing left after control characters and blanks are removed
    TooLong,     // more than kMaxChatBytes
    InvalidUtf8, // not well-formed UTF-8
    RateLimited,
    Muted
};

enum class PingVerdict {
    Ok,
    UnknownTarget, // the object the ping names does not exist
    RateLimited,
    Muted
};

const char *chatVerdictText(ChatVerdict verdict);
const char *pingVerdictText(PingVerdict verdict);

/** Drops control characters (C0, DEL, C1) and trims blanks; collapses runs of blanks. Input must be valid UTF-8. */
std::string stripChatControls(const std::string &text);

/** At most count events within any window of seconds. */
class ChatRateLimiter {
public:
    ChatRateLimiter(int count = 5, double seconds = 5.0) :
        _count(count), _seconds(seconds) {}

    bool allow(double now);

private:
    int _count;
    double _seconds;
    std::deque<double> _times;
};

constexpr int kChatMessagesPerWindow = 5;
constexpr double kChatWindowSeconds = 5.0;
constexpr int kPingsPerWindow = 3;
constexpr double kPingWindowSeconds = 4.0;
constexpr float kPingLifetime = 6.0f;

/** The host's rules: who may speak or ping, and what is let through. The host's own id is 0 and is never muted or limited. */
class CoopChatPolicy {
public:
    /** On Ok, cleaned is the text to relay. */
    ChatVerdict checkChat(uint32_t sender, const std::string &raw, double now, std::string &cleaned);
    PingVerdict checkPing(uint32_t sender, bool targeted, bool targetExists, double now);

    /** Mutes are by player name (case-insensitive), so they survive a reconnect. */
    void mute(const std::string &name) { _muted.insert(key(name)); }
    bool unmute(const std::string &name) { return _muted.erase(key(name)) > 0; }
    bool isMuted(const std::string &name) const { return _muted.count(key(name)) > 0; }
    void setName(uint32_t sender, const std::string &name) { _names[sender] = name; }
    void forget(uint32_t sender);
    const std::set<std::string> &muted() const { return _muted; }

private:
    static std::string key(const std::string &name);
    bool mutedSender(uint32_t sender) const;

    std::set<std::string> _muted;
    std::map<uint32_t, std::string> _names;
    std::map<uint32_t, ChatRateLimiter> _chat;
    std::map<uint32_t, ChatRateLimiter> _pings;
};

// Relay

enum class ChatSide : uint8_t {
    Party, // the host and players who control party members
    Guest  // Free Mode guests, outside the party
};

struct ChatPeer {
    uint32_t id {0}; // 0 is the host
    ChatSide side {ChatSide::Party};
};

/**
 * Who gets a line from sender. Everyone (the sender included, so their own
 * line shows) for the All channel; for Team, in Free Mode, the peers on the
 * sender's side; in Story Mode everyone is the party, so Team is All.
 */
std::vector<uint32_t> chatRecipients(bool freeMode, net::ChatChannel channel, const ChatPeer &sender, const std::vector<ChatPeer> &peers);

/** Colour index for a player: the host is 0, clients cycle through the rest. */
uint8_t chatColourFor(uint32_t sender);

struct ChatColour {
    float r, g, b;
};
ChatColour chatColour(uint8_t index);

// Overlay (client)

struct ChatLine {
    double time {0.0};
    uint32_t sender {0};
    std::string name;
    uint8_t colour {0};
    net::ChatChannel channel {net::ChatChannel::All};
    std::string text;
};

struct ChatShown {
    const ChatLine *line {nullptr};
    float alpha {1.0f};
};

/**
 * The chat overlay's state. Fresh lines show for kShowSeconds then fade over
 * kFadeSeconds, newest at the bottom, at most kOverlayLines of them. While the
 * log is open (typing, or scrolled back) lines stay opaque and the window can be
 * scrolled through the last kHistory lines.
 */
class ChatLog {
public:
    static constexpr double kShowSeconds = 8.0;
    static constexpr double kFadeSeconds = 2.0;
    static constexpr size_t kOverlayLines = 6;
    static constexpr size_t kHistory = 200;

    void add(ChatLine line);
    const std::deque<ChatLine> &lines() const { return _lines; }

    /** The lines to draw, oldest first. */
    std::vector<ChatShown> visible(double now) const;

    void setOpen(bool open);
    bool open() const { return _open; }
    /** Scroll back (positive) or forward (negative) by lines; clamped. Returns the new offset. */
    size_t scroll(int lines);
    void scrollToEnd() { _offset = 0; }
    /** Lines hidden below the window (0: at the newest). */
    size_t offset() const { return _offset; }

    static float alphaFor(double age);

private:
    std::deque<ChatLine> _lines;
    bool _open {false};
    size_t _offset {0};
};

struct PingShown {
    const net::PingMarker *marker {nullptr};
    float alpha {1.0f};
};

/** Markers in the world: one per sender, a new ping replaces the sender's old one. */
class PingBoard {
public:
    static constexpr double kFadeSeconds = 0.75;

    void add(const net::PingMarker &marker, double now);
    /** Live markers; the pointers stay valid until the next call. */
    std::vector<PingShown> active(double now);
    size_t size() const { return _pings.size(); }
    void clear() { _pings.clear(); }

private:
    struct Entry {
        net::PingMarker marker;
        double expires {0.0};
    };
    std::map<uint32_t, Entry> _pings;
    std::vector<net::PingMarker> _shown;
};

} // namespace game

} // namespace reone
