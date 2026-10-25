/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#include <set>
#include <vector>

#include "event.h"

namespace reone {

namespace input {

/**
 * Tracks which keys and mouse buttons the game has seen go down without
 * seeing them come up, so that losing window focus (alt-tab, a minimise)
 * can let go of them: the release happens in another window and never
 * arrives. Also decides when the mouse may be captured.
 */
class FocusTracker {
public:
    /** Sees every event on its way to the game. */
    void observe(const Event &event) {
        switch (event.type) {
        case EventType::KeyDown:
            _keys.insert({event.key.code, event.key.mod});
            break;
        case EventType::KeyUp:
            for (auto it = _keys.begin(); it != _keys.end();) {
                it = it->first == event.key.code ? _keys.erase(it) : std::next(it);
            }
            break;
        case EventType::MouseButtonDown:
            _buttons.insert(event.button.button);
            break;
        case EventType::MouseButtonUp:
            _buttons.erase(event.button.button);
            break;
        default:
            break;
        }
    }

    /**
     * Focus is gone. Returns the releases to deliver for everything still
     * held (empty if focus was already gone).
     */
    std::vector<Event> loseFocus(int32_t mouseX, int32_t mouseY) {
        std::vector<Event> releases;
        if (!_focused) {
            return releases;
        }
        _focused = false;
        for (auto &[code, mod] : _keys) {
            releases.push_back(Event::newKeyUp(KeyEvent(false, code, mod, false)));
        }
        for (auto button : _buttons) {
            releases.push_back(Event::newMouseButtonUp(MouseButtonEvent(button, false, 1, mouseX, mouseY)));
        }
        _keys.clear();
        _buttons.clear();
        return releases;
    }

    void gainFocus() {
        _focused = true;
    }

    bool focused() const {
        return _focused;
    }
    size_t heldKeys() const {
        return _keys.size();
    }
    size_t heldButtons() const {
        return _buttons.size();
    }

    /**
     * Whether the mouse may be captured (relative mode, pointer hidden).
     * The game asks for it for mouse-look and the first-person and turret
     * views; a modal pop-up on top, or a window without focus, overrides
     * the ask, because the player needs a pointer to answer it.
     */
    static bool allowRelativeMouse(bool requested, bool modalOpen, bool focused) {
        return requested && focused && !modalOpen;
    }

    /**
     * Which events go on to the game while a modal pop-up has the input:
     * releases (they belong to whatever saw the press). Pointer motion goes
     * to the game's pointer only (Game::handleMouseMotion), so the pointer
     * the game draws keeps following the mouse over the pop-up.
     */
    static bool reachesGameUnderModal(EventType type) {
        return type == EventType::MouseButtonUp || type == EventType::KeyUp;
    }
    static bool movesPointerUnderModal(EventType type) {
        return type == EventType::MouseMotion;
    }

    /**
     * Whether the main loop sleeps instead of running a frame while the
     * window has no focus, as single player always has. A session other
     * players depend on (a co-op host, a dedicated host: serving) keeps
     * simulating, or every guest freezes with it. A headless run never
     * has real focus to lose; there only the harness's "focus lost" counts.
     */
    static bool sleepsUnfocused(bool focused, bool headless, bool harnessLoss, bool runUnfocused, bool serving) {
        if (focused || runUnfocused || serving) {
            return false;
        }
        return !headless || harnessLoss;
    }

    /** A serving session without focus still draws, at a lower rate. */
    static bool throttlesUnfocused(bool focused, bool serving) {
        return !focused && serving;
    }

private:
    std::set<std::pair<KeyCode, uint16_t>> _keys;
    std::set<MouseButton> _buttons;
    bool _focused {true};
};

} // namespace input

} // namespace reone
