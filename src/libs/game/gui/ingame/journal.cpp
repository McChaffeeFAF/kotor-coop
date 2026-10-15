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

#include "reone/game/gui/ingame/journal.h"

#include "reone/game/game.h"
#include "reone/resource/strings.h"

#include <boost/algorithm/string.hpp>

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

void JournalMenu::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();
    tintK2InGameFooter();
    tintK2InGameHeader();

    if (isTSL()) {
        fillK2SectionStrip(_controls.LBL_BAR1, _controls.LBL_BAR2);
        _controls.LB_ITEMS->setTintBorderFill(true);
        _controls.LBL_ITEM_DESCRIPTION->setTintBorderFill(true);
        useK2ShellTitle(_controls.LBL_TITLE);
        enableK2ButtonBodyFill(_controls.BTN_EXIT);
        enableK2ButtonBodyFill(_controls.BTN_MESSAGES);
        _controls.BTN_MESSAGES->setOnClick([this]() {
            _game.openInGameMenu(InGameMenuTab::Messages);
        });
        _controls.BTN_FILTER_PRIORITY->setOnClick([this]() {
            setFilter(Filter::Priority);
        });
        _controls.BTN_FILTER_PLANET->setOnClick([this]() {
            setFilter(Filter::Planet);
        });
        _controls.BTN_FILTER_NAME->setOnClick([this]() {
            setFilter(Filter::Name);
        });
        _controls.BTN_FILTER_TIME->setOnClick([this]() {
            setFilter(Filter::Time);
        });
        updateFilterControls();
    }
    _controls.BTN_EXIT->setOnClick([this]() {
        _game.openInGame();
    });
    // Active / completed quests (both layouts author BTN_SWAPTEXT).
    if (_controls.BTN_SWAPTEXT) {
        _controls.BTN_SWAPTEXT->setDisabled(false);
        _controls.BTN_SWAPTEXT->setOnClick([this]() {
            _showCompleted = !_showCompleted;
            refreshToggleTexts();
            refresh();
        });
    }

    if (!isTSL()) {
        // Quest items: no page yet (design note k1hud_parity_2).
        _controls.BTN_QUESTITEMS->setDisabled(true);
        _filter = Filter::Time;
        if (_controls.BTN_SORT) {
            _controls.BTN_SORT->setDisabled(false);
            _controls.BTN_SORT->setOnClick([this]() {
                _filter = nextK1Sort(_filter);
                refreshToggleTexts();
                refresh();
            });
        }
    }
    refreshToggleTexts();

    _controls.LB_ITEMS->setSelectionMode(ListBox::SelectionMode::OnClick);
    _controls.LB_ITEMS->setOnItemClick([this](const std::string &plotId) {
        refreshEntryText(plotId);
    });
}

JournalMenu::Filter JournalMenu::nextK1Sort(Filter filter) {
    switch (filter) {
    case Filter::Time:
        return Filter::Name;
    case Filter::Name:
        return Filter::Priority;
    case Filter::Priority:
        return Filter::Planet;
    default:
        return Filter::Time;
    }
}

std::vector<JournalMenu::Row> JournalMenu::arrange(std::vector<Row> rows, Filter filter, bool completed) {
    rows.erase(std::remove_if(rows.begin(), rows.end(), [completed](const Row &row) {
                   return row.completed != completed;
               }),
               rows.end());
    std::stable_sort(rows.begin(), rows.end(), [filter](const Row &a, const Row &b) {
        switch (filter) {
        case Filter::Name: {
            auto la = boost::to_lower_copy(a.name);
            auto lb = boost::to_lower_copy(b.name);
            if (la != lb) {
                return la < lb;
            }
            break;
        }
        case Filter::Priority:
            if (a.priority != b.priority) {
                return a.priority < b.priority;
            }
            break;
        case Filter::Planet:
            if (a.planetId != b.planetId) {
                return a.planetId < b.planetId;
            }
            break;
        default:
            break;
        }
        return a.received < b.received;
    });
    return rows;
}

void JournalMenu::refreshToggleTexts() {
    auto &strings = _services.resource.strings;
    if (_controls.BTN_SWAPTEXT && !isTSL()) {
        // K1: the button names the other page (32177 Completed, 32178 Active).
        _controls.BTN_SWAPTEXT->setTextMessage(strings.getText(_showCompleted ? 32178 : 32177));
        // The page title names the page shown.
        if (_controls.LBL_TITLE) {
            _controls.LBL_TITLE->setTextMessage(strings.getText(_showCompleted ? 32177 : 32178));
        }
    }
    if (_controls.BTN_SORT && !isTSL()) {
        int strRef = 32173; // by Order Received
        switch (_filter) {
        case Filter::Name:
            strRef = 32174;
            break;
        case Filter::Priority:
            strRef = 32175;
            break;
        case Filter::Planet:
            strRef = 32176;
            break;
        default:
            break;
        }
        _controls.BTN_SORT->setTextMessage(strings.getText(strRef));
    }
    if (isTSL() && _controls.BTN_SWAPTEXT) {
        updateK2FilterButton(_controls.BTN_SWAPTEXT, _showCompleted);
    }
}

void JournalMenu::refresh() {
    _controls.LB_ITEMS->clearItems();
    _controls.LB_ITEMS->clearSelection();
    _controls.LBL_ITEM_DESCRIPTION->clearItems();

    Journal &journal = _game.journal();
    std::vector<Row> rows;
    int received = 0;
    for (const auto &quest : journal.quests()) {
        Row row;
        row.plotId = quest.plotId;
        row.name = journal.getQuestName(quest.plotId);
        if (row.name.empty()) {
            row.name = quest.plotId;
        }
        if (const auto *category = journal.findCategory(quest.plotId)) {
            row.priority = category->priority;
            row.planetId = category->planetId;
        }
        row.received = received++;
        row.completed = journal.isCompleted(quest.plotId);
        rows.push_back(std::move(row));
    }
    for (auto &row : arrange(std::move(rows), _filter, _showCompleted)) {
        ListBox::Item item;
        item.tag = row.plotId;
        item.text = std::move(row.name);
        _controls.LB_ITEMS->addItem(std::move(item));
    }
}

void JournalMenu::refreshEntryText(const std::string &plotId) {
    _controls.LBL_ITEM_DESCRIPTION->clearItems();

    Journal &journal = _game.journal();
    std::string text(journal.getEntryText(plotId, journal.getEntryState(plotId)));
    if (!text.empty()) {
        _controls.LBL_ITEM_DESCRIPTION->addTextLinesAsItems(text);
    }
}

void JournalMenu::setFilter(Filter filter) {
    _filter = filter;
    updateFilterControls();
    refresh();
}

void JournalMenu::updateFilterControls() {
    updateK2FilterButton(_controls.BTN_FILTER_PRIORITY, _filter == Filter::Priority);
    updateK2FilterButton(_controls.BTN_FILTER_PLANET, _filter == Filter::Planet);
    updateK2FilterButton(_controls.BTN_FILTER_NAME, _filter == Filter::Name);
    updateK2FilterButton(_controls.BTN_FILTER_TIME, _filter == Filter::Time);
}

} // namespace game

} // namespace reone
