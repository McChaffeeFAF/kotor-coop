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

#include "reone/game/stringtokens.h"

#include "reone/resource/2da.h"

namespace reone {

namespace game {

void StringTokens::load(const resource::TwoDA &table) {
    _rows.clear();
    static const char *kStrRefColumns[] = {"strref1", "strref2", "strref3", "strref4"};
    for (int row = 0; row < table.getRowCount(); ++row) {
        std::string token = table.getString(row, "token");
        if (token.empty()) {
            continue;
        }
        StringTokenRow entry;
        entry.actionCode = table.getInt(row, "actioncode", -1);
        entry.defaultStrRef = table.getInt(row, "default", -1);
        for (size_t i = 0; i < entry.strRefs.size(); ++i) {
            entry.strRefs[i] = table.getInt(row, kStrRefColumns[i], -1);
        }
        _rows[std::move(token)] = entry;
    }
}

static std::string trimSpaces(std::string s) {
    size_t first = s.find_first_not_of(' ');
    if (first == std::string::npos) {
        return "";
    }
    size_t last = s.find_last_not_of(' ');
    return s.substr(first, last - first + 1);
}

std::string StringTokens::value(const StringTokenRow &row,
                                const StringTokenSubject &subject,
                                const std::function<std::string(int)> &strRefText) const {
    auto text = [&](int strRef) { return strRef >= 0 && strRefText ? strRefText(strRef) : std::string(); };
    switch (row.actionCode) {
    case 0: // deity: a fixed string
        return text(row.strRefs[0]);
    case 1: { // gendered word: one string per gender
        int gender = subject.gender >= 0 && subject.gender < 4 ? subject.gender : 0;
        return text(row.strRefs[gender]);
    }
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 14:
    case 23:
        if (!subject.present) {
            return text(row.defaultStrRef);
        }
        switch (row.actionCode) {
        case 2:
        case 23:
            return subject.fullName;
        case 3:
            return subject.firstName;
        case 4:
            return subject.lastName;
        case 5:
            return subject.race;
        case 6:
            return subject.raceLower;
        case 7:
            return subject.subrace;
        case 8:
            return subject.className;
        case 9:
            return subject.classLower;
        case 10:
            return std::to_string(subject.level);
        case 14: {
            // Good at 60 and above, evil below 41, neutral between.
            int index = subject.goodEvil >= 60 ? 0 : (subject.goodEvil < 41 ? 2 : 1);
            return text(row.strRefs[index]);
        }
        default:
            // Alignment words: K1 ships no strings for them.
            return "";
        }
    case 16: // game hour on a 12-hour clock
        if (!subject.hour) {
            return "";
        }
        return std::to_string((*subject.hour + 11) % 12 + 1);
    case 15: // calendar year
        return subject.year ? std::to_string(*subject.year) : std::string();
    case 17: // calendar month
        return subject.month ? std::to_string(*subject.month) : std::string();
    case 18: // quarter of the day, as the row's string (K1 ships none)
        return subject.quarterDay >= 0 && subject.quarterDay < 4 ? text(row.strRefs[subject.quarterDay]) : std::string();
    case 25: // the key bound to a keymap.2da action
        return subject.keyName ? subject.keyName(row.defaultStrRef, row.strRefs[0]) : std::string();
    default:
        // Highlight markers carry no text on PC; the button codes are Xbox
        // font glyphs and the mouse-button names are not used by K1 text.
        return "";
    }
}

std::string StringTokens::resolve(const std::string &text,
                                  const std::map<int, std::string> &custom,
                                  const StringTokenSubject &subject,
                                  const std::function<std::string(int)> &strRefText) const {
    if (text.find('<') == std::string::npos) {
        return text;
    }
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        char c = text[i];
        if (c != '<') {
            out += c;
            ++i;
            continue;
        }
        if (i + 1 < text.size() && text[i + 1] == '<') {
            out += '<';
            i += 2;
            continue;
        }
        size_t close = text.find('>', i + 1);
        if (close == std::string::npos) {
            out += kUnrecognizedToken;
            break;
        }
        std::string name = text.substr(i + 1, close - i - 1);
        i = close + 1;
        std::optional<std::string> resolved;
        if (name.size() >= 7 && name.compare(0, 6, "CUSTOM") == 0) {
            bool digits = true;
            int number = 0;
            for (size_t d = 6; d < name.size() && digits; ++d) {
                if (name[d] < '0' || name[d] > '9' || number > 100000000) {
                    digits = false;
                } else {
                    number = number * 10 + (name[d] - '0');
                }
            }
            if (digits) {
                auto it = custom.find(number);
                if (it != custom.end()) {
                    resolved = it->second;
                }
            }
        } else if (!name.empty()) {
            auto it = _rows.find(name);
            if (it != _rows.end()) {
                resolved = trimSpaces(value(it->second, subject, strRefText));
            }
        }
        out += resolved ? *resolved : std::string(kUnrecognizedToken);
    }
    return out;
}

} // namespace game

} // namespace reone
