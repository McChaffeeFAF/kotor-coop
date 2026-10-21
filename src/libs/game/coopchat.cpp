/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/coopchat.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace reone {

namespace game {

const char *chatVerdictText(ChatVerdict verdict) {
    switch (verdict) {
    case ChatVerdict::Ok:
        return "";
    case ChatVerdict::Empty:
        return "Nothing to send";
    case ChatVerdict::TooLong:
        return "That message is too long";
    case ChatVerdict::InvalidUtf8:
        return "That message could not be sent";
    case ChatVerdict::RateLimited:
        return "You are sending messages too fast";
    case ChatVerdict::Muted:
        return "The host has muted you";
    }
    return "";
}

const char *pingVerdictText(PingVerdict verdict) {
    switch (verdict) {
    case PingVerdict::Ok:
        return "";
    case PingVerdict::UnknownTarget:
        return "That cannot be pinged";
    case PingVerdict::RateLimited:
        return "You are pinging too fast";
    case PingVerdict::Muted:
        return "The host has muted you";
    }
    return "";
}

std::string stripChatControls(const std::string &text) {
    std::string out;
    out.reserve(text.size());
    bool pendingBlank = false;
    for (size_t i = 0; i < text.size(); ++i) {
        auto c = static_cast<unsigned char>(text[i]);
        // C1 controls are U+0080..U+009F, encoded C2 80 .. C2 9F.
        if (c == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) >= 0x80 &&
            static_cast<unsigned char>(text[i + 1]) <= 0x9F) {
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t') {
            pendingBlank = !out.empty();
            continue;
        }
        if (c < 0x20 || c == 0x7F) {
            continue;
        }
        if (pendingBlank) {
            out.push_back(' ');
            pendingBlank = false;
        }
        out.push_back(static_cast<char>(c));
    }
    return out;
}

bool ChatRateLimiter::allow(double now) {
    while (!_times.empty() && now - _times.front() >= _seconds) {
        _times.pop_front();
    }
    if (static_cast<int>(_times.size()) >= _count) {
        return false;
    }
    _times.push_back(now);
    return true;
}

std::string CoopChatPolicy::key(const std::string &name) {
    std::string out = name;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool CoopChatPolicy::mutedSender(uint32_t sender) const {
    if (sender == 0) {
        return false;
    }
    auto it = _names.find(sender);
    return it != _names.end() && isMuted(it->second);
}

void CoopChatPolicy::forget(uint32_t sender) {
    _names.erase(sender);
    _chat.erase(sender);
    _pings.erase(sender);
}

ChatVerdict CoopChatPolicy::checkChat(uint32_t sender, const std::string &raw, double now, std::string &cleaned) {
    cleaned.clear();
    if (mutedSender(sender)) {
        return ChatVerdict::Muted;
    }
    if (raw.size() > net::kMaxChatBytes) {
        return ChatVerdict::TooLong;
    }
    if (!net::validUtf8(raw)) {
        return ChatVerdict::InvalidUtf8;
    }
    std::string text = stripChatControls(raw);
    if (text.empty()) {
        return ChatVerdict::Empty;
    }
    if (sender != 0) {
        auto it = _chat.try_emplace(sender, kChatMessagesPerWindow, kChatWindowSeconds).first;
        if (!it->second.allow(now)) {
            return ChatVerdict::RateLimited;
        }
    }
    cleaned = std::move(text);
    return ChatVerdict::Ok;
}

PingVerdict CoopChatPolicy::checkPing(uint32_t sender, bool targeted, bool targetExists, double now) {
    if (mutedSender(sender)) {
        return PingVerdict::Muted;
    }
    if (targeted && !targetExists) {
        return PingVerdict::UnknownTarget;
    }
    if (sender != 0) {
        auto it = _pings.try_emplace(sender, kPingsPerWindow, kPingWindowSeconds).first;
        if (!it->second.allow(now)) {
            return PingVerdict::RateLimited;
        }
    }
    return PingVerdict::Ok;
}

std::vector<uint32_t> chatRecipients(bool freeMode, net::ChatChannel channel, const ChatPeer &sender, const std::vector<ChatPeer> &peers) {
    std::vector<uint32_t> out;
    bool team = freeMode && channel == net::ChatChannel::Team;
    for (const auto &peer : peers) {
        if (!team || peer.side == sender.side) {
            out.push_back(peer.id);
        }
    }
    return out;
}

uint8_t chatColourFor(uint32_t sender) {
    if (sender == 0) {
        return 0;
    }
    return static_cast<uint8_t>(1 + (sender - 1) % (net::kChatColours - 1));
}

ChatColour chatColour(uint8_t index) {
    static const ChatColour palette[net::kChatColours] = {
        {1.00f, 0.85f, 0.30f}, {0.40f, 0.80f, 1.00f}, {0.55f, 1.00f, 0.55f}, {1.00f, 0.55f, 0.55f},
        {0.85f, 0.60f, 1.00f}, {1.00f, 0.70f, 0.30f}, {0.40f, 1.00f, 0.90f}, {1.00f, 0.60f, 0.85f},
        {0.75f, 0.90f, 0.35f}, {0.60f, 0.70f, 1.00f}, {1.00f, 1.00f, 0.60f}, {0.70f, 0.95f, 0.80f},
        {1.00f, 0.80f, 0.70f}, {0.80f, 0.75f, 1.00f}, {0.90f, 0.90f, 0.90f}, {0.65f, 0.85f, 0.95f}};
    return palette[index % net::kChatColours];
}

// Chat log

float ChatLog::alphaFor(double age) {
    if (age < kShowSeconds) {
        return 1.0f;
    }
    double t = (age - kShowSeconds) / kFadeSeconds;
    return t >= 1.0 ? 0.0f : static_cast<float>(1.0 - t);
}

void ChatLog::add(ChatLine line) {
    _lines.push_back(std::move(line));
    if (_offset > 0) {
        ++_offset; // keep the window where the reader put it
    }
    while (_lines.size() > kHistory) {
        _lines.pop_front();
        if (_offset > 0) {
            --_offset;
        }
    }
    _offset = std::min(_offset, _lines.size() > kOverlayLines ? _lines.size() - kOverlayLines : 0);
}

void ChatLog::setOpen(bool open) {
    _open = open;
    if (!open) {
        _offset = 0;
    }
}

size_t ChatLog::scroll(int lines) {
    size_t maxOffset = _lines.size() > kOverlayLines ? _lines.size() - kOverlayLines : 0;
    if (lines >= 0) {
        _offset = std::min(maxOffset, _offset + static_cast<size_t>(lines));
    } else {
        size_t back = static_cast<size_t>(-static_cast<int64_t>(lines));
        _offset = back > _offset ? 0 : _offset - back;
    }
    return _offset;
}

std::vector<ChatShown> ChatLog::visible(double now) const {
    std::vector<ChatShown> out;
    if (_open || _offset > 0) {
        size_t end = _lines.size() - std::min(_offset, _lines.size());
        size_t begin = end > kOverlayLines ? end - kOverlayLines : 0;
        for (size_t i = begin; i < end; ++i) {
            out.push_back({&_lines[i], 1.0f});
        }
        return out;
    }
    for (const auto &line : _lines) {
        float alpha = alphaFor(now - line.time);
        if (alpha > 0.0f) {
            out.push_back({&line, alpha});
        }
    }
    if (out.size() > kOverlayLines) {
        out.erase(out.begin(), out.end() - kOverlayLines);
    }
    return out;
}

// Pings

void PingBoard::add(const net::PingMarker &marker, double now) {
    float seconds = std::clamp(marker.seconds, 0.1f, net::kMaxPingSeconds);
    _pings[marker.sender] = {marker, now + seconds};
}

std::vector<PingShown> PingBoard::active(double now) {
    for (auto it = _pings.begin(); it != _pings.end();) {
        it = it->second.expires <= now ? _pings.erase(it) : std::next(it);
    }
    _shown.clear();
    _shown.reserve(_pings.size());
    std::vector<PingShown> out;
    for (const auto &[sender, entry] : _pings) {
        _shown.push_back(entry.marker);
        double left = entry.expires - now;
        float alpha = left >= kFadeSeconds ? 1.0f : static_cast<float>(left / kFadeSeconds);
        out.push_back({nullptr, alpha});
    }
    for (size_t i = 0; i < out.size(); ++i) {
        out[i].marker = &_shown[i];
    }
    return out;
}

} // namespace game

} // namespace reone
