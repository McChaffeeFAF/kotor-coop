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

#include "reone/game/padinput.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "reone/game/usersettings.h"

namespace reone {

namespace game {

using input::GamepadAxis;
using input::GamepadButton;
using input::KeyCode;
using input::NavigateAction;

namespace {

struct NamedButton {
    const char *name;
    GamepadButton button;
};

// The first name for a button is the one stored and shown.
const NamedButton kButtons[] {
    {"A", GamepadButton::A},
    {"B", GamepadButton::B},
    {"X", GamepadButton::X},
    {"Y", GamepadButton::Y},
    {"Back", GamepadButton::Back},
    {"Select", GamepadButton::Back},
    {"Guide", GamepadButton::Guide},
    {"Start", GamepadButton::Start},
    {"LeftStick", GamepadButton::LeftStick},
    {"L3", GamepadButton::LeftStick},
    {"RightStick", GamepadButton::RightStick},
    {"R3", GamepadButton::RightStick},
    {"LeftBumper", GamepadButton::LeftShoulder},
    {"LB", GamepadButton::LeftShoulder},
    {"RightBumper", GamepadButton::RightShoulder},
    {"RB", GamepadButton::RightShoulder},
    {"DPadUp", GamepadButton::DPadUp},
    {"DPadDown", GamepadButton::DPadDown},
    {"DPadLeft", GamepadButton::DPadLeft},
    {"DPadRight", GamepadButton::DPadRight},
    {"LeftTrigger", GamepadButton::LeftTrigger},
    {"LT", GamepadButton::LeftTrigger},
    {"RightTrigger", GamepadButton::RightTrigger},
    {"RT", GamepadButton::RightTrigger},
};

std::string lowered(const std::string &s) {
    std::string out;
    for (char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

struct DefaultPadBinding {
    GamepadButton button;
    KeyAction action;
};

// The shipped layout: A acts, B cancels, X pauses, Y opens the map; the bumpers cycle
// targets, the triggers switch the party leader and solo mode; the D-pad is the quick slots.
const DefaultPadBinding kDefaultPad[] {
    {GamepadButton::A, KeyAction::DefaultAction},
    {GamepadButton::B, KeyAction::CancelCombat},
    {GamepadButton::X, KeyAction::Pause},
    {GamepadButton::Y, KeyAction::Map},
    {GamepadButton::LeftShoulder, KeyAction::SelectPrev},
    {GamepadButton::RightShoulder, KeyAction::SelectNext},
    {GamepadButton::LeftTrigger, KeyAction::ChangeChar},
    {GamepadButton::RightTrigger, KeyAction::PartyActive},
    {GamepadButton::DPadLeft, KeyAction::TargetLeftAct},
    {GamepadButton::DPadUp, KeyAction::TargetMiddleAct},
    {GamepadButton::DPadRight, KeyAction::TargetRightAct},
    {GamepadButton::DPadDown, KeyAction::PersonalPowerAct},
    {GamepadButton::Back, KeyAction::Quests},
    {GamepadButton::LeftStick, KeyAction::Stealth},
    {GamepadButton::RightStick, KeyAction::Flourish},
};

constexpr float kPressTravel = 0.5f;   // a stick or trigger counts as pressed past this
constexpr float kTriggerRelease = 0.3f;

int releaseToken(GamepadButton button) {
    return 100 + static_cast<int>(button);
}

int stickIndex(GamepadAxis axis) {
    switch (axis) {
    case GamepadAxis::LeftX:
        return 0;
    case GamepadAxis::LeftY:
        return 1;
    case GamepadAxis::RightX:
        return 2;
    case GamepadAxis::RightY:
        return 3;
    default:
        return -1;
    }
}

std::optional<NavigateAction> dpadNavigate(GamepadButton button) {
    switch (button) {
    case GamepadButton::DPadUp:
        return NavigateAction::Up;
    case GamepadButton::DPadDown:
        return NavigateAction::Down;
    case GamepadButton::DPadLeft:
        return NavigateAction::Left;
    case GamepadButton::DPadRight:
        return NavigateAction::Right;
    default:
        return std::nullopt;
    }
}

KeyCode arrowKey(NavigateAction action) {
    switch (action) {
    case NavigateAction::Up:
        return KeyCode::Up;
    case NavigateAction::Down:
        return KeyCode::Down;
    case NavigateAction::Left:
        return KeyCode::Left;
    default:
        return KeyCode::Right;
    }
}

} // namespace

std::string gamepadButtonName(GamepadButton button) {
    for (const auto &named : kButtons) {
        if (named.button == button) {
            return named.name;
        }
    }
    return "None";
}

std::optional<GamepadButton> parseGamepadButton(const std::string &name) {
    auto wanted = lowered(name);
    for (const auto &named : kButtons) {
        if (lowered(named.name) == wanted) {
            return named.button;
        }
    }
    return std::nullopt;
}

PadBindings::PadBindings() {
    _defaults.assign(static_cast<size_t>(KeyAction::Count), std::nullopt);
    for (const auto &binding : kDefaultPad) {
        _defaults[static_cast<size_t>(binding.action)] = binding.button;
    }
    _buttons = _defaults;
}

void PadBindings::resetToDefaults() {
    _buttons = _defaults;
}

bool PadBindings::reserved(GamepadButton button) {
    return button == GamepadButton::Start || button == GamepadButton::Guide;
}

bool PadBindings::bindable(KeyAction action) {
    return keyActionInfo(action).remappable;
}

std::optional<KeyAction> PadBindings::actionFor(GamepadButton button) const {
    for (size_t i = 0; i < _buttons.size(); ++i) {
        if (_buttons[i] && *_buttons[i] == button) {
            return static_cast<KeyAction>(i);
        }
    }
    return std::nullopt;
}

std::optional<KeyAction> PadBindings::conflict(KeyAction action, GamepadButton button) const {
    auto holder = actionFor(button);
    if (holder && *holder != action) {
        return holder;
    }
    return std::nullopt;
}

bool PadBindings::bind(KeyAction action, GamepadButton button) {
    if (reserved(button) || !bindable(action)) {
        return false;
    }
    size_t index = static_cast<size_t>(action);
    auto old = _buttons[index];
    if (auto holder = conflict(action, button)) {
        _buttons[static_cast<size_t>(*holder)] = old;
    }
    _buttons[index] = button;
    return true;
}

void PadBindings::unbind(KeyAction action) {
    if (bindable(action)) {
        _buttons[static_cast<size_t>(action)].reset();
    }
}

void PadBindings::applySettings(const UserSettings &settings) {
    for (const auto &info : keyActionInfos()) {
        if (!info.remappable) {
            continue;
        }
        auto value = settings.get(std::string("pad.") + info.name);
        if (!value) {
            continue;
        }
        if (lowered(*value) == "none") {
            unbind(info.action);
        } else if (auto button = parseGamepadButton(*value)) {
            bind(info.action, *button);
        }
    }
}

void PadBindings::storeSettings(UserSettings &settings) const {
    for (const auto &info : keyActionInfos()) {
        std::string key = std::string("pad.") + info.name;
        size_t index = static_cast<size_t>(info.action);
        if (!info.remappable || _buttons[index] == _defaults[index]) {
            settings.erase(key);
        } else {
            settings.set(key, _buttons[index] ? gamepadButtonName(*_buttons[index]) : "None");
        }
    }
}

void PadInput::setDeadZone(float deadZone) {
    _deadZone = std::clamp(deadZone, 0.0f, 0.9f);
}

std::vector<PadOutput> PadInput::releaseAll() {
    std::vector<PadOutput> out;
    for (auto &[token, release] : _releases) {
        out.push_back(release);
    }
    _releases.clear();
    _held.clear();
    _stickDirections.clear();
    _repeating.reset();
    return out;
}

void PadInput::switchMode(PadMode mode, std::vector<PadOutput> &out) {
    if (_mode && *_mode != mode) {
        auto released = releaseAll();
        out.insert(out.end(), released.begin(), released.end());
    }
    _mode = mode;
}

void PadInput::startRepeat(NavigateAction action) {
    _repeating = action;
    _repeatTimer = kRepeatDelay;
}

void PadInput::press(GamepadButton button, PadMode mode, std::vector<PadOutput> &out) {
    int token = releaseToken(button);
    auto key = [&](KeyCode code) {
        out.push_back(PadOutput::forKey(code, true));
        _releases[token] = PadOutput::forKey(code, false);
    };
    if (button == GamepadButton::Start || (button == GamepadButton::B && mode != PadMode::World)) {
        key(KeyCode::Escape);
        return;
    }
    switch (mode) {
    case PadMode::None:
        return;
    case PadMode::World:
        if (auto action = _bindings.actionFor(button)) {
            out.push_back(PadOutput::forAction(*action, true));
            _releases[token] = PadOutput::forAction(*action, false);
        }
        return;
    case PadMode::Minigame:
        if (button == GamepadButton::A || button == GamepadButton::RightTrigger) {
            key(KeyCode::Space);
        } else if (auto direction = dpadNavigate(button)) {
            key(arrowKey(*direction));
        }
        return;
    case PadMode::Menu:
        if (button == GamepadButton::A) {
            out.push_back(PadOutput::forNavigate(NavigateAction::Accept));
        } else if (auto direction = dpadNavigate(button)) {
            out.push_back(PadOutput::forNavigate(*direction));
            startRepeat(*direction);
        }
        return;
    }
}

void PadInput::release(GamepadButton button, PadMode mode, std::vector<PadOutput> &out) {
    auto it = _releases.find(releaseToken(button));
    if (it != _releases.end()) {
        out.push_back(it->second);
        _releases.erase(it);
    }
    if (mode == PadMode::Menu) {
        if (auto direction = dpadNavigate(button); direction && _repeating == direction) {
            _repeating.reset();
        }
    }
}

void PadInput::stickDirection(int direction, bool pressed, PadMode mode, std::vector<PadOutput> &out) {
    int stick = direction / 2;
    bool positive = direction % 2 == 1;
    if (!pressed) {
        auto it = _releases.find(direction);
        if (it != _releases.end()) {
            out.push_back(it->second);
            _releases.erase(it);
        }
        // A menu direction stops repeating when its stick lets go.
        if (mode == PadMode::Menu && stick == 1 && _repeating) {
            NavigateAction was = positive ? NavigateAction::Down : NavigateAction::Up;
            if (_repeating == was) {
                _repeating.reset();
            }
        }
        if (mode == PadMode::Menu && stick == 0 && _repeating) {
            NavigateAction was = positive ? NavigateAction::Right : NavigateAction::Left;
            if (_repeating == was) {
                _repeating.reset();
            }
        }
        return;
    }
    std::optional<NavigateAction> nav;
    if (stick == 0) {
        nav = positive ? NavigateAction::Right : NavigateAction::Left;
    } else if (stick == 1) {
        nav = positive ? NavigateAction::Down : NavigateAction::Up;
    }
    switch (mode) {
    case PadMode::World: {
        std::optional<KeyAction> action;
        switch (stick) {
        case 0:
            action = positive ? KeyAction::ActionRight : KeyAction::ActionLeft;
            break;
        case 1:
            action = positive ? KeyAction::ActionDown : KeyAction::ActionUp;
            break;
        case 2:
            action = positive ? KeyAction::CameraRotateRight : KeyAction::CameraRotateLeft;
            break;
        default:
            break;
        }
        if (action) {
            out.push_back(PadOutput::forAction(*action, true));
            _releases[direction] = PadOutput::forAction(*action, false);
        }
        break;
    }
    case PadMode::Minigame:
        if (nav) {
            out.push_back(PadOutput::forKey(arrowKey(*nav), true));
            _releases[direction] = PadOutput::forKey(arrowKey(*nav), false);
        }
        break;
    case PadMode::Menu:
        if (nav) {
            out.push_back(PadOutput::forNavigate(*nav));
            startRepeat(*nav);
        }
        break;
    case PadMode::None:
        break;
    }
}

void PadInput::axis(GamepadAxis which, float value, PadMode mode, std::vector<PadOutput> &out) {
    if (which == GamepadAxis::LeftTrigger || which == GamepadAxis::RightTrigger) {
        auto button = which == GamepadAxis::LeftTrigger ? GamepadButton::LeftTrigger : GamepadButton::RightTrigger;
        bool held = _held.count(button) > 0;
        if (!held && value >= kPressTravel) {
            _held.insert(button);
            press(button, mode, out);
        } else if (held && value <= kTriggerRelease) {
            _held.erase(button);
            release(button, mode, out);
        }
        return;
    }
    int stick = stickIndex(which);
    if (stick < 0) {
        return;
    }
    float press = std::max(kPressTravel, _deadZone + 0.05f);
    for (int sign = 0; sign < 2; ++sign) {
        int direction = stick * 2 + sign;
        float signed_ = sign == 1 ? value : -value;
        bool held = _stickDirections.count(direction) > 0;
        if (!held && signed_ >= press) {
            _stickDirections.insert(direction);
            stickDirection(direction, true, mode, out);
        } else if (held && signed_ < _deadZone) {
            _stickDirections.erase(direction);
            stickDirection(direction, false, mode, out);
        }
    }
}

std::vector<PadOutput> PadInput::handle(const input::Event &event, PadMode mode) {
    std::vector<PadOutput> out;
    if (!event.isGamepad()) {
        return out;
    }
    if (event.type == input::EventType::GamepadDevice) {
        uint32_t pad = event.padDevice.pad;
        auto known = std::find(_pads.begin(), _pads.end(), pad);
        if (event.padDevice.connected) {
            if (known == _pads.end()) {
                _pads.push_back(pad);
            }
            if (!_active) {
                _active = pad;
            }
        } else if (known != _pads.end()) {
            _pads.erase(known);
            if (_active == pad) {
                out = releaseAll(); // the pad that was held is gone: nothing stays pressed
                _active = _pads.empty() ? std::nullopt : std::optional<uint32_t>(_pads.front());
            }
        }
        return out;
    }
    uint32_t pad = event.type == input::EventType::GamepadAxisMotion ? event.padAxis.pad : event.padButton.pad;
    if (!_active) {
        // A pad that never announced itself: take it.
        _active = pad;
        _pads.push_back(pad);
    }
    if (pad != *_active) {
        return out; // the first pad drives the local player
    }
    switchMode(mode, out);
    switch (event.type) {
    case input::EventType::GamepadButtonDown:
        if (_held.insert(event.padButton.button).second) {
            press(event.padButton.button, mode, out);
        }
        break;
    case input::EventType::GamepadButtonUp:
        if (_held.erase(event.padButton.button)) {
            release(event.padButton.button, mode, out);
        }
        break;
    case input::EventType::GamepadAxisMotion: {
        float value = std::isfinite(event.padAxis.value) ? std::clamp(event.padAxis.value, -1.0f, 1.0f) : 0.0f;
        axis(event.padAxis.axis, value, mode, out);
        break;
    }
    default:
        break;
    }
    return out;
}

std::vector<PadOutput> PadInput::update(float dt, PadMode mode) {
    std::vector<PadOutput> out;
    switchMode(mode, out);
    if (_repeating && mode == PadMode::Menu && dt > 0.0f) {
        _repeatTimer -= dt;
        while (_repeatTimer <= 0.0f) {
            out.push_back(PadOutput::forNavigate(*_repeating));
            _repeatTimer += kRepeatInterval;
        }
    }
    return out;
}

} // namespace game

} // namespace reone
