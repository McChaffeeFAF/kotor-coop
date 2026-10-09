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

#include "reone/resource/types.h"

namespace reone {

namespace gui {

class IGUIs;

}

namespace resource {

struct ResourceServices;

}

namespace game {

/**
 * What the HUD family presents through.
 *
 * The presentation ID picks layouts, colours and styling; it is the running
 * game's ID unless the HUD profile says otherwise. The GUI loader and resource
 * view are the K2 GUI set for a K2 HUD in a K1 session, and the main ones in
 * every other case. Game rules never key off the presentation ID.
 *
 * Content art stays on the main services whatever the profile: portraits,
 * item, feat and power icons, and area maps are K1 data in a K1 session.
 */
struct HudPresentationServices {
    resource::GameID presentationId;
    gui::IGUIs &guis;
    resource::ResourceServices &resource;

    bool isTSL() const { return presentationId == resource::GameID::TSL; }
};

/** Which family a game GUI belongs to, for choosing its presentation. */
enum class PresentationFamily {
    /// Menus: the main menu and its sub-menus, options, character generation,
    /// level-up, the in-game menus, store, container, upgrade bench, party
    /// selection and the loading screen. Presented per the HUD profile, so a
    /// K2 HUD brings K2 menus with it (design note p1_k2menus).
    Menu,
    /// The HUD and the screens it opens directly: presented per the HUD profile.
    Hud,
    /// Screens whose layout is bound to the running game's content, such as
    /// conversations, the galaxy map and Pazaak: always the running game.
    Game
};

} // namespace game

} // namespace reone
