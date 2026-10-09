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

#include "reone/game/gui/credits.h"

#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/strings.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"

using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

void CreditsGUI::onGUILoaded() {
    centerRootInCanvas(isTSL() ? 800 : 640, isTSL() ? 600 : 480);
    _list = findControl<ListBox>("LB_CREDITS");
    if (_list) {
        _list->setScrollBarEnabled(false);
        _list->setProtoMatchContent(true);
        _list->setItemsInteractive(false);
        if (auto proto = _list->protoItemOrNull()) {
            Control::Text text(proto->text());
            text.align = Control::TextAlign::CenterCenter;
            proto->setText(text);
        }
    }
}

bool CreditsGUI::handle(const input::Event &event) {
    bool skip = (event.type == input::EventType::KeyDown && !event.key.repeat &&
                 event.key.code == input::KeyCode::Escape) ||
                event.type == input::EventType::MouseButtonDown;
    if (skip && _running) {
        _game.endCreditSequence();
        return true;
    }
    return GameGUI::handle(event);
}

void CreditsGUI::start(bool transparentBackground) {
    _transparent = transparentBackground;
    _elapsed = 0.0f;
    _offset = 0;
    _lineCount = 0;
    _running = true;
    if (!_list) {
        return;
    }
    _list->clearItems();
    auto credits = _services.resource.twoDas.get("credits");
    if (!credits) {
        return;
    }
    // Start below the box, so the first line scrolls in from the bottom.
    int blank = std::max(1, _list->visibleItemCount());
    for (int i = 0; i < blank; ++i) {
        _list->addTextLinesAsItems(" ");
    }
    for (int row = 0; row < credits->getRowCount(); ++row) {
        int strRef = credits->getInt(row, "name", -1);
        if (strRef < 0) {
            continue;
        }
        _list->addTextLinesAsItems(_services.resource.strings.getText(strRef));
        _list->addTextLinesAsItems(" ");
    }
    _lineCount = _list->getItemCount();
    _list->setItemOffset(0);
}

bool CreditsGUI::advance(float dt) {
    if (!_running) {
        return false;
    }
    _elapsed += dt;
    _offset = static_cast<int>(_elapsed / kSecondsPerLine);
    if (_offset >= _lineCount) {
        _running = false;
        return false;
    }
    if (_list) {
        _list->setItemOffset(_offset);
    }
    return true;
}

} // namespace game

} // namespace reone
