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

#include "reone/game/keymap.h"

#include "reone/game/usersettings.h"

#include <cctype>

namespace reone {

namespace game {

using input::KeyCode;

const std::vector<KeyActionInfo> &keyActionInfos() {
    // Fallbacks are K1's shipped defaults; keymap.2da overrides them.
    static const std::vector<KeyActionInfo> infos {
        {KeyAction::ActionUp, "ActionUp", KeyCode::W, KeyCode::W, true},
        {KeyAction::ActionDown, "ActionDown", KeyCode::S, KeyCode::S, true},
        {KeyAction::ActionLeft, "ActionLeft", KeyCode::Z, KeyCode::Z, true},
        {KeyAction::ActionRight, "ActionRight", KeyCode::C, KeyCode::C, true},
        {KeyAction::CameraRotateLeft, "CameraRotateLeft", KeyCode::A, KeyCode::A, true},
        {KeyAction::CameraRotateRight, "CameraRotateRight", KeyCode::D, KeyCode::D, true},
        {KeyAction::SelectPrev, "SelectPrev", KeyCode::Q, KeyCode::Unknown, true},
        {KeyAction::SelectNext, "SelectNext", KeyCode::E, KeyCode::Unknown, true},
        {KeyAction::ChangeChar, "ChangeChar", KeyCode::Tab, KeyCode::Tab, true},
        {KeyAction::PartyActive, "PartyActive", KeyCode::V, KeyCode::Unknown, true},
        {KeyAction::Freelook, "Freelook", KeyCode::CapsLock, KeyCode::Unknown, true},
        {KeyAction::Equip, "Equip", KeyCode::U, KeyCode::Unknown, true},
        {KeyAction::Inventory, "Inventory", KeyCode::I, KeyCode::Unknown, true},
        {KeyAction::Character, "Character", KeyCode::P, KeyCode::Unknown, true},
        {KeyAction::SkillsAndFeats, "SkillsAndFeats", KeyCode::K, KeyCode::Unknown, true},
        {KeyAction::Messages, "Messages", KeyCode::J, KeyCode::Unknown, true},
        {KeyAction::Quests, "Quests", KeyCode::L, KeyCode::Unknown, true},
        {KeyAction::Map, "Map", KeyCode::M, KeyCode::Unknown, true},
        {KeyAction::Options, "Options", KeyCode::O, KeyCode::Unknown, true},
        {KeyAction::Quicksave, "Quicksave", KeyCode::F4, KeyCode::F4, true},
        {KeyAction::Quickload, "QUICKLOAD", KeyCode::F5, KeyCode::Unknown, true},
        {KeyAction::Pause, "Pause", KeyCode::Space, KeyCode::Space, true},
        {KeyAction::PauseKey, "PauseKey", KeyCode::Pause, KeyCode::Unknown, false},
        {KeyAction::Flourish, "Flourish", KeyCode::X, KeyCode::X, true},
        {KeyAction::TargetLeftAct, "TargetLeftAct", KeyCode::Key1, KeyCode::Unknown, true},
        {KeyAction::TargetMiddleAct, "TargetMiddleAct", KeyCode::Key2, KeyCode::Unknown, true},
        {KeyAction::TargetRightAct, "TargetRightAct", KeyCode::Key3, KeyCode::Unknown, true},
        {KeyAction::PersonalPowerAct, "PersonalPowerAct", KeyCode::Key4, KeyCode::Unknown, true},
        {KeyAction::PersonalMedicalAct, "PersonalMedicalAct", KeyCode::Key5, KeyCode::Unknown, true},
        {KeyAction::PersonalOtherAct, "PersonalOtherAct", KeyCode::Key6, KeyCode::Unknown, true},
        {KeyAction::PersonalMinesAct, "PersonalMinesAct", KeyCode::Key7, KeyCode::Unknown, true},
        {KeyAction::DefaultAction, "DefaultAction", KeyCode::R, KeyCode::Unknown, true},
        {KeyAction::CancelCombat, "CancleCombat", KeyCode::F, KeyCode::Unknown, true},
        {KeyAction::ClearOneAction, "ClearOneAction", KeyCode::Y, KeyCode::Unknown, true},
        {KeyAction::Stealth, "STEALTH", KeyCode::G, KeyCode::Unknown, true},
        {KeyAction::GameMenu, "GUI", KeyCode::Escape, KeyCode::Unknown, false},
    };
    return infos;
}

const KeyActionInfo &keyActionInfo(KeyAction action) {
    return keyActionInfos().at(static_cast<size_t>(action));
}

static std::string lowered(const std::string &s) {
    std::string out;
    for (char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::optional<KeyAction> keyActionByName(const std::string &name) {
    auto wanted = lowered(name);
    for (const auto &info : keyActionInfos()) {
        if (lowered(info.name) == wanted) {
            return info.action;
        }
    }
    return std::nullopt;
}

namespace {

struct NamedKey {
    const char *name;
    KeyCode code;
};

// The first name for a key is the one stored and shown.
const NamedKey kNamedKeys[] {
    {"Tab", KeyCode::Tab}, {"Space", KeyCode::Space}, {"Escape", KeyCode::Escape},
    {"Enter", KeyCode::Return}, {"Return", KeyCode::Return}, {"Backspace", KeyCode::Backspace},
    {"CapsLock", KeyCode::CapsLock}, {"CAPTLOCK", KeyCode::CapsLock},
    {"LeftCtrl", KeyCode::LeftControl}, {"RightCtrl", KeyCode::RightControl},
    {"LeftShift", KeyCode::LeftShift}, {"RightShift", KeyCode::RightShift},
    {"LeftAlt", KeyCode::LeftAlt}, {"RightAlt", KeyCode::RightAlt},
    {"Pause", KeyCode::Pause}, {"PauseKey", KeyCode::Pause},
    {"Up", KeyCode::Up}, {"Down", KeyCode::Down}, {"Left", KeyCode::Left}, {"Right", KeyCode::Right},
    {"Insert", KeyCode::Insert}, {"Delete", KeyCode::Delete}, {"Del", KeyCode::Delete},
    {"Home", KeyCode::Home}, {"End", KeyCode::End}, {"PageUp", KeyCode::PageUp}, {"PageDown", KeyCode::PageDown},
    {"Minus", KeyCode::Minus}, {"Equals", KeyCode::Equals}, {"Plus", KeyCode::Plus}, {"+", KeyCode::Plus},
    {"Comma", KeyCode::Comma}, {"Period", KeyCode::Period}, {"Slash", KeyCode::Slash},
    {"Semicolon", KeyCode::Semicolon}, {"Quote", KeyCode::Quote}, {"Backquote", KeyCode::Backquote},
    {"LeftBracket", KeyCode::LeftBracket}, {"RightBracket", KeyCode::RightBracket}, {"Backslash", KeyCode::Backslash},
    {"F1", KeyCode::F1}, {"F2", KeyCode::F2}, {"F3", KeyCode::F3}, {"F4", KeyCode::F4},
    {"F5", KeyCode::F5}, {"F6", KeyCode::F6}, {"F7", KeyCode::F7}, {"F8", KeyCode::F8},
    {"F9", KeyCode::F9}, {"F10", KeyCode::F10}, {"F11", KeyCode::F11}, {"F12", KeyCode::F12},
};

} // namespace

KeyCode parseKeyName(const std::string &name) {
    if (name.size() == 1) {
        char c = static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            return static_cast<KeyCode>(c);
        }
    }
    auto wanted = lowered(name);
    for (const auto &key : kNamedKeys) {
        if (lowered(key.name) == wanted) {
            return key.code;
        }
    }
    return KeyCode::Unknown;
}

std::string keyName(KeyCode code) {
    int value = static_cast<int>(code);
    if ((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9')) {
        return std::string(1, static_cast<char>(std::toupper(value)));
    }
    for (const auto &key : kNamedKeys) {
        if (key.code == code) {
            return key.name;
        }
    }
    return code == KeyCode::Unknown ? "None" : std::to_string(value);
}

Keymap::Keymap() {
    loadDefaults({});
}

void Keymap::loadDefaults(const std::vector<std::pair<std::string, std::string>> &rows) {
    const auto &infos = keyActionInfos();
    _defaults.assign(infos.size(), KeyCode::Unknown);
    for (const auto &info : infos) {
        _defaults[static_cast<size_t>(info.action)] = info.fallback;
    }
    for (const auto &[name, character] : rows) {
        auto action = keyActionByName(name);
        auto code = parseKeyName(character);
        if (action && code != KeyCode::Unknown) {
            _defaults[static_cast<size_t>(*action)] = code;
        }
    }
    _keys = _defaults;
}

void Keymap::resetToDefaults() {
    _keys = _defaults;
}

void Keymap::applySettings(const UserSettings &settings) {
    for (const auto &info : keyActionInfos()) {
        if (!info.remappable) {
            continue;
        }
        auto value = settings.get(std::string("key.") + info.name);
        if (!value) {
            continue;
        }
        auto code = parseKeyName(*value);
        if (code != KeyCode::Unknown || lowered(*value) == "none") {
            bind(info.action, code);
        }
    }
}

void Keymap::storeSettings(UserSettings &settings) const {
    for (const auto &info : keyActionInfos()) {
        std::string key = std::string("key.") + info.name;
        size_t index = static_cast<size_t>(info.action);
        if (!info.remappable || _keys[index] == _defaults[index]) {
            settings.erase(key);
        } else {
            settings.set(key, keyName(_keys[index]));
        }
    }
}

std::optional<KeyAction> Keymap::actionFor(KeyCode code) const {
    if (code == KeyCode::Unknown) {
        return std::nullopt;
    }
    for (size_t i = 0; i < _keys.size(); ++i) {
        if (_keys[i] == code) {
            return static_cast<KeyAction>(i);
        }
    }
    return std::nullopt;
}

void Keymap::bind(KeyAction action, KeyCode code) {
    size_t index = static_cast<size_t>(action);
    if (!keyActionInfo(action).remappable) {
        return;
    }
    if (code != KeyCode::Unknown) {
        for (size_t i = 0; i < _keys.size(); ++i) {
            if (i != index && _keys[i] == code && !keyActionInfos()[i].remappable) {
                return; // Escape and Pause stay put
            }
        }
    }
    KeyCode old = _keys[index];
    for (size_t i = 0; i < _keys.size(); ++i) {
        if (i != index && _keys[i] == code && code != KeyCode::Unknown) {
            _keys[i] = old;
        }
    }
    _keys[index] = code;
}

Keymap::Translation Keymap::translate(KeyCode pressed) const {
    Translation result;
    if (auto action = actionFor(pressed)) {
        const auto &info = keyActionInfo(*action);
        if (info.engineKey != KeyCode::Unknown) {
            result.forward = info.engineKey;
        } else {
            result.dispatch = action;
        }
        return result;
    }
    // An engine key whose action now lives on another key does nothing.
    for (const auto &info : keyActionInfos()) {
        if (info.engineKey == pressed) {
            return result;
        }
    }
    result.forward = pressed;
    return result;
}

} // namespace game

} // namespace reone
