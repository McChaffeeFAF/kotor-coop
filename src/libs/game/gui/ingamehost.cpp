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

#include "reone/game/gui/ingamehost.h"

#include <algorithm>
#include <array>

#include "reone/game/types.h"
#include "reone/resource/provider/textures.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

static void tintK2TopNavigationIcon(const std::shared_ptr<ImageButton> &button, const glm::vec3 &baseColor) {
    if (!button) {
        return;
    }
    button->setBorderColor(baseColor);
    button->setTintBorderFill(true);
}

static void configureTopNavigationIcon(const std::shared_ptr<ImageButton> &button) {
    if (!button) {
        return;
    }
    button->setSelectable(false);
    button->setSharpenBorderFillAlpha(true);
}

// Retail's K2 top.gui authors only two class-line labels; a third class slot
// (the K2 systems merge's prestige classes) needs a third, cloned from the
// second and placed one line's pitch below it (design note third_class_line).
static std::shared_ptr<Label> addThirdClassLabel(
    IGUI &gui,
    const std::shared_ptr<Label> &line1,
    const std::shared_ptr<Label> &line2,
    const std::string &tag) {

    if (auto existing = gui.findControl(tag)) {
        return std::static_pointer_cast<Label>(existing);
    }
    if (!line1 || !line2) {
        return nullptr;
    }

    int pitch = line2->authoredExtent().top - line1->authoredExtent().top;
    Control::Extent extent = line2->authoredExtent();
    extent.top += pitch;

    auto control = std::shared_ptr<Control>(gui.newControl(ControlType::Label, tag));
    control->setBorder(line2->border());
    control->setText(line2->text());
    control->setTextMessage(std::string());
    control->setPadding(line2->padding());
    control->setTintBorderFill(gui.tintBorderFills());
    control->setAuthoredExtent(extent);
    control->setExtent(extent);
    gui.addControlToBack(control, IGUI::ControlCoordinates::Authored);

    return std::static_pointer_cast<Label>(control);
}

void InGameMenuHost::preload(IGUI &gui) {
    // Chain the base: without it this GUI - the top navigation icon strip
    // among it - missed the game-wide scaled mode and floated unscaled over
    // the scaled subscreens.
    PresentationGUI::preload(gui);
    if (isTSL()) {
        gui.setResolution(800, 600);
    }
}

void InGameMenuHost::onGUILoaded() {
    if (isTSL()) {
        // On a wide screen the leader's portrait keeps its gauges and
        // overlays against it: they move as one picture (design note p3_menus).
        for (auto tag : {"LBL_BACK1", "LBL_CHAR1", "LBL_DISABLE1", "LBL_DEBILATATED1", "LBL_CMBTEFCTINC1",
                         "LBL_CMBTEFCTRED1", "LBL_LEVELUP1", "PB_VIT1", "PB_FORCE1"}) {
            _gui->setControlGroup(tag, "leader");
        }
        _gui->refreshLayout();
    }
    bindControls();

    configureTopNavigationIcon(_controls.LBLH_EQU);
    configureTopNavigationIcon(_controls.LBLH_INV);
    configureTopNavigationIcon(_controls.LBLH_CHA);
    configureTopNavigationIcon(_controls.LBLH_ABI);
    configureTopNavigationIcon(_controls.LBLH_MSG);
    configureTopNavigationIcon(_controls.LBLH_JOU);
    configureTopNavigationIcon(_controls.LBLH_MAP);
    configureTopNavigationIcon(_controls.LBLH_OPT);

    if (isTSL()) {
        tintK2TopNavigationIcon(_controls.LBLH_EQU, _baseColor);
        tintK2TopNavigationIcon(_controls.LBLH_INV, _baseColor);
        tintK2TopNavigationIcon(_controls.LBLH_CHA, _baseColor);
        tintK2TopNavigationIcon(_controls.LBLH_ABI, _baseColor);
        tintK2TopNavigationIcon(_controls.LBLH_MSG, _baseColor);
        tintK2TopNavigationIcon(_controls.LBLH_JOU, _baseColor);
        tintK2TopNavigationIcon(_controls.LBLH_MAP, _baseColor);
        tintK2TopNavigationIcon(_controls.LBLH_OPT, _baseColor);
        _controls.LBL_SECTITLE->setBorderFill(std::string());
        updateK2SectionTitle();
        _controls.LBL_BACK1->setTintBorderFill(true);
        _controls.LBL_TOP_CLASS3 = addThirdClassLabel(
            *_gui, _controls.LBL_TOP_CLASS1, _controls.LBL_TOP_CLASS2, "LBL_TOP_CLASS3");
        _controls.LBL_TOP_CLASS3LEVEL = addThirdClassLabel(
            *_gui, _controls.LBL_TOP_CLASS1LEVEL, _controls.LBL_TOP_CLASS2LEVEL, "LBL_TOP_CLASS3LEVEL");
        refreshK2Footer();
    }

    // _controls.BTN_EQU->setVisible(false);
    // _controls.BTN_INV->setVisible(false);
    // _controls.BTN_CHAR->setVisible(false);
    // _controls.BTN_ABI->setVisible(false);
    // _controls.BTN_MSG->setVisible(false);
    // _controls.BTN_JOU->setVisible(false);
    // _controls.BTN_MAP->setVisible(false);
    // _controls.BTN_OPT->setVisible(false);

    _controls.BTN_EQU->setOnClick([this]() {
        navigate(InGameMenuTab::Equipment);
    });
    _controls.BTN_INV->setOnClick([this]() {
        navigate(InGameMenuTab::Inventory);
    });
    _controls.BTN_CHAR->setOnClick([this]() {
        navigate(InGameMenuTab::Character);
    });
    _controls.BTN_ABI->setOnClick([this]() {
        navigate(InGameMenuTab::Abilities);
    });
    _controls.BTN_MSG->setOnClick([this]() {
        if (isTSL()) {
            navigate(InGameMenuTab::Party);
        } else {
            navigate(InGameMenuTab::Messages);
        }
    });
    _controls.BTN_JOU->setOnClick([this]() {
        navigate(InGameMenuTab::Journal);
    });
    _controls.BTN_MAP->setOnClick([this]() {
        navigate(InGameMenuTab::Map);
    });
    _controls.BTN_OPT->setOnClick([this]() {
        navigate(InGameMenuTab::Options);
    });

    updateTabButtons();
}

bool InGameMenuHost::handle(const input::Event &event) {
    auto tabGui = getActiveTabGUI();
    if (tabGui && tabGui->handle(event))
        return true;

    if (_gui->handle(event))
        return true;

    return false;
}

void InGameMenuHost::update(float dt) {
    PresentationGUI::update(dt);

    refreshK2Footer();

    auto tabGui = getActiveTabGUI();
    if (tabGui) {
        tabGui->update(dt);
    }
}

void InGameMenuHost::render() {
    auto tabGui = getActiveTabGUI();
    if (tabGui) {
        tabGui->render();
        if (tabGui->coversMenuHost()) {
            return;
        }
    }
    PresentationGUI::render();
}

void InGameMenuHost::changeTab(InGameMenuTab tab) {
    if (tab != InGameMenuTab::None && !available(tab))
        return;
    auto gui = getActiveTabGUI();
    if (gui) {
        gui->clearSelection();
    }
    _tab = tab;
    if (!_gui)
        return;
    updateK2SectionTitle();
    updateTabButtons();
    refreshK2Footer();
}

void InGameMenuHost::updateK2SectionTitle() {
    if (!isTSL() || !_controls.LBL_SECTITLE) {
        return;
    }

    auto border = _controls.LBL_SECTITLE->border();
    border.edge = _presentation.textures.get("uibit_brdr_16bet", TextureUsage::GUI);
    border.corner = _presentation.textures.get("uibit_brdr_16bct", TextureUsage::GUI);
    border.fill.reset();
    _controls.LBL_SECTITLE->setBorder(std::move(border));

    auto activeTab = getActiveTabGUI();
    auto titleControl = activeTab ? activeTab->k2InGameTitleControl() : nullptr;
    if (titleControl) {
        _controls.LBL_SECTITLE->setText(titleControl->text());
    } else {
        _controls.LBL_SECTITLE->setTextMessage(std::string());
    }
}

void InGameMenuHost::refreshK2Footer() {
    if (!isTSL()) {
        return;
    }

    auto hide = [](const auto &control) {
        if (control) {
            control->setVisible(false);
        }
    };

    hide(_controls.BTN_CHANGE2);
    hide(_controls.BTN_CHANGE3);
    hide(_controls.LBL_LEFT_ARROW);
    hide(_controls.LBL_RIGHT_ARROW);
    hide(_controls.LBL_CMBTEFCTINC1);
    hide(_controls.LBL_CMBTEFCTINC2);
    hide(_controls.LBL_CMBTEFCTINC3);
    hide(_controls.LBL_CMBTEFCTRED1);
    hide(_controls.LBL_CMBTEFCTRED2);
    hide(_controls.LBL_CMBTEFCTRED3);
    hide(_controls.LBL_DEBILATATED1);
    hide(_controls.LBL_DEBILATATED2);
    hide(_controls.LBL_DEBILATATED3);
    hide(_controls.LBL_DISABLE1);
    hide(_controls.LBL_DISABLE2);
    hide(_controls.LBL_DISABLE3);
    hide(_controls.PB_FORCE1);

    auto footer = _footer ? _footer() : InGameMenuFooter {};
    std::array<std::shared_ptr<Label>, 3> backLabels {
        _controls.LBL_BACK1,
        _controls.LBL_BACK2,
        _controls.LBL_BACK3};
    std::array<std::shared_ptr<Label>, 3> portraitLabels {
        _controls.LBL_CHAR1,
        _controls.LBL_CHAR2,
        _controls.LBL_CHAR3};
    std::array<std::shared_ptr<Label>, 3> levelUpLabels {
        _controls.LBL_LEVELUP1,
        _controls.LBL_LEVELUP2,
        _controls.LBL_LEVELUP3};

    for (int i = 0; i < 3; ++i) {
        const auto &member = footer.members[i];
        if (!member.present) {
            hide(backLabels[i]);
            hide(portraitLabels[i]);
            hide(levelUpLabels[i]);
            continue;
        }

        backLabels[i]->setVisible(true);
        portraitLabels[i]->setVisible(true);
        portraitLabels[i]->setBorderFill(member.portrait);
        levelUpLabels[i]->setVisible(member.levelUp);
    }

    if (!footer.subjectPresent) {
        hide(_controls.LBL_CHARNAME);
        hide(_controls.LBL_TOP_CLASS1);
        hide(_controls.LBL_TOP_CLASS1LEVEL);
        hide(_controls.LBL_TOP_CLASS2);
        hide(_controls.LBL_TOP_CLASS2LEVEL);
        hide(_controls.LBL_TOP_CLASS3);
        hide(_controls.LBL_TOP_CLASS3LEVEL);
        hide(_controls.PB_VIT1);
        return;
    }

    _controls.LBL_CHARNAME->setVisible(true);
    _controls.LBL_CHARNAME->setTextMessage(footer.name);

    _controls.LBL_TOP_CLASS1->setVisible(true);
    _controls.LBL_TOP_CLASS1->setTextMessage(footer.classes[0]);
    _controls.LBL_TOP_CLASS1LEVEL->setVisible(true);
    _controls.LBL_TOP_CLASS1LEVEL->setTextMessage(footer.levels[0]);
    _controls.LBL_TOP_CLASS2->setVisible(true);
    _controls.LBL_TOP_CLASS2->setTextMessage(footer.classes[1]);
    _controls.LBL_TOP_CLASS2LEVEL->setVisible(true);
    _controls.LBL_TOP_CLASS2LEVEL->setTextMessage(footer.levels[1]);

    // A K2 prestige class (the K2 systems merge's third class slot).
    bool hasThirdClass = !footer.classes[2].empty();
    if (_controls.LBL_TOP_CLASS3) {
        _controls.LBL_TOP_CLASS3->setVisible(hasThirdClass);
        _controls.LBL_TOP_CLASS3->setTextMessage(footer.classes[2]);
    }
    if (_controls.LBL_TOP_CLASS3LEVEL) {
        _controls.LBL_TOP_CLASS3LEVEL->setVisible(hasThirdClass);
        _controls.LBL_TOP_CLASS3LEVEL->setTextMessage(footer.levels[2]);
    }

    _controls.PB_VIT1->setVisible(true);
    _controls.PB_VIT1->setValue(footer.vitalityPercent);
}

void InGameMenuHost::updateTabButtons() {
    if (!_gui)
        return;
    for (const auto &[tab, button] : std::vector<std::pair<InGameMenuTab, std::shared_ptr<Button>>> {
             {InGameMenuTab::Equipment, _controls.BTN_EQU}, {InGameMenuTab::Inventory, _controls.BTN_INV}, {InGameMenuTab::Character, _controls.BTN_CHAR}, {InGameMenuTab::Abilities, _controls.BTN_ABI}, {isTSL() ? InGameMenuTab::Party : InGameMenuTab::Messages, _controls.BTN_MSG}, {InGameMenuTab::Journal, _controls.BTN_JOU}, {InGameMenuTab::Map, _controls.BTN_MAP}, {InGameMenuTab::Options, _controls.BTN_OPT}}) {
        if (button)
            button->setDisabled(!available(tab));
    }
    _controls.LBLH_EQU->setSelected(_tab == InGameMenuTab::Equipment);
    _controls.LBLH_INV->setSelected(_tab == InGameMenuTab::Inventory);
    _controls.LBLH_CHA->setSelected(_tab == InGameMenuTab::Character);
    _controls.LBLH_ABI->setSelected(_tab == InGameMenuTab::Abilities);
    _controls.LBLH_MSG->setSelected(_tab == (isTSL() ? InGameMenuTab::Party : InGameMenuTab::Messages));
    _controls.LBLH_JOU->setSelected(_tab == InGameMenuTab::Journal || (isTSL() && _tab == InGameMenuTab::Messages));
    _controls.LBLH_MAP->setSelected(_tab == InGameMenuTab::Map);
    _controls.LBLH_OPT->setSelected(_tab == InGameMenuTab::Options);
}

std::shared_ptr<Button> InGameMenuHost::getBtnChange2() {
    return isTSL() ? findControl<Button>("BTN_CHANGE2") : nullptr;
}

std::shared_ptr<Button> InGameMenuHost::getBtnChange3() {
    return isTSL() ? findControl<Button>("BTN_CHANGE3") : nullptr;
}

bool InGameMenuHost::available(InGameMenuTab tab) const {
    auto entry = _screens.find(tab);
    return entry != _screens.end() && entry->second.enabled && entry->second.screen;
}

PresentationGUI *InGameMenuHost::getActiveTabGUI() const {
    return available(_tab) ? _screens.at(_tab).screen.get() : nullptr;
}

void InGameMenuHost::registerScreen(InGameMenuTab tab, std::shared_ptr<PresentationGUI> screen) {
    if (tab == _tab)
        changeTab(InGameMenuTab::None);
    _screens[tab].screen = std::move(screen);
    updateTabButtons();
}

void InGameMenuHost::setEnabled(InGameMenuTab tab, bool enabled) {
    if (!enabled && tab == _tab)
        changeTab(InGameMenuTab::None);
    _screens[tab].enabled = enabled;
    updateTabButtons();
}

void InGameMenuHost::navigate(InGameMenuTab tab) {
    if (!available(tab))
        return;
    if (_navigate)
        _navigate(tab);
    else
        changeTab(tab);
}

} // namespace game
} // namespace reone
