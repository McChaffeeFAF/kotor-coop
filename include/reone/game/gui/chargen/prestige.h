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

#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"

#include "../../types.h"
#include "../../gui.h"

namespace reone {

namespace gui {

class IconChain;

}

namespace game {

class CharacterGeneration;

/**
 * Prestige class selection, the first step of a level-up once the K2
 * prestige classes are unlocked (design note k2_merge, "Prestige classes").
 *
 * It uses the feat selection layout of the running HUD profile (ftchrgen or
 * ftchrgen_p), so the K1 layout keeps K1 art: the offered classes are the
 * icons of the grid, the description box explains the pick, OK takes the
 * selected class, and Cancel keeps levelling the Jedi class (the choice is
 * offered again at the next level-up).
 */
class PrestigeSelection : public GameGUI {
public:
    PrestigeSelection(
        CharacterGeneration &charGen,
        Game &game,
        ServicesView &services) :
        GameGUI(game, services),
        _charGen(charGen) {
        _resRef = guiResRef("ftchrgen");
    }

    /// A private copy of the layout: the feat screen shares the cached one,
    /// and this screen rewires its buttons and grid.
    void init() override;

    void reset(std::vector<ClassType> offered);

    const std::vector<ClassType> &offered() const { return _offered; }
    ClassType selected() const { return _selected; }

    /// Select an offered class (as clicking its icon does); false otherwise.
    bool select(ClassType type);
    /// OK: take the selected class. False with nothing selected.
    bool accept();
    /// Cancel: keep the Jedi class this level.
    void skip();

private:
    struct Controls {
        std::shared_ptr<gui::Button> BTN_ACCEPT;
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Button> BTN_RECOMMENDED;
        std::shared_ptr<gui::Button> BTN_SELECT;
        std::shared_ptr<gui::Label> LBL_NAME;
        std::shared_ptr<gui::IconChain> ICONCHAIN_CLASSES;
        std::shared_ptr<gui::ListBox> LB_DESC;
        std::shared_ptr<gui::ListBox> LB_FEATS;
        std::shared_ptr<gui::Label> STD_REMAINING_SELECTIONS_LBL;
        std::shared_ptr<gui::Label> STD_SELECTIONS_REMAINING_LBL;
        std::shared_ptr<gui::Label> SUB_TITLE_LBL;
    };

    Controls _controls;

    CharacterGeneration &_charGen;
    std::vector<ClassType> _offered;
    ClassType _selected {ClassType::Invalid};

    void onGUILoaded() override;

    void bindControls() {
        _controls.BTN_ACCEPT = findControl<gui::Button>("BTN_ACCEPT");
        _controls.BTN_BACK = findControl<gui::Button>("BTN_BACK");
        _controls.BTN_RECOMMENDED = findControl<gui::Button>("BTN_RECOMMENDED");
        _controls.BTN_SELECT = findControl<gui::Button>("BTN_SELECT");
        _controls.LBL_NAME = findControl<gui::Label>("LBL_NAME");
        _controls.LB_DESC = findControl<gui::ListBox>("LB_DESC");
        _controls.LB_FEATS = findControl<gui::ListBox>("LB_FEATS");
        _controls.STD_REMAINING_SELECTIONS_LBL = findControl<gui::Label>("STD_REMAINING_SELECTIONS_LBL");
        _controls.STD_SELECTIONS_REMAINING_LBL = findControl<gui::Label>("STD_SELECTIONS_REMAINING_LBL");
        _controls.SUB_TITLE_LBL = findControl<gui::Label>("SUB_TITLE_LBL");
    }

    void refresh();
    void showDescription(ClassType type);
    std::string describe(ClassType type) const;
};

} // namespace game

} // namespace reone
