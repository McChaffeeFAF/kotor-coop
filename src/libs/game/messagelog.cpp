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

#include "reone/game/messagelog.h"

namespace reone {

namespace game {

std::string MessageLog::dialogLine(const std::string &speaker, const std::string &text) {
    return speaker.empty() ? text : speaker + ": " + text;
}

// Whitespace: a node with only these says nothing.
static const std::string kBlank {' ', '\t', '\r', '\n'};

void MessageLog::addDialog(const std::string &speaker, const std::string &text) {
    if (text.find_first_not_of(kBlank) == std::string::npos) {
        return; // a silent node (camera or script only)
    }
    add(kDialogMessageType, Style::Normal, dialogLine(speaker, text));
}

void MessageLog::add(uint32_t type, Style style, std::string text) {
    while (_entries.size() >= kMaxEntries) {
        _entries.pop_front();
    }
    _entries.push_back({type, style, std::move(text)});
}

} // namespace game

} // namespace reone
