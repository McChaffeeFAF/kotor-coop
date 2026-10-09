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

#include "../control.h"

#include <functional>

namespace reone {

namespace graphics {
class Texture;
}

namespace gui {

/**
 * A slider (options volume, gamma, ...). Its value is 0..1; a click or a
 * drag along the track sets it, and the thumb is drawn at it. A track taller
 * than it is wide is vertical (the character screens' alignment bar), with
 * its maximum at the top.
 */
class Slider : public Control {
public:
    Slider(
        IGUI &gui,
        scene::ISceneGraphs &sceneGraphs,
        graphics::GraphicsServices &graphicsSvc,
        resource::ResourceServices &resourceSvc) :
        Control(
            gui,
            ControlType::Slider,
            sceneGraphs,
            graphicsSvc,
            resourceSvc) {
        _selectable = true;
    }

    void load(const resource::generated::GUI_BASECONTROL &gui, bool protoItem) override;
    void render(const glm::ivec2 &screenSize, const glm::ivec2 &offset, scene::IRenderPass &pass) override;
    bool handleClick(int x, int y, int clicks = 1) override;
    bool handleMouseDrag(int x, int y) override;

    float value() const { return _value; }
    /** Set the value (clamped to 0..1) without notifying. */
    void setValue(float value);
    /** Called when a click or drag changes the value. */
    void setOnValueChanged(std::function<void(float)> fn) { _onValueChanged = std::move(fn); }

    /** The value a control-local x position selects. */
    float valueAt(int x) const;
    /** The value a point selects: x on a horizontal track, y on a vertical one. */
    float valueAt(int x, int y) const;
    bool isVertical() const { return _extent.height > _extent.width; }

private:
    std::shared_ptr<graphics::Texture> _thumb;
    float _value {0.0f};
    std::function<void(float)> _onValueChanged;

    int thumbWidth() const;
    int thumbHeight() const;
};

} // namespace gui

} // namespace reone
