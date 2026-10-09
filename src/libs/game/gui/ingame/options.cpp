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

#include "reone/game/gui/ingame/options.h"

#include "reone/game/game.h"
#include "reone/gui/control/button.h"
#include "reone/resource/parser/gff/gui.h"
#include "reone/resource/provider/gffs.h"
#include "reone/system/logutil.h"

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

void OptionsMenu::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();

    if (isTSL()) {
        fillK2SectionStrip(_controls.LBL_BAR1, _controls.LBL_BAR2);
        _controls.LB_DESC->setTintBorderFill(true);
        useK2ShellTitle(_controls.LBL_TITLE);
        for (auto &button : {
                 _controls.BTN_SAVEGAME,
                 _controls.BTN_LOADGAME,
                 _controls.BTN_GAMEPLAY,
                 _controls.BTN_FEEDBACK,
                 _controls.BTN_AUTOPAUSE,
                 _controls.BTN_GRAPHICS,
                 _controls.BTN_SOUND,
                 _controls.BTN_QUIT,
                 _controls.BTN_EXIT}) {
            enableK2ButtonBodyFill(button);
        }
    }
    _controls.BTN_LOADGAME->setOnClick([this]() {
        _game.openSaveLoad(SaveLoadMode::LoadFromInGame);
    });
    _controls.BTN_SAVEGAME->setOnClick([this]() {
        _game.openSaveLoad(SaveLoadMode::Save);
    });
    _controls.BTN_EXIT->setOnClick([this]() {
        _game.openInGame();
    });
    addCoopButton();

    // The option pages open over this one; their Close comes back here.
    struct Page {
        std::shared_ptr<gui::Button> button;
        std::string panel;
    };
    const Page pages[] {
        {_controls.BTN_GAMEPLAY, guiResRef("optgameplay")},
        {_controls.BTN_FEEDBACK, guiResRef("optfeedback")},
        {_controls.BTN_AUTOPAUSE, guiResRef("optautopause")},
        {_controls.BTN_GRAPHICS, guiResRef("optgraphics")},
        {_controls.BTN_SOUND, guiResRef("optsound")}};
    for (const auto &page : pages) {
        if (!page.button) {
            continue;
        }
        std::string panel = page.panel;
        page.button->setOnClick([this, panel]() {
            _panels.open(panel);
        });
    }
}

void OptionsMenu::addCoopButton() {
    // Co-op (co-op design notes, "Menus"): one more button in the column, a copy
    // of the last authored one a row further down (K1 and K2 layouts both
    // leave that row empty).
    if (auto existing = findControl<Button>("BTN_COOP")) {
        _coopButton = existing;
    } else {
        auto gff = _presentationResources.gffs.get(_resRef, resource::ResType::Gui);
        if (!gff) {
            return;
        }
        auto parsed = resource::generated::parseGUI(*gff);
        auto byTag = [&](const char *tag) {
            return std::find_if(parsed.CONTROLS.begin(), parsed.CONTROLS.end(), [tag](const auto &c) { return c.TAG == tag; });
        };
        auto last = byTag("BTN_QUIT");
        auto above = byTag("BTN_SOUND");
        if (last == parsed.CONTROLS.end() || above == parsed.CONTROLS.end()) {
            return;
        }
        auto copy = *last;
        copy.TAG = "BTN_COOP";
        copy.EXTENT.TOP = last->EXTENT.TOP + (last->EXTENT.TOP - above->EXTENT.TOP);
        auto control = _gui->newControl(gui::ControlType::Button, copy.TAG);
        if (!control) {
            return;
        }
        control->load(copy);
        _coopButton = std::shared_ptr<Button>(static_cast<Button *>(control.release()));
        _gui->addControlToFront(_coopButton, gui::IGUI::ControlCoordinates::Authored);
        if (isTSL()) {
            enableK2ButtonBodyFill(_coopButton);
        }
    }
    _coopButton->setTextMessage("Co-op");
    _coopButton->setOnClick([this]() {
        openCoop();
    });
}

void OptionsMenu::openCoop() {
    if (!_coop) {
        auto menu = std::make_unique<CoopMenu>(_game, _services, CoopMenu::Place::InGame);
        try {
            menu->init();
        } catch (const std::exception &e) {
            warn(std::string("Co-op page did not load: ") + e.what());
            return;
        }
        _coop = std::move(menu);
    }
    _coop->open();
}

void OptionsMenu::closeCoop() {
    if (_coop) {
        _coop->close();
    }
}

bool OptionsMenu::handle(const input::Event &event) {
    if (_coop && _coop->isOpen()) {
        return _coop->handle(event);
    }
    if (_panels.isOpen()) {
        _panels.handle(event);
        return true;
    }
    return GameGUI::handle(event);
}

void OptionsMenu::update(float dt) {
    if (_coop && _coop->isOpen()) {
        _coop->update(dt);
        return;
    }
    if (_panels.isOpen()) {
        _panels.update(dt);
        return;
    }
    GameGUI::update(dt);
}

void OptionsMenu::render() {
    if (_coop && _coop->isOpen()) {
        _coop->render();
        return;
    }
    if (_panels.isOpen()) {
        _panels.render();
        return;
    }
    GameGUI::render();
}

} // namespace game

} // namespace reone
