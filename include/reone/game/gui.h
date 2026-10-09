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

#include "reone/game/di/services.h"
#include "hudpresentation.h"
#include "presentationgui.h"

#include <initializer_list>

namespace reone::game {

class Game;

/** Strong gameplay facade for existing screen constructors and subclasses. */
class GameGUI : public PresentationGUI {
protected:
    Game &_game;
    ServicesView &_services;
    /**
     * GUI resources this screen presents through: the main services for menus,
     * or the HUD profile's for the HUD family. Game data (portraits, icons,
     * strings in code) still comes from _services.
     */
    resource::ResourceServices &_presentationResources;

    GameGUI(Game &game, ServicesView &services);
    GameGUI(Game &game, ServicesView &services, PresentationFamily family);
    GameGUI(Game &game, ServicesView &services, const HudPresentationServices &presentation);

    /** A K2 layout presenting a K1 session (--hud k2). */
    bool isK2LayoutInK1() const;

    /**
     * Hide controls that only serve K2 systems (influence, prestige, cloud
     * saves, ...) when a K2 layout presents a K1 session. The hidden set is
     * listed in design note p1_k2menus.
     */
    void hideK2OnlyControls(std::initializer_list<const char *> tags);
};

} // namespace reone::game
