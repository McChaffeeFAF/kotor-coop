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

#include <optional>

#include <SDL3/SDL_events.h>

#include "reone/input/event.h"

namespace reone {

namespace graphics {

/**
 * A gamepad event of SDL as the engine's own: buttons (A is the bottom face button), axes
 * scaled to -1..1 (the triggers 0..1) and hot-plug. Nullopt for any other SDL event, and for
 * buttons the engine has no use for.
 */
std::optional<input::Event> gamepadEventFromSDL(const SDL_Event &event);

} // namespace graphics

} // namespace reone
