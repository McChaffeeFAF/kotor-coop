/* Copyright (c) 2026 The reone project contributors */

#include "reone/net/joincode.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace reone {

namespace net {

std::string normalizeJoinCode(std::string_view text) {
    if (text.size() > 16) {
        return std::string();
    }
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    std::string code;
    int dashes = 0;
    for (size_t i = begin; i < end; ++i) {
        char c = static_cast<char>(std::toupper(static_cast<unsigned char>(text[i])));
        if (c == '-') {
            ++dashes;
            continue;
        }
        if (!std::strchr(kJoinCodeAlphabet, c) || c == '\0') {
            return std::string();
        }
        code.push_back(c);
    }
    if (dashes > 1 || code.size() != kJoinCodeLength) {
        return std::string();
    }
    return code;
}

std::string displayJoinCode(const std::string &code) {
    if (code.size() != kJoinCodeLength) {
        return code;
    }
    return code.substr(0, 4) + "-" + code.substr(4);
}

bool looksLikeAddress(std::string_view text) {
    auto colon = text.rfind(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= text.size()) {
        return false;
    }
    auto port = text.substr(colon + 1);
    return std::all_of(port.begin(), port.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

std::string withDefaultPort(std::string_view text, uint16_t port) {
    if (text.empty() || looksLikeAddress(text) || text.find(':') != std::string_view::npos) {
        return std::string(text);
    }
    bool hostName = std::all_of(text.begin(), text.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_';
    });
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    bool dotted = text.front() != '.' && text.back() != '.' && text.find('.') != std::string_view::npos;
    if (!hostName || !(dotted || lower == "localhost")) {
        return std::string(text);
    }
    return std::string(text) + ":" + std::to_string(port);
}

bool parseServiceAddress(std::string_view text, std::string &host, uint16_t &port) {
    for (std::string_view scheme : {"kc://", "tcp://"}) {
        if (text.size() > scheme.size() && text.substr(0, scheme.size()) == scheme) {
            text.remove_prefix(scheme.size());
            break;
        }
    }
    while (!text.empty() && text.back() == '/') {
        text.remove_suffix(1);
    }
    if (text.empty() || text.size() > 253) {
        return false;
    }
    port = kDefaultServicePort;
    std::string_view name = text;
    auto colon = text.rfind(':');
    if (colon != std::string_view::npos) {
        auto digits = text.substr(colon + 1);
        if (digits.empty() || digits.size() > 5 ||
            !std::all_of(digits.begin(), digits.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
            return false;
        }
        int value = std::stoi(std::string(digits));
        if (value < 1 || value > 65535) {
            return false;
        }
        port = static_cast<uint16_t>(value);
        name = text.substr(0, colon);
    }
    if (name.size() >= 2 && name.front() == '[' && name.back() == ']') {
        name = name.substr(1, name.size() - 2);
    }
    if (name.empty()) {
        return false;
    }
    for (char c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == ':')) {
            return false;
        }
    }
    host = std::string(name);
    return true;
}

namespace {

constexpr char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

} // namespace

std::string base64Encode(const uint8_t *data, size_t size) {
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (size_t i = 0; i < size; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < size) {
            n |= static_cast<uint32_t>(data[i + 1]) << 8;
        }
        if (i + 2 < size) {
            n |= data[i + 2];
        }
        out.push_back(kBase64[(n >> 18) & 63]);
        out.push_back(kBase64[(n >> 12) & 63]);
        out.push_back(i + 1 < size ? kBase64[(n >> 6) & 63] : '=');
        out.push_back(i + 2 < size ? kBase64[n & 63] : '=');
    }
    return out;
}

bool base64Decode(std::string_view text, std::vector<uint8_t> &out) {
    out.clear();
    if (text.size() % 4 != 0) {
        return false;
    }
    out.reserve(text.size() / 4 * 3);
    for (size_t i = 0; i < text.size(); i += 4) {
        int v[4];
        int padding = 0;
        for (int k = 0; k < 4; ++k) {
            char c = text[i + k];
            if (c == '=') {
                // Padding only at the very end, in the last two places.
                if (i + 4 != text.size() || k < 2) {
                    return false;
                }
                ++padding;
                v[k] = 0;
            } else {
                if (padding) {
                    return false;
                }
                v[k] = base64Value(c);
                if (v[k] < 0) {
                    return false;
                }
            }
        }
        // Padding stands in for bits that must be zero; otherwise two different
        // texts would decode to the same bytes (not canonical).
        if ((padding == 2 && (v[1] & 0x0f) != 0) || (padding == 1 && (v[2] & 0x03) != 0)) {
            return false;
        }
        uint32_t n = (v[0] << 18) | (v[1] << 12) | (v[2] << 6) | v[3];
        out.push_back(static_cast<uint8_t>(n >> 16));
        if (padding < 2) {
            out.push_back(static_cast<uint8_t>(n >> 8));
        }
        if (padding < 1) {
            out.push_back(static_cast<uint8_t>(n));
        }
    }
    return true;
}

} // namespace net

} // namespace reone
