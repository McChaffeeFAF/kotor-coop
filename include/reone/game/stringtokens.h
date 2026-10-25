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

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace reone {

namespace resource {
class TwoDA;
}

namespace game {

// Text tokens such as <FullName> and <he/she> that dialog and talk-table
// text carry (design note p1_game "Dialog tokens").
struct StringTokenRow {
    int actionCode {-1};
    int defaultStrRef {-1};
    std::array<int, 4> strRefs {-1, -1, -1, -1};
};

// What the tokens describe: the player character, when there is one.
struct StringTokenSubject {
    bool present {false};
    std::string fullName;
    std::string firstName;
    std::string lastName;
    std::string race;
    std::string raceLower;
    std::string subrace;
    std::string className;
    std::string classLower;
    int level {0};
    int gender {0};
    int goodEvil {50};
    std::optional<int> hour; // 0..23 game hour
    std::optional<int> year;  // calendar year (design note c11_data, "Clock")
    std::optional<int> month; // calendar month, 1..12
    int quarterDay {0};       // 0 outside dawn..dusk, else 1..3
    // Name of the key bound to keymap.2da row "action<row>", with the suffix
    // "a" (direction 1) or "b" (direction 2) for a two-way binding.
    std::function<std::string(int row, int direction)> keyName;
    // TSL: the mouse-button names (token action 26) are other talk-table rows.
    bool tsl {false};
    // TSL: the Nar Shaddaa bounty on the player, in credits (token action 27).
    std::optional<int> bounty;
};

inline constexpr const char *kUnrecognizedToken = "<UNRECOGNIZED TOKEN>";

class StringTokens {
public:
    void load(const resource::TwoDA &table);
    void add(std::string token, StringTokenRow row) { _rows[std::move(token)] = row; }
    bool empty() const { return _rows.empty(); }

    /**
     * Replace every <token> in text. "<<" is a literal '<'. <CUSTOMn> takes
     * the custom-token value; a named token takes its stringtokens row. A
     * token with no value becomes kUnrecognizedToken.
     */
    std::string resolve(const std::string &text,
                        const std::map<int, std::string> &custom,
                        const StringTokenSubject &subject,
                        const std::function<std::string(int)> &strRefText) const;

private:
    std::map<std::string, StringTokenRow> _rows;

    std::string value(const StringTokenRow &row,
                      const StringTokenSubject &subject,
                      const std::function<std::string(int)> &strRefText) const;
};

} // namespace game

} // namespace reone
