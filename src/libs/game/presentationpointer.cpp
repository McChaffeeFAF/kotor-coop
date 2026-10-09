/*
 * Copyright (c) 2020-2026 The reone project contributors
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

#include "reone/game/presentationpointer.h"

#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"

namespace reone::game {

void PresentationPointer::setType(resource::CursorType type) {
    if (_type == type)
        return;
    _cursor = type == resource::CursorType::None ? nullptr : _cursors.get(type);
    _type = type;
}

void PresentationPointer::setPosition(glm::ivec2 position) {
    if (_cursor)
        _cursor->setPosition(position);
}

void PresentationPointer::setPressed(bool pressed) {
    if (_cursor)
        _cursor->setPressed(pressed);
}

void PresentationPointer::render(const graphics::GraphicsOptions &options, bool relativeMouseMode) {
    if (!_cursor || relativeMouseMode)
        return;
    static constexpr float kCursorSizeScale = 0.5f;
    float scale = std::min(options.width / 800.0f, options.height / 600.0f) * options.guiScale * kCursorSizeScale;
    _cursor->render(scale);
}

resource::CursorType cursorTypeForCursorsRow(resource::ITwoDAs &twoDas, int row) {
    using resource::CursorType;
    auto cursors = twoDas.get("cursors");
    if (!cursors || row <= 0 || row >= cursors->getRowCount()) {
        return CursorType::None;
    }
    switch (cursors->getInt(row, "cursorid", -1)) {
    case 11:
        return CursorType::Talk;
    case 21:
    case 23:
        return CursorType::Door;
    case 25:
        return CursorType::Pickup;
    case 33:
        return CursorType::DisableMine;
    case 37:
        return CursorType::RecoverMine;
    case 7:
    case 51:
        return CursorType::Attack;
    default:
        return CursorType::Default;
    }
}

resource::CursorType contextualCursor(ObjectType type, bool dead, bool hostile) {
    using resource::CursorType;
    switch (type) {
    case ObjectType::Creature:
        return dead ? CursorType::Pickup : (hostile ? CursorType::Attack : CursorType::Talk);
    case ObjectType::Door:
        return CursorType::Door;
    case ObjectType::Placeable:
        return CursorType::Pickup;
    default:
        return CursorType::Default;
    }
}

} // namespace reone::game
