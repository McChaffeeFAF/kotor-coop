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

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "reone/input/event.h"

#include "keymap.h"

namespace reone {

namespace game {

class UserSettings;

// Buttons

/** A stable, readable button name (stored in settings, shown on the controls page). */
std::string gamepadButtonName(input::GamepadButton button);
/** Parse a button name (case-insensitive; also a few common aliases); nullopt if it names none. */
std::optional<input::GamepadButton> parseGamepadButton(const std::string &name);

/**
 * Which pad button performs which in-world action (the same KeyActions the keyboard has).
 * Defaults put the common actions on the face buttons, shoulders, triggers and D-pad; they
 * are bindings like any other, loaded from and saved to the player's settings as
 * "pad.<Action>=<Button>" next to the keyboard's "key.<Action>". Start always opens the game
 * menu and the sticks always move and turn the camera, so those are not bindable.
 */
class PadBindings {
public:
    PadBindings();

    void resetToDefaults();

    std::optional<input::GamepadButton> buttonFor(KeyAction action) const { return _buttons[static_cast<size_t>(action)]; }
    std::optional<input::GamepadButton> defaultButtonFor(KeyAction action) const { return _defaults[static_cast<size_t>(action)]; }
    std::optional<KeyAction> actionFor(input::GamepadButton button) const;

    /** Buttons the player cannot rebind. */
    static bool reserved(input::GamepadButton button);
    /** Whether the action takes a pad button at all (the remappable ones). */
    static bool bindable(KeyAction action);

    /** The action that holds this button now, if it is not `action` itself. */
    std::optional<KeyAction> conflict(KeyAction action, input::GamepadButton button) const;

    /**
     * Bind a button to an action. An action that held the button takes this action's old button (a swap), as
     * for keys. False, and nothing changes, for a reserved button or an action that takes none.
     */
    bool bind(KeyAction action, input::GamepadButton button);
    void unbind(KeyAction action);

    /** Player bindings stored as "pad.<Name>=<button>"; `none` for an unbound action. */
    void applySettings(const UserSettings &settings);
    void storeSettings(UserSettings &settings) const;

private:
    std::vector<std::optional<input::GamepadButton>> _buttons;
    std::vector<std::optional<input::GamepadButton>> _defaults;
};

// Input

/** What the pad drives right now. */
enum class PadMode {
    None,     /**< a cutscene or a movie: only Back out and Start */
    World,    /**< the party in the world: stick moves, the other stick turns the camera, buttons are actions */
    Minigame, /**< a swoop race or a turret: arrows and fire */
    Menu      /**< any screen: focus moves, A uses, B backs out */
};

/** One thing the game should do because of the pad. */
struct PadOutput {
    enum class Kind {
        Action,  /**< a KeyAction pressed or released: the game does it through the keymap, as the bound key would */
        Key,     /**< a plain key pressed or released (Escape, an arrow, Space) */
        Navigate /**< a menu direction or accept */
    };
    Kind kind {Kind::Action};
    bool pressed {true};
    KeyAction action {KeyAction::ActionUp};
    input::KeyCode key {input::KeyCode::Unknown};
    input::NavigateAction navigate {input::NavigateAction::Accept};

    static PadOutput forAction(KeyAction action, bool pressed) {
        PadOutput out;
        out.kind = Kind::Action;
        out.action = action;
        out.pressed = pressed;
        return out;
    }
    static PadOutput forKey(input::KeyCode key, bool pressed) {
        PadOutput out;
        out.kind = Kind::Key;
        out.key = key;
        out.pressed = pressed;
        return out;
    }
    static PadOutput forNavigate(input::NavigateAction action) {
        PadOutput out;
        out.kind = Kind::Navigate;
        out.navigate = action;
        return out;
    }
};

/**
 * Turns gamepad events into what the game does. Pads come and go (hot-plug); the first one
 * plugged in drives the local player and the others are ignored until it goes. Everything
 * here is local: nothing about a pad ever goes on the co-op wire.
 *
 * Sticks are read as four digital directions each, with a dead zone and a little hysteresis
 * (they press at half travel and let go below the dead zone), so a stick drives exactly what
 * a key does. Triggers are buttons that press at half pull.
 */
class PadInput {
public:
    PadInput(const PadBindings &bindings) :
        _bindings(bindings) {
    }

    /** Dead zone of the sticks, 0..0.9 (default 0.25). */
    void setDeadZone(float deadZone);
    float deadZone() const { return _deadZone; }

    /** Handle one gamepad event (anything else is ignored); the outputs, in order. */
    std::vector<PadOutput> handle(const input::Event &event, PadMode mode);

    /** Time passes: held menu directions repeat. */
    std::vector<PadOutput> update(float dt, PadMode mode);

    /** The mode changed under held buttons: let them all go. */
    std::vector<PadOutput> releaseAll();

    std::optional<uint32_t> activePad() const { return _active; }
    const std::vector<uint32_t> &pads() const { return _pads; }

    // Menu repeat, in seconds.
    static constexpr float kRepeatDelay = 0.4f;
    static constexpr float kRepeatInterval = 0.12f;

private:
    const PadBindings &_bindings;
    float _deadZone {0.25f};
    std::vector<uint32_t> _pads;
    std::optional<uint32_t> _active;
    std::optional<PadMode> _mode;

    std::map<input::GamepadAxis, float> _axes;
    std::set<input::GamepadButton> _held;      /**< buttons down, trigger pulls included */
    std::set<int> _stickDirections;            /**< pressed stick directions: axis * 2 + (negative ? 0 : 1) */
    std::map<int, PadOutput> _releases;        /**< by source (a button, a stick direction): what letting it go does */
    std::optional<input::NavigateAction> _repeating;
    float _repeatTimer {0.0f};

    void switchMode(PadMode mode, std::vector<PadOutput> &out);
    void press(input::GamepadButton button, PadMode mode, std::vector<PadOutput> &out);
    void release(input::GamepadButton button, PadMode mode, std::vector<PadOutput> &out);
    void stickDirection(int direction, bool pressed, PadMode mode, std::vector<PadOutput> &out);
    void axis(input::GamepadAxis axis, float value, PadMode mode, std::vector<PadOutput> &out);
    void startRepeat(input::NavigateAction action);
};

} // namespace game

} // namespace reone
