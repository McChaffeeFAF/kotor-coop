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

#include "reone/game/gui/confirmpopup.h"

#include "reone/game/game.h"
#include "reone/resource/parser/gff/gui.h"
#include "reone/resource/provider/gffs.h"

#include <algorithm>
#include <cmath>

using namespace reone::gui;

namespace reone {

namespace game {

static const char kIconControlTag[] = "LBL_POPUPICON";

static constexpr int kAuthoredCanvasWidth = 640;
static constexpr int kAuthoredCanvasHeight = 480;
static constexpr int kIconSize = 32;
static constexpr int kIconPadding = 6;

void ConfirmPopup::preload(IGUI &gui) {
    GameGUI::preload(gui);

    // The confirmation dialog is authored for 640x480 in both games. It
    // scales and centers with the game-wide Scaled default from the base
    // preload; an explicit Center here kept it at native size.
    gui.setResolution(kAuthoredCanvasWidth, kAuthoredCanvasHeight);
}

void ConfirmPopup::onGUILoaded() {
    bindControls();

    _controls.BTN_CANCEL->setOnClick([this]() {
        auto callback = std::move(_onCancel);
        hide();
        if (callback) callback();
    });
    _controls.BTN_OK->setOnClick([this]() {
        auto callback = std::move(_onConfirm);
        hide();
        if (callback) callback();
    });
    _controls.LB_MESSAGE->setItemsInteractive(false);
    _controls.LB_MESSAGE->setScrollBarEnabled(false);

    // The authored confirmation panel reserves a second button row. This
    // popup only exposes OK, so retain the top inset below that button instead
    // of leaving the unused row as a large empty footer, and center the
    // trimmed panel in the authored canvas so the Scaled layout lands it
    // mid-screen at any resolution.
    auto &root = _gui->rootControl();
    auto rootAuthored = root.authoredExtent();
    const auto &okAuthored = _controls.BTN_OK->authoredExtent();
    const auto &messageAuthored = _controls.LB_MESSAGE->authoredExtent();
    int topInset = std::max(0, std::min(messageAuthored.top, okAuthored.top));
    rootAuthored.height = std::min(rootAuthored.height, okAuthored.top + okAuthored.height + topInset);
    _choicesRootExtent = rootAuthored;
    _twoChoicesRootExtent = rootAuthored;
    {
        const auto &cancelAuthored = _controls.BTN_CANCEL->authoredExtent();
        _twoChoicesRootExtent.height = std::max(rootAuthored.height,
            confirmPanelHeight(cancelAuthored.top, cancelAuthored.height, topInset));
    }
    root.setAuthoredExtent(std::move(rootAuthored));
    centerRootInCanvas(kAuthoredCanvasWidth, kAuthoredCanvasHeight);
    _trimmedRootExtent = root.authoredExtent();
    _okLabel = _controls.BTN_OK->text().text;
    _cancelLabel = _controls.BTN_CANCEL->text().text;
    createThirdButton(topInset);
    _baseOk = _controls.BTN_OK->authoredExtent();
    _baseCancel = _controls.BTN_CANCEL->authoredExtent();
    _baseMessageList = _controls.LB_MESSAGE->authoredExtent();
    if (_thirdButton) _baseThird = _thirdButton->authoredExtent();
    if (_fourthButton) _baseFourth = _fourthButton->authoredExtent();

    _messageExtent = _controls.LB_MESSAGE->protoItem().extent();
    _iconSize = std::max(1, static_cast<int>(std::lround(kIconSize * _gui->scale())));
    _iconPadding = std::max(1, static_cast<int>(std::lround(kIconPadding * _gui->scale())));

    auto icon = _gui->newControl(ControlType::Label, kIconControlTag);
    Control::Extent iconExtent;
    iconExtent.left = _messageExtent.left;
    iconExtent.top = _messageExtent.top + (_messageExtent.height - _iconSize) / 2;
    iconExtent.width = _iconSize;
    iconExtent.height = _iconSize;
    icon->setExtent(std::move(iconExtent));
    icon->setSharpenBorderFillAlpha(true);
    icon->setVisible(false);
    _icon = std::shared_ptr<Control>(std::move(icon));
    _gui->addControlToFront(_icon, IGUI::ControlCoordinates::Screen);
}

void ConfirmPopup::createThirdButton(int bottomInset) {
    // The authored panel has two button rows. A third one is a copy of the
    // Cancel button one row further down.
    auto gff = _presentationResources.gffs.get(_resRef, resource::ResType::Gui);
    if (!gff) {
        return;
    }
    auto parsed = resource::generated::parseGUI(*gff);
    auto cancel = std::find_if(parsed.CONTROLS.begin(), parsed.CONTROLS.end(),
                               [](const auto &control) { return control.TAG == "BTN_CANCEL"; });
    auto ok = std::find_if(parsed.CONTROLS.begin(), parsed.CONTROLS.end(),
                           [](const auto &control) { return control.TAG == "BTN_OK"; });
    if (cancel == parsed.CONTROLS.end() || ok == parsed.CONTROLS.end()) {
        return;
    }
    int step = cancel->EXTENT.TOP - ok->EXTENT.TOP;
    auto third = *cancel;
    third.TAG = "BTN_THIRD";
    third.EXTENT.TOP = cancel->EXTENT.TOP + step;
    auto fourth = third;
    fourth.TAG = "BTN_FOURTH";
    fourth.EXTENT.TOP = third.EXTENT.TOP + step;
    auto make = [this](const resource::generated::GUI_CONTROLS &authored, std::function<void()> &action) -> std::shared_ptr<Button> {
        auto control = _gui->newControl(ControlType::Button, authored.TAG);
        if (!control) {
            return nullptr;
        }
        control->load(authored);
        control->setVisible(false);
        auto button = std::shared_ptr<Button>(static_cast<Button *>(control.release()));
        button->setOnClick([this, &action]() {
            auto callback = std::move(action);
            hide();
            if (callback) callback();
        });
        _gui->addControlToFront(button, IGUI::ControlCoordinates::Authored);
        return button;
    };
    _thirdButton = make(third, _onThird);
    _fourthButton = _thirdButton ? make(fourth, _onFourth) : nullptr;
    _choicesRootExtent.height = third.EXTENT.TOP + third.EXTENT.HEIGHT + bottomInset;
    _fourChoicesRootExtent = _choicesRootExtent;
    _fourChoicesRootExtent.height = fourth.EXTENT.TOP + fourth.EXTENT.HEIGHT + bottomInset;
}

void ConfirmPopup::showChoices(const std::string &message, std::vector<Choice> choices) {
    show(message);
    std::shared_ptr<Button> buttons[] {_controls.BTN_OK, _controls.BTN_CANCEL, _thirdButton, _fourthButton};
    std::function<void()> *actions[] {&_onConfirm, &_onCancel, &_onThird, &_onFourth};
    for (size_t i = 0; i < 4; ++i) {
        if (!buttons[i]) {
            continue;
        }
        bool used = i < choices.size();
        buttons[i]->setVisible(used);
        if (used) {
            buttons[i]->setTextMessage(choices[i].label);
            *actions[i] = std::move(choices[i].action);
        }
    }
    if (choices.size() == 2) {
        fitPanelForTwoButtons();
    } else if (choices.size() > 3 && _fourthButton) {
        auto root = _fourChoicesRootExtent;
        root.height += _growth;
        _gui->rootControl().setAuthoredExtent(std::move(root));
        centerRootInCanvas(kAuthoredCanvasWidth, kAuthoredCanvasHeight);
    } else if (choices.size() > 2 && _thirdButton) {
        auto root = _choicesRootExtent;
        root.height += _growth;
        _gui->rootControl().setAuthoredExtent(std::move(root));
        centerRootInCanvas(kAuthoredCanvasWidth, kAuthoredCanvasHeight);
    }
}

void ConfirmPopup::fitPanelForTwoButtons() {
    // OK and Cancel stacked: the trimmed panel only holds OK.
    auto root = _twoChoicesRootExtent;
    root.height += _growth;
    _gui->rootControl().setAuthoredExtent(std::move(root));
    centerRootInCanvas(kAuthoredCanvasWidth, kAuthoredCanvasHeight);
}

void ConfirmPopup::show(const std::string &message, std::shared_ptr<graphics::Texture> icon) {
    ++_serial;
    _onConfirm = {};
    _onCancel = {};
    _onThird = {};
    _onFourth = {};
    _controls.BTN_CANCEL->setVisible(false);
    _controls.BTN_OK->setVisible(true);
    _controls.BTN_OK->setTextMessage(_okLabel);
    _controls.BTN_CANCEL->setTextMessage(_cancelLabel);
    if (_fourthButton) {
        _fourthButton->setVisible(false);
    }
    if (_thirdButton) {
        _thirdButton->setVisible(false);
    }
    applyGrowth(0);
    if (_gui->rootControl().authoredExtent().height != _trimmedRootExtent.height) {
        _gui->rootControl().setAuthoredExtent(_trimmedRootExtent);
        _gui->refreshLayout();
    }
    // Reserve a gutter for the icon, if any, before the message is broken
    // into lines.
    bool hasIcon = static_cast<bool>(icon);
    Control::Extent textExtent(_messageExtent);
    if (hasIcon) {
        int gutter = _iconSize + _iconPadding;
        textExtent.left += gutter;
        textExtent.width = std::max(0, textExtent.width - gutter);
        _icon->setBorderFill(std::move(icon));
    }
    _icon->setVisible(hasIcon);
    _controls.LB_MESSAGE->protoItem().setExtent(std::move(textExtent));

    _controls.LB_MESSAGE->clearItems();
    ListBox::Item item;
    item.text = message;
    _controls.LB_MESSAGE->addItem(std::move(item));

    // A message of more lines than the panel was authored for (a four-line
    // tutorial popup) would run under the OK button: make the panel taller.
    if (_controls.LB_MESSAGE->getItemCount() > 0) {
        int growth = growthForMessageLines(_controls.LB_MESSAGE->getItemAt(0)._textLines.size());
        if (growth > 0) {
            applyGrowth(growth);
            auto root = _trimmedRootExtent;
            root.height += growth;
            _gui->rootControl().setAuthoredExtent(std::move(root));
            centerRootInCanvas(kAuthoredCanvasWidth, kAuthoredCanvasHeight);
        }
    }

    _visible = true;
}

int ConfirmPopup::growthForMessageLines(size_t lines) const {
    auto &proto = _controls.LB_MESSAGE->protoItem();
    if (!proto.text().font || lines == 0) {
        return 0;
    }
    // The room the panel gives the text: from the message's top to the OK
    // button's, in authored units.
    float scale = std::max(0.01f, _gui->scale());
    float lineAuthored = proto.text().font->height() * proto.scale() / scale;
    // A line's worth of margin keeps the last line clear of the button.
    float needed = (static_cast<float>(lines) + 1.0f) * lineAuthored;
    float room = static_cast<float>(_baseOk.top - _baseMessageList.top);
    int growth = static_cast<int>(std::ceil(needed - room));
    return std::max(0, growth);
}

void ConfirmPopup::applyGrowth(int growth) {
    _growth = growth;
    auto moved = [growth](const Control::Extent &base) {
        auto e = base;
        e.top += growth;
        return e;
    };
    _controls.BTN_OK->setAuthoredExtent(moved(_baseOk));
    _controls.BTN_CANCEL->setAuthoredExtent(moved(_baseCancel));
    if (_thirdButton) _thirdButton->setAuthoredExtent(moved(_baseThird));
    if (_fourthButton) _fourthButton->setAuthoredExtent(moved(_baseFourth));
    auto list = _baseMessageList;
    list.height += growth;
    _controls.LB_MESSAGE->setAuthoredExtent(std::move(list));
    _gui->refreshLayout();
}

void ConfirmPopup::showConfirm(
    const std::string &message,
    std::function<void()> onConfirm,
    std::function<void()> onCancel) {
    show(message);
    _onConfirm = std::move(onConfirm);
    _onCancel = std::move(onCancel);
    _controls.BTN_CANCEL->setVisible(true);
    fitPanelForTwoButtons();
}

void ConfirmPopup::hide() {
    ++_serial;
    _visible = false;
    _onConfirm = {};
    _onCancel = {};
    _onThird = {};
    _onFourth = {};
}

} // namespace game

} // namespace reone
