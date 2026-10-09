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

#include "reone/gui/control/togglebutton.h"

#include "reone/graphics/renderbuffer.h"
#include "reone/graphics/texture.h"
#include "reone/resource/parser/gff/gui.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/di/services.h"

namespace reone {

namespace gui {

void ToggleButton::load(const resource::generated::GUI_BASECONTROL &gui, bool protoItem) {
    Button::load(gui, protoItem);
    if (protoItem) {
        return;
    }
    const auto &controlStruct = *static_cast<const resource::generated::GUI_CONTROLS *>(&gui);
    auto texture = [this](const std::string &resRef) -> std::shared_ptr<graphics::Texture> {
        if (resRef.empty() || resRef == "0") {
            return nullptr;
        }
        return _resourceSvc.textures.get(resRef, graphics::TextureUsage::GUI);
    };
    _offFill = _border ? _border->fill : nullptr;
    _offHilightFill = _hilight ? _hilight->fill : nullptr;
    if (controlStruct.SELECTED) {
        _onFill = texture(controlStruct.SELECTED->FILL);
    }
    if (controlStruct.HILIGHTSELECTED) {
        _onHilightFill = texture(controlStruct.HILIGHTSELECTED->FILL);
    }
}

const glm::vec3 &ToggleButton::getBorderColor() const {
    return _on ? _onColor : Button::getBorderColor();
}

void ToggleButton::toggle() {
    _on = !_on;
    applyCheckedFill();
}

void ToggleButton::setShowCheckedFill(bool show) {
    _showCheckedFill = show;
    applyCheckedFill();
}

void ToggleButton::copyCheckedFill(const ToggleButton &other) {
    _offFill = other._offFill;
    _offHilightFill = other._offHilightFill;
    _onFill = other._onFill;
    _onHilightFill = other._onHilightFill;
    setShowCheckedFill(other._showCheckedFill);
}

void ToggleButton::applyCheckedFill() {
    if (!_showCheckedFill || !_onFill) {
        return;
    }
    if (_border) {
        _border->fill = _on ? _onFill : _offFill;
    }
    if (_hilight) {
        auto fill = _on ? (_onHilightFill ? _onHilightFill : _onFill) : _offHilightFill;
        if (fill) {
            _hilight->fill = fill;
        }
    }
}

void ToggleButton::setOnColor(const glm::vec3 &color) {
    _onColor = color;
}

} // namespace gui

} // namespace reone
