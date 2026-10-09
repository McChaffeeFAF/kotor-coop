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

#include "reone/game/gui/chargen/prestige.h"

#include "reone/game/gui/chargen/iconselection.h"

#include "reone/game/d20/class.h"
#include "reone/game/d20/classes.h"
#include "reone/game/d20/prestige.h"
#include "reone/game/game.h"
#include "reone/game/gui/chargen.h"
#include "reone/gui/control/iconchain.h"
#include "reone/gui/guis.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/textures.h"

using namespace reone::gui;
using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace game {

// A feat or power icon for each prestige class (ours).
static const char *signatureIcon(ClassType type) {
    switch (type) {
    case ClassType::JediWeaponMaster:
        return "if_sfoclsab01"; // Superior Weapon Focus: Lightsaber
    case ClassType::JediMaster:
        return "if_lsenlight"; // Light Side Enlightenment
    case ClassType::JediWatchman:
        return "ip_forcecamo01"; // Force Camouflage
    case ClassType::SithMarauder:
        return "if_incdmg01"; // Increase Combat Damage
    case ClassType::SithLord:
        return "if_dscorrupt"; // Dark Side Corruption
    case ClassType::SithAssassin:
        return "i_sneak01"; // Sneak Attack
    default:
        return "";
    }
}

void PrestigeSelection::init() {
    _gui = _presentation.guis.getPrivateCopy(_resRef, [this](gui::IGUI &gui) { preload(gui); });
    if (!_gui) {
        throw std::runtime_error("GUI not found: " + _resRef);
    }
    _gui->setEventListener(*this);
    onGUILoaded();
}

void PrestigeSelection::onGUILoaded() {
    bindControls();

    styleChargenTitles(isTSL(), *_controls.STD_SELECTIONS_REMAINING_LBL, *_controls.LB_DESC);

    IconSelectionCallbacks callbacks;
    callbacks.onItemFocus = [this](const std::string &item) {
        select(static_cast<ClassType>(std::stoi(item)));
    };
    callbacks.onItemFocusCleared = []() {};
    callbacks.onItemDoubleClick = [this](const std::string &item) {
        if (select(static_cast<ClassType>(std::stoi(item)))) {
            accept();
        }
    };
    _controls.ICONCHAIN_CLASSES = addIconSelectionChain(
        *_gui,
        isTSL(),
        _presentation.textures,
        "ICONCHAIN_PRESTIGE",
        *_controls.LB_FEATS,
        _hilightColor,
        std::move(callbacks));

    _controls.LB_DESC->setProtoMatchContent(true);
    _controls.LB_FEATS->setVisible(false);
    if (_controls.BTN_RECOMMENDED) {
        _controls.BTN_RECOMMENDED->setVisible(false);
    }
    if (_controls.BTN_SELECT) {
        _controls.BTN_SELECT->setVisible(false);
    }
    _controls.BTN_ACCEPT->setOnClick([this]() {
        accept();
    });
    _controls.BTN_BACK->setOnClick([this]() {
        skip();
    });
}

void PrestigeSelection::reset(std::vector<ClassType> offered) {
    _offered = std::move(offered);
    _selected = ClassType::Invalid;
    refresh();
}

bool PrestigeSelection::select(ClassType type) {
    if (std::find(_offered.begin(), _offered.end(), type) == _offered.end()) {
        return false;
    }
    _selected = type;
    showDescription(type);
    refresh();
    return true;
}

bool PrestigeSelection::accept() {
    if (_selected == ClassType::Invalid) {
        return false;
    }
    _charGen.choosePrestigeClass(_selected);
    return true;
}

void PrestigeSelection::skip() {
    _charGen.declinePrestigeClass();
}

void PrestigeSelection::refresh() {
    if (_controls.SUB_TITLE_LBL) {
        _controls.SUB_TITLE_LBL->setTextMessage("Prestige Class");
    }
    _controls.STD_SELECTIONS_REMAINING_LBL->setTextMessage("Remaining");
    _controls.STD_REMAINING_SELECTIONS_LBL->setTextMessage(_selected == ClassType::Invalid ? "1" : "0");
    _controls.BTN_ACCEPT->setDisabled(_selected == ClassType::Invalid);

    auto &chain = *_controls.ICONCHAIN_CLASSES;
    chain.clearItems();
    chain.clearLinks();
    chain.setColumnCount(kIconSelectionColumnCount);
    chain.setVisible(true);
    auto classes = _services.resource.twoDas.get("classes");
    // Light-side classes on the first row, dark-side on the second.
    for (auto type : _offered) {
        bool dark = type == ClassType::SithMarauder || type == ClassType::SithLord || type == ClassType::SithAssassin;
        int column = 0;
        switch (type) {
        case ClassType::JediMaster:
        case ClassType::SithLord:
            column = 1;
            break;
        case ClassType::JediWatchman:
        case ClassType::SithAssassin:
            column = 2;
            break;
        default:
            break;
        }
        IconChain::Item item;
        item.tag = std::to_string(static_cast<int>(type));
        item.column = column;
        item.row = dark && _offered.size() > 3 ? 1 : 0;
        if (classes && static_cast<int>(type) < classes->getRowCount()) {
            item.iconTexture = _services.resource.textures.get(
                classes->getString(static_cast<int>(type), "icon"), TextureUsage::GUI);
        }
        if (!item.iconTexture) {
            // classes.2da names IR_* icons that neither game ships: each
            // class shows the icon of a feat or power it is known for.
            item.iconTexture = _services.resource.textures.get(signatureIcon(type), TextureUsage::GUI);
        }
        item.state = IconChain::State::Selectable;
        item.selected = type == _selected;
        chain.addItem(std::move(item));
    }
}

std::string PrestigeSelection::describe(ClassType type) const {
    auto clazz = _services.game.classes.get(type);
    if (!clazz) {
        return "";
    }
    bool dark = type == ClassType::SithMarauder || type == ClassType::SithLord || type == ClassType::SithAssassin;
    std::string text = dark ? "Dark side prestige class." : "Light side prestige class.";
    if (!clazz->description().empty()) {
        text += "\n\n" + clazz->description();
    }
    text += str(boost::format("\n\nVitality per level: d%d\nForce points per level: %d\nSkill points per level: %d") %
                clazz->hitdie() % clazz->forcePointsPerLevel() % clazz->skillPointBase());
    text += "\n\nFrom this level on, every level goes into the prestige class. "
            "Cancel keeps your Jedi class for this level; the choice returns at your next level-up.";
    return text;
}

void PrestigeSelection::showDescription(ClassType type) {
    auto clazz = _services.game.classes.get(type);
    _controls.LBL_NAME->setTextMessage(clazz ? clazz->name() : std::string());
    _controls.LB_DESC->clearItems();
    _controls.LB_DESC->addTextLinesAsItems(describe(type));
}

} // namespace game

} // namespace reone
