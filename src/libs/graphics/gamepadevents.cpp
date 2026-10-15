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

#include "reone/graphics/gamepadevents.h"

#include <algorithm>

#include <SDL3/SDL_gamepad.h>

namespace reone {

namespace graphics {

static std::optional<input::GamepadButton> buttonFromSDL(Uint8 button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return input::GamepadButton::A;
    case SDL_GAMEPAD_BUTTON_EAST:
        return input::GamepadButton::B;
    case SDL_GAMEPAD_BUTTON_WEST:
        return input::GamepadButton::X;
    case SDL_GAMEPAD_BUTTON_NORTH:
        return input::GamepadButton::Y;
    case SDL_GAMEPAD_BUTTON_BACK:
        return input::GamepadButton::Back;
    case SDL_GAMEPAD_BUTTON_GUIDE:
        return input::GamepadButton::Guide;
    case SDL_GAMEPAD_BUTTON_START:
        return input::GamepadButton::Start;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return input::GamepadButton::LeftStick;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return input::GamepadButton::RightStick;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return input::GamepadButton::LeftShoulder;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return input::GamepadButton::RightShoulder;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return input::GamepadButton::DPadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return input::GamepadButton::DPadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return input::GamepadButton::DPadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return input::GamepadButton::DPadRight;
    default:
        return std::nullopt;
    }
}

static std::optional<input::GamepadAxis> axisFromSDL(Uint8 axis) {
    switch (axis) {
    case SDL_GAMEPAD_AXIS_LEFTX:
        return input::GamepadAxis::LeftX;
    case SDL_GAMEPAD_AXIS_LEFTY:
        return input::GamepadAxis::LeftY;
    case SDL_GAMEPAD_AXIS_RIGHTX:
        return input::GamepadAxis::RightX;
    case SDL_GAMEPAD_AXIS_RIGHTY:
        return input::GamepadAxis::RightY;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
        return input::GamepadAxis::LeftTrigger;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
        return input::GamepadAxis::RightTrigger;
    default:
        return std::nullopt;
    }
}

std::optional<input::Event> gamepadEventFromSDL(const SDL_Event &event) {
    switch (event.type) {
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        auto button = buttonFromSDL(event.gbutton.button);
        if (!button) {
            return std::nullopt;
        }
        return input::Event::newGamepadButton(input::GamepadButtonEvent {
            static_cast<uint32_t>(event.gbutton.which), *button, event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN});
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        auto axis = axisFromSDL(event.gaxis.axis);
        if (!axis) {
            return std::nullopt;
        }
        float value = std::clamp(static_cast<float>(event.gaxis.value) / 32767.0f, -1.0f, 1.0f);
        if (*axis == input::GamepadAxis::LeftTrigger || *axis == input::GamepadAxis::RightTrigger) {
            value = std::max(0.0f, value);
        }
        return input::Event::newGamepadAxis(input::GamepadAxisEvent {static_cast<uint32_t>(event.gaxis.which), *axis, value});
    }
    case SDL_EVENT_GAMEPAD_ADDED:
        return input::Event::newGamepadDevice(input::GamepadDeviceEvent {static_cast<uint32_t>(event.gdevice.which), true});
    case SDL_EVENT_GAMEPAD_REMOVED:
        return input::Event::newGamepadDevice(input::GamepadDeviceEvent {static_cast<uint32_t>(event.gdevice.which), false});
    default:
        return std::nullopt;
    }
}

} // namespace graphics

} // namespace reone
