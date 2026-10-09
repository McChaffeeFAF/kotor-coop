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

#include "reone/game/gui/loadscreen.h"

#include "reone/resource/di/services.h"
#include "reone/resource/provider/textures.h"

#include "reone/game/game.h"
#include "reone/resource/resources.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

void LoadingScreen::preload(IGUI &gui) {
    GameGUI::preload(gui);
    if (isTSL()) {
        gui.setResolution(800, 600);
        // The panel's fill is the load image, a 4:3 picture: it stays in
        // its authored aspect, centred on black.
        gui.setScaling(IGUI::ScalingMode::Scaled);
    }
}

void LoadingScreen::onGUILoaded() {
    // K1: its load backdrop. K2: black beside the 4:3 load image, which the
    // previous frame showed through on a wide screen.
    loadBackground(BackgroundType::Load);

    bindControls();

    // The loading screen shows the game being loaded: a K1 session keeps
    // K1's title logo in K2's frame.
    if (isK2LayoutInK1()) {
        if (auto logo = _gui->findControl("LBL_LOGO")) {
            logo->setBorderFill(_services.resource.textures.get("logo_sw_02", TextureUsage::GUI));
        }
    }

    _controls.LBL_HINT->setTextMessage("");
}

void LoadingScreen::setImage(const std::string &resRef) {
    // The load image is the module's own art: K1 content in a K1 session,
    // whatever layout frames it.
    _gui->rootControl().setBorderFill(_services.resource.textures.get(resRef, TextureUsage::GUI));
}

void LoadingScreen::setHint(const std::string &text) {
    _controls.LBL_HINT->setTextMessage(text);
}

void LoadingScreen::setProgress(int progress) {
    _controls.PB_PROGRESS->setValue(progress);
}

} // namespace game

} // namespace reone
