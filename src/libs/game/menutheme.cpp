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

#include "reone/game/menutheme.h"

#include <algorithm>

namespace reone {

namespace game {

std::optional<MenuThemeMode> parseMenuThemeMode(std::string_view value) {
    if (value == "rotate") return MenuThemeMode::Rotate;
    if (value == "k1") return MenuThemeMode::K1Only;
    if (value == "k2") return MenuThemeMode::K2Only;
    return std::nullopt;
}

const char *menuThemeModeName(MenuThemeMode mode) {
    switch (mode) {
    case MenuThemeMode::K1Only:
        return "k1";
    case MenuThemeMode::K2Only:
        return "k2";
    case MenuThemeMode::Rotate:
    default:
        return "rotate";
    }
}

std::optional<MenuTheme> parseMenuTheme(std::string_view value) {
    if (value == "k1") return MenuTheme::K1;
    if (value == "k2") return MenuTheme::K2;
    return std::nullopt;
}

const char *menuThemeName(MenuTheme theme) {
    return theme == MenuTheme::K2 ? "k2" : "k1";
}

MenuTheme nextMenuTheme(MenuThemeMode mode, bool k2Available, std::optional<MenuTheme> last) {
    if (!k2Available) {
        return MenuTheme::K1;
    }
    switch (mode) {
    case MenuThemeMode::K1Only:
        return MenuTheme::K1;
    case MenuThemeMode::K2Only:
        return MenuTheme::K2;
    case MenuThemeMode::Rotate:
    default:
        // The first show ever is K1's; after that, the other one.
        return last == MenuTheme::K1 ? MenuTheme::K2 : MenuTheme::K1;
    }
}

bool MenuThemeFade::update(float dt, bool canRotate) {
    switch (_phase) {
    case Phase::None:
        if (!canRotate) {
            _idle = 0.0f;
            return false;
        }
        _idle += dt;
        if (_idle >= kIdleSeconds) {
            _phase = Phase::Out;
            _time = 0.0f;
        }
        return false;
    case Phase::Out:
        _time += dt;
        if (_time >= kFadeSeconds) {
            _phase = Phase::None;
            _idle = 0.0f;
            return true;
        }
        return false;
    case Phase::In:
        _time += dt;
        if (_time >= kFadeSeconds) {
            _phase = Phase::None;
            _idle = 0.0f;
        }
        return false;
    }
    return false;
}

void MenuThemeFade::beginFadeIn() {
    _phase = Phase::In;
    _time = 0.0f;
    _idle = 0.0f;
}

float MenuThemeFade::alpha() const {
    float t = std::clamp(_time / kFadeSeconds, 0.0f, 1.0f);
    switch (_phase) {
    case Phase::Out:
        return t;
    case Phase::In:
        return 1.0f - t;
    case Phase::None:
    default:
        return 0.0f;
    }
}

const std::vector<MenuLineupEntry> &defaultMenuLineup() {
    static const std::vector<MenuLineupEntry> kLineup {
        {"Bastila", MenuTheme::K1, "Party_NPC_Bastila"},
        {"Carth", MenuTheme::K1, "Party_NPC_Carth"},
        {"HK-47", MenuTheme::K1, "Party_NPC_HK47"},
        {"Kreia", MenuTheme::K2, "Party_NPC_Kreia"},
        {"Atton", MenuTheme::K2, "Party_NPC_Atton"},
        {"Bao-Dur", MenuTheme::K2, "Party_NPC_BaoDur"},
        {"Visas", MenuTheme::K2, "Party_NPC_Visas"},
        {"Revan", MenuTheme::K1, "Unique_Darth_Revan", MenuLineupGroup::Villain},
        {"Malak", MenuTheme::K1, "Unique_Darth_Malak", MenuLineupGroup::Villain},
        {"Darth Sion", MenuTheme::K2, "Unique_Darth_Sion", MenuLineupGroup::Villain},
        {"Darth Nihilus", MenuTheme::K2, "Unique_Darth_Nihilus", MenuLineupGroup::Villain},
    };
    return kLineup;
}

static std::string trimmed(std::string_view s) {
    auto begin = s.find_first_not_of(" \t\r");
    if (begin == std::string_view::npos) {
        return "";
    }
    auto end = s.find_last_not_of(" \t\r");
    return std::string(s.substr(begin, end - begin + 1));
}

std::vector<MenuLineupEntry> parseMenuLineup(std::istream &input) {
    std::vector<MenuLineupEntry> result;
    for (std::string line; std::getline(input, line);) {
        auto text = trimmed(line);
        if (text.empty() || text.front() == '#') {
            continue;
        }
        std::vector<std::string> fields;
        std::string_view rest(text);
        while (true) {
            auto comma = rest.find(',');
            fields.push_back(trimmed(rest.substr(0, comma)));
            if (comma == std::string_view::npos) {
                break;
            }
            rest.remove_prefix(comma + 1);
        }
        if ((fields.size() != 3 && fields.size() != 4) || fields[0].empty() || fields[2].empty()) {
            continue;
        }
        auto source = parseMenuTheme(fields[1]);
        if (!source) {
            continue;
        }
        auto group = MenuLineupGroup::Party;
        if (fields.size() == 4) {
            if (fields[3] == "villain") {
                group = MenuLineupGroup::Villain;
            } else if (fields[3] != "party") {
                continue;
            }
        }
        result.push_back({fields[0], *source, fields[2], group});
    }
    return result;
}

std::optional<size_t> nextMenuCharacter(const std::vector<MenuLineupEntry> &entries,
                                        bool k2Available,
                                        const std::string &last) {
    std::vector<size_t> eligible;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].source != MenuTheme::K2 || k2Available) {
            eligible.push_back(i);
        }
    }
    if (eligible.empty()) {
        return std::nullopt;
    }
    for (size_t i = 0; i < eligible.size(); ++i) {
        if (entries[eligible[i]].name == last) {
            return eligible[(i + 1) % eligible.size()];
        }
    }
    return eligible.front();
}

std::vector<size_t> pickMenuLineup(const std::vector<MenuLineupEntry> &entries,
                                   bool k2Available,
                                   size_t count,
                                   const std::function<int(int, int)> &randomInt) {
    auto eligible = [&](const MenuLineupEntry &entry) {
        return entry.source != MenuTheme::K2 || k2Available;
    };
    // One group per show: party members stand with party members, villains
    // with villains.
    std::vector<MenuLineupGroup> groups;
    for (auto group : {MenuLineupGroup::Party, MenuLineupGroup::Villain}) {
        if (std::any_of(entries.begin(), entries.end(), [&](const MenuLineupEntry &entry) {
                return entry.group == group && eligible(entry);
            })) {
            groups.push_back(group);
        }
    }
    if (groups.empty()) {
        return {};
    }
    auto group = groups[groups.size() == 1 ? 0 : randomInt(0, static_cast<int>(groups.size()) - 1)];

    std::vector<size_t> pools[2];
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].group != group || !eligible(entries[i])) {
            continue;
        }
        pools[entries[i].source == MenuTheme::K2 ? 1 : 0].push_back(i);
    }
    std::vector<size_t> result;
    // Alternate the two games, starting with a random one, until the count is
    // reached or both pools are empty.
    int turn = 0;
    if (!pools[0].empty() && !pools[1].empty()) {
        turn = randomInt(0, 1);
    } else if (pools[0].empty()) {
        turn = 1;
    }
    while (result.size() < count && (!pools[0].empty() || !pools[1].empty())) {
        auto &pool = pools[turn].empty() ? pools[1 - turn] : pools[turn];
        int index = randomInt(0, static_cast<int>(pool.size()) - 1);
        result.push_back(pool[index]);
        pool.erase(pool.begin() + index);
        turn = 1 - turn;
    }
    return result;
}

} // namespace game

} // namespace reone
