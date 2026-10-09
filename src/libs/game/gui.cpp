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

#include "reone/game/gui.h"
#include "reone/game/game.h"
#include "reone/gui/control.h"
#include "reone/gui/gui.h"

namespace reone::game {

GameGUI::GameGUI(Game &game, ServicesView &services) :
    GameGUI(game, services, PresentationFamily::Menu) {
}

static HudPresentationServices presentationFor(Game &game, ServicesView &services, PresentationFamily family) {
    if (family == PresentationFamily::Hud || family == PresentationFamily::Menu) {
        return game.hudPresentation();
    }
    return {game.gameId(), services.gui.guis, services.resource};
}

GameGUI::GameGUI(Game &game, ServicesView &services, PresentationFamily family) :
    GameGUI(game, services, presentationFor(game, services, family)) {
}

GameGUI::GameGUI(Game &game, ServicesView &services, const HudPresentationServices &presentation) :
    PresentationGUI(presentation.presentationId, game.options().graphics,
                    {presentation.guis, presentation.resource.textures,
                     services.audio.mixer, services.game.guiSounds}),
    _game(game),
    _services(services),
    _presentationResources(presentation.resource) {
}

bool GameGUI::isK2LayoutInK1() const {
    return isTSL() && !_game.isTSL();
}

void GameGUI::hideK2OnlyControls(std::initializer_list<const char *> tags) {
    if (!isK2LayoutInK1() || !_gui) {
        return;
    }
    for (const char *tag : tags) {
        if (auto control = _gui->findControl(tag)) {
            control->setVisible(false);
            control->setDisabled(true);
        }
    }
}

} // namespace reone::game
