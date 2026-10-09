/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reone {

namespace net {

/**
 * Join codes (co-op design notes, "Hosting and joining a session"): eight
 * characters from an alphabet without look-alikes (no 0/O, 1/I/L), shown
 * as XXXX-XXXX. Typed codes are case-insensitive and the dash is optional.
 * tools/coopserver/joincode.py has the same rules.
 */
constexpr char kJoinCodeAlphabet[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
constexpr size_t kJoinCodeLength = 8;

/** The canonical code for what a player typed or pasted, or empty when it is not one. */
std::string normalizeJoinCode(std::string_view text);

/** XXXX-XXXX for a canonical code. */
std::string displayJoinCode(const std::string &code);

/** True for "ip:port" or "host:port" (what --join takes besides a code). */
bool looksLikeAddress(std::string_view text);

/**
 * "ip" or "host.name" with no port becomes "ip:port" (also "localhost"); anything
 * else, including text that already has a port and join codes, is returned as is.
 */
std::string withDefaultPort(std::string_view text, uint16_t port);

/**
 * The join-code service address from --coop-server: "host", "host:port",
 * "tcp://host:port" or "kc://host:port". The default port is 27200.
 */
bool parseServiceAddress(std::string_view text, std::string &host, uint16_t &port);

constexpr uint16_t kDefaultServicePort = 27200;

/** The UDP port a co-op host listens on, and what a join address without a port gets. */
constexpr uint16_t kDefaultCoopPort = 27015;

std::string base64Encode(const uint8_t *data, size_t size);
/** Strict: false for anything but canonical, padded base64. */
bool base64Decode(std::string_view text, std::vector<uint8_t> &out);

} // namespace net

} // namespace reone
