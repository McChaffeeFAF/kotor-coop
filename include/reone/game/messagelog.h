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

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>

namespace reone {

namespace game {

/**
 * Rolling in-game feedback history.
 */
class MessageLog {
public:
    enum class Style : uint8_t {
        Normal = 0,
        Combat = 1,
    };

    struct Entry {
        uint32_t type;
        Style style;
        std::string text;
    };

    static constexpr std::size_t kMaxEntries = 64;
    static constexpr uint32_t kFeedbackMessageType = 0x80;
    /** A conversation line (speaker and text), for the Messages screen's dialog log. */
    static constexpr uint32_t kDialogMessageType = 0x01;

    /** One logged conversation line: "Speaker: text", or the text alone. */
    static std::string dialogLine(const std::string &speaker, const std::string &text);
    void addDialog(const std::string &speaker, const std::string &text);

    void add(uint32_t type, Style style, std::string text);
    void reset() { _entries.clear(); }

    const std::deque<Entry> &entries() const { return _entries; }

private:
    std::deque<Entry> _entries;
};

} // namespace game

} // namespace reone
