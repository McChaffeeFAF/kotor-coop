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

#include "reone/gui/control/slider.h"

#include "reone/graphics/texture.h"
#include "reone/resource/parser/gff/gui.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/di/services.h"
#include "reone/resource/resources.h"
#include "reone/scene/render/pass.h"

#include <algorithm>

using namespace reone::graphics;

namespace reone {

namespace gui {

void Slider::load(const resource::generated::GUI_BASECONTROL &gui, bool protoItem) {
    Control::load(gui, protoItem);
    auto &controlStruct = *static_cast<const resource::generated::GUI_CONTROLS *>(&gui);
    if (controlStruct.THUMB && !controlStruct.THUMB->IMAGE.empty()) {
        _thumb = _resourceSvc.textures.get(controlStruct.THUMB->IMAGE, TextureUsage::GUI);
    }
    if (controlStruct.MAXVALUE > 0) {
        _value = std::clamp(controlStruct.CURVALUE / static_cast<float>(controlStruct.MAXVALUE), 0.0f, 1.0f);
    }
}

int Slider::thumbWidth() const {
    // The authored thumb is a narrow bar as tall as the track.
    if (_thumb && _thumb->height() > 0) {
        return std::max(2, _extent.height * _thumb->width() / _thumb->height());
    }
    return std::max(2, _extent.height / 2);
}

int Slider::thumbHeight() const {
    // On a vertical track the thumb is a marker as wide as the track.
    if (_thumb && _thumb->width() > 0) {
        return std::max(2, _extent.width * _thumb->height() / _thumb->width());
    }
    return std::max(2, _extent.width / 2);
}

void Slider::setValue(float value) {
    _value = std::clamp(value, 0.0f, 1.0f);
}

float Slider::valueAt(int x) const {
    int travel = _extent.width - thumbWidth();
    if (travel <= 0) {
        return _value;
    }
    float local = static_cast<float>(x - _extent.left - thumbWidth() / 2);
    return std::clamp(local / travel, 0.0f, 1.0f);
}

float Slider::valueAt(int x, int y) const {
    if (!isVertical()) {
        return valueAt(x);
    }
    int travel = _extent.height - thumbHeight();
    if (travel <= 0) {
        return _value;
    }
    float local = static_cast<float>(y - _extent.top - thumbHeight() / 2);
    return 1.0f - std::clamp(local / travel, 0.0f, 1.0f);
}

bool Slider::handleClick(int x, int y, int clicks) {
    return handleMouseDrag(x, y);
}

bool Slider::handlePadDirection(input::NavigateAction direction) {
    float step = 0.1f;
    float value = _value;
    if (direction == input::NavigateAction::Left) {
        value -= step;
    } else if (direction == input::NavigateAction::Right) {
        value += step;
    } else {
        return false;
    }
    value = std::clamp(value, 0.0f, 1.0f);
    if (value != _value) {
        _value = value;
        if (_onValueChanged) {
            _onValueChanged(_value);
        }
    }
    return true;
}

bool Slider::handleMouseDrag(int x, int y) {
    float value = valueAt(x, y);
    if (value != _value) {
        _value = value;
        if (_onValueChanged) {
            _onValueChanged(_value);
        }
    }
    return true;
}

void Slider::render(const glm::ivec2 &screenSize, const glm::ivec2 &offset, scene::IRenderPass &pass) {
    if (!_visible) {
        return;
    }
    Control::render(screenSize, offset, pass);
    if (!_thumb) {
        return;
    }
    if (isVertical()) {
        // Sized from the track's width: sizing it from the height drew the
        // marker as a bar many times the screen's width.
        int height = thumbHeight();
        float y = _extent.top + offset.y + (1.0f - _value) * (_extent.height - height);
        pass.drawImage(*_thumb, {static_cast<float>(_extent.left + offset.x), y},
                       {static_cast<float>(_extent.width), static_cast<float>(height)});
        return;
    }
    int width = thumbWidth();
    float x = _extent.left + offset.x + _value * (_extent.width - width);
    pass.drawImage(*_thumb, {x, static_cast<float>(_extent.top + offset.y)},
                   {static_cast<float>(width), static_cast<float>(_extent.height)});
}

} // namespace gui

} // namespace reone
