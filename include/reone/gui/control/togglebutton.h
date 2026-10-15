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

#include "button.h"

namespace reone {

namespace gui {

class ToggleButton : public Button {
public:
    ToggleButton(
        IGUI &gui,
        scene::ISceneGraphs &sceneGraphs,
        graphics::GraphicsServices &graphicsSvc,
        resource::ResourceServices &resourceSvc) :
        Button(
            gui,
            ControlType::ToggleButton,
            sceneGraphs,
            graphicsSvc,
            resourceSvc) {
    }

    void load(const resource::generated::GUI_BASECONTROL &gui, bool protoItem = false) override;

    const glm::vec3 &getBorderColor() const override;

    void toggle();
    /** Turns the button on or off (upstream #356). */
    void setOn(bool on) {
        if (_on != on) {
            toggle();
        }
    }

    bool isOn() const { return _on; }

    void setOnColor(const glm::vec3 &color);

    /**
     * Show the authored check-box art: the SELECTED and HILIGHTSELECTED
     * fills while on, the BORDER and HILIGHT fills while off. Off by
     * default; screens whose toggles mark their state otherwise keep it off.
     */
    void setShowCheckedFill(bool show);
    /** Copy the checked art (and whether it shows) from another toggle. */
    void copyCheckedFill(const ToggleButton &other);

private:
    bool _on {false};
    glm::vec3 _onColor {1.0f};
    bool _showCheckedFill {false};
    std::shared_ptr<graphics::Texture> _offFill;
    std::shared_ptr<graphics::Texture> _offHilightFill;
    std::shared_ptr<graphics::Texture> _onFill;
    std::shared_ptr<graphics::Texture> _onHilightFill;

    void applyCheckedFill();
};

} // namespace gui

} // namespace reone
