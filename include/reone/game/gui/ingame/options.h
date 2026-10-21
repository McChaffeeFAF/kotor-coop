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

#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"

#include "../../gui.h"
#include "../coopmenu.h"
#include "../optionspanels.h"

namespace reone {

namespace gui {

class Button;

}

namespace game {

class OptionsMenu : public GameGUI {
public:
    OptionsMenu(Game &game, ServicesView &services) :
        GameGUI(game, services),
        _panels(game, services) {
        _resRef = guiResRef("optionsingame");
    }

    bool handle(const input::Event &event) override;
    void update(float dt) override;
    void render() override;
    bool coversMenuHost() const override { return (_panels.isOpen() || (_coop && _coop->isOpen())); }

    /** The sub-panels (Gameplay, Feedback, ...) opened over this page. */
    OptionsPanels &panels() { return _panels; }

    /** The Co-op page (design note coop_online): the join code, Copy, New code, players. */
    void openCoop();
    void closeCoop();
    CoopMenu *coopMenu() { return _coop.get(); }
    PresentationGUI *inputPage() override { return _coop && _coop->isOpen() ? _coop->inputPage() : this; }

private:
    struct Controls {
        std::shared_ptr<gui::Button> BTN_AUTOPAUSE;
        std::shared_ptr<gui::Button> BTN_EXIT;
        std::shared_ptr<gui::Button> BTN_FEEDBACK;
        std::shared_ptr<gui::Button> BTN_GAMEPLAY;
        std::shared_ptr<gui::Button> BTN_GRAPHICS;
        std::shared_ptr<gui::Button> BTN_LOADGAME;
        std::shared_ptr<gui::Button> BTN_QUIT;
        std::shared_ptr<gui::Button> BTN_SAVEGAME;
        std::shared_ptr<gui::Button> BTN_SOUND;
        std::shared_ptr<gui::Label> LBL_BAR1;
        std::shared_ptr<gui::Label> LBL_BAR2;
        std::shared_ptr<gui::Label> LBL_BAR3;
        std::shared_ptr<gui::Label> LBL_BAR4;
        std::shared_ptr<gui::Label> LBL_BAR5;
        std::shared_ptr<gui::Label> LBL_TITLE;
        std::shared_ptr<gui::ListBox> LB_DESC;
    };

    Controls _controls;
    OptionsPanels _panels;
    std::unique_ptr<CoopMenu> _coop;
    std::shared_ptr<gui::Button> _coopButton;

    void addCoopButton();

    void onGUILoaded() override;

    void bindControls() {
        _controls.BTN_AUTOPAUSE = findControl<gui::Button>("BTN_AUTOPAUSE");
        _controls.BTN_EXIT = findControl<gui::Button>("BTN_EXIT");
        _controls.BTN_FEEDBACK = findControl<gui::Button>("BTN_FEEDBACK");
        _controls.BTN_GAMEPLAY = findControl<gui::Button>("BTN_GAMEPLAY");
        _controls.BTN_GRAPHICS = findControl<gui::Button>("BTN_GRAPHICS");
        _controls.BTN_LOADGAME = findControl<gui::Button>("BTN_LOADGAME");
        _controls.BTN_QUIT = findControl<gui::Button>("BTN_QUIT");
        _controls.BTN_SAVEGAME = findControl<gui::Button>("BTN_SAVEGAME");
        _controls.BTN_SOUND = findControl<gui::Button>("BTN_SOUND");
        _controls.LBL_BAR1 = findControl<gui::Label>("LBL_BAR1");
        _controls.LBL_BAR2 = findControl<gui::Label>("LBL_BAR2");
        _controls.LBL_BAR3 = findControl<gui::Label>("LBL_BAR3");
        _controls.LBL_BAR4 = findControl<gui::Label>("LBL_BAR4");
        _controls.LBL_BAR5 = findControl<gui::Label>("LBL_BAR5");
        _controls.LBL_TITLE = findControl<gui::Label>("LBL_TITLE");
        _controls.LB_DESC = findControl<gui::ListBox>("LB_DESC");
    }
};

} // namespace game

} // namespace reone
