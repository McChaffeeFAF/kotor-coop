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

#include "reone/game/gui/ingame/messages.h"

#include "reone/game/game.h"
#include "reone/game/creaturestates.h"
#include "reone/game/messagelog.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/gui/control/button.h"
#include "reone/resource/strings.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kStrRefMessages = 1563;
static constexpr int kStrRefDialog = 371;
static constexpr int kStrRefFeedback = 42167;
static constexpr int kStrRefShowFeedback = 42142;
static constexpr int kStrRefShowDialog = 42143;

static const glm::vec3 kFeedbackColor(0.0f, 0.66f, 0.98f);
static const glm::vec3 kCombatColor(0.74f, 0.11f, 0.0f);

void MessagesMenu::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();
    tintK2InGameFooter();
    tintK2InGameHeader();

    _controls.BTN_EXIT->setOnClick([this]() {
        if (isTSL()) {
            _game.openInGameMenu(InGameMenuTab::Journal);
        } else {
            _game.openInGame();
        }
    });
    if (!isTSL()) {
        _controls.BTN_SHOW->setOnClick([this]() {
            toggleMessages();
        });
        for (auto &list : {_controls.LB_MESSAGES, _controls.LB_DIALOG}) {
            if (list) {
                list->setItemsInteractive(false);
                list->setProtoMatchContent(true);
            }
        }
        return;
    }

    useK2ShellTitle(_controls.LBL_MESSAGES);
    enableK2ButtonBodyFill(_controls.BTN_EXIT);
    _controls.LB_DIALOG->setTintBorderFill(true);
    _controls.LB_MESSAGES->setTintBorderFill(true);
    _controls.LB_COMBAT->setTintBorderFill(true);
    _controls.LB_EFFECTS_GOOD->setTintBorderFill(true);
    _controls.LB_EFFECTS_BAD->setTintBorderFill(true);
    _controls.LBL_EFFECTS_GOOD->setTintBorderFill(true);
    _controls.LBL_EFFECTS_BAD->setTintBorderFill(true);

    _controls.BTN_DIALOG->setOnClick([this]() {
        setFilter(Filter::Dialog);
    });
    _controls.BTN_FEEDBACK->setOnClick([this]() {
        setFilter(Filter::Feedback);
    });
    _controls.BTN_COMBAT->setOnClick([this]() {
        setFilter(Filter::Combat);
    });
    _controls.BTN_EFFECTS->setOnClick([this]() {
        setFilter(Filter::Effects);
    });
    for (auto &list : {_controls.LB_MESSAGES, _controls.LB_DIALOG, _controls.LB_COMBAT,
                       _controls.LB_EFFECTS_GOOD, _controls.LB_EFFECTS_BAD}) {
        if (list) {
            list->setItemsInteractive(false);
            list->setProtoMatchContent(true);
        }
    }

    resetFilter();
}

MessagesMenu::Split MessagesMenu::split(const std::deque<MessageLog::Entry> &entries) {
    Split result;
    for (const MessageLog::Entry &entry : entries) {
        if (entry.type & MessageLog::kFeedbackMessageType) {
            result.feedback.push_back(&entry);
            (entry.style == MessageLog::Style::Combat ? result.combat : result.general).push_back(&entry);
        } else if (entry.type & MessageLog::kDialogMessageType) {
            result.dialog.push_back(&entry);
        }
    }
    return result;
}

static void fillList(const std::shared_ptr<ListBox> &list, const std::vector<const MessageLog::Entry *> &entries,
                     std::optional<glm::vec3> color) {
    if (!list) {
        return;
    }
    list->clearItems();
    for (const MessageLog::Entry *entry : entries) {
        gui::ListBox::Item item;
        item.text = entry->text;
        if (color) {
            item.textColor = *color;
        } else {
            item.textColor = entry->style == MessageLog::Style::Combat ? kCombatColor : kFeedbackColor;
        }
        list->addItem(std::move(item));
    }
    list->scrollToBottom();
}

static std::string effectName(resource::IStrings &strings, resource::ITwoDAs &twoDas, const EffectIconEntry &icon) {
    if (icon.descriptionStrRef >= 0) {
        if (auto text = strings.getText(icon.descriptionStrRef); !text.empty()) {
            return text;
        }
    }
    // effecticon.2da ships no descriptions: its label, made readable
    // (FORCE_POWER_SPEED_BURST -> Speed Burst).
    auto table = twoDas.get("effecticon");
    std::string label = table ? table->getString(icon.row, "label") : std::string();
    for (auto prefix : {"FORCE_POWER_", "ITEM_ABILITY_", "DROID_ITEM_"}) {
        if (label.rfind(prefix, 0) == 0) {
            label = label.substr(std::string(prefix).size());
        }
    }
    std::string result;
    bool start = true;
    for (char c : label) {
        if (c == '_') {
            result += ' ';
            start = true;
            continue;
        }
        result += start ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                        : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        start = false;
    }
    return result;
}

void MessagesMenu::refresh() {
    Split lists = split(_game.messageLog().entries());
    // The dialog log: K1's BTN_SHOW page and K2's Dialog filter.
    fillList(_controls.LB_DIALOG, lists.dialog, _baseColor);
    if (isTSL()) {
        fillList(_controls.LB_MESSAGES, lists.general, kFeedbackColor);
        fillList(_controls.LB_COMBAT, lists.combat, kCombatColor);
        // Effects: the leader's effect icons, good and bad (effecticon.2da).
        for (auto list : {_controls.LB_EFFECTS_GOOD, _controls.LB_EFFECTS_BAD}) {
            if (list) {
                list->clearItems();
            }
        }
        if (auto leader = _game.party().getLeader()) {
            for (const auto &icon : creatureEffectIcons(_services.resource.twoDas, *leader)) {
                auto &list = icon.good ? _controls.LB_EFFECTS_GOOD : _controls.LB_EFFECTS_BAD;
                if (!list) {
                    continue;
                }
                gui::ListBox::Item item;
                item.text = effectName(_services.resource.strings, _services.resource.twoDas, icon);
                item.iconTexture = _services.resource.textures.get(icon.icon, graphics::TextureUsage::GUI);
                list->addItem(std::move(item));
            }
        }
        refreshFilterVisibility();
        return;
    }
    fillList(_controls.LB_MESSAGES, lists.feedback, std::nullopt);

    if (_showingFeedback) {
        showFeedbackMessages();
    } else {
        showDialogMessages();
    }
}

void MessagesMenu::showDialogMessages() {
    _controls.LB_MESSAGES->setVisible(false);
    _controls.LB_DIALOG->setVisible(true);
    _controls.LBL_MESSAGES->setTextMessage(
        _services.resource.strings.getText(kStrRefMessages) + " - " +
        _services.resource.strings.getText(kStrRefDialog));
    _controls.BTN_SHOW->setTextMessage(
        _services.resource.strings.getText(kStrRefShowFeedback));
    _showingFeedback = false;
}

void MessagesMenu::showFeedbackMessages() {
    _controls.LB_DIALOG->setVisible(false);
    _controls.LB_MESSAGES->setVisible(true);
    _controls.LBL_MESSAGES->setTextMessage(
        _services.resource.strings.getText(kStrRefMessages) + " - " +
        _services.resource.strings.getText(kStrRefFeedback));
    _controls.BTN_SHOW->setTextMessage(
        _services.resource.strings.getText(kStrRefShowDialog));
    _showingFeedback = true;
}

void MessagesMenu::toggleMessages() {
    if (_showingFeedback) {
        showDialogMessages();
    } else {
        showFeedbackMessages();
    }
}

void MessagesMenu::resetFilter() {
    if (!isTSL()) {
        return;
    }

    setFilter(Filter::Dialog);
}

void MessagesMenu::setFilter(Filter filter) {
    _filter = filter;
    refreshFilterVisibility();
}

void MessagesMenu::refreshFilterVisibility() {
    bool dialog = _filter == Filter::Dialog;
    bool feedback = _filter == Filter::Feedback;
    bool combat = _filter == Filter::Combat;
    bool effects = _filter == Filter::Effects;

    updateK2FilterButton(_controls.BTN_DIALOG, dialog);
    updateK2FilterButton(_controls.BTN_FEEDBACK, feedback);
    updateK2FilterButton(_controls.BTN_COMBAT, combat);
    updateK2FilterButton(_controls.BTN_EFFECTS, effects);

    _controls.LB_DIALOG->setVisible(dialog);
    _controls.LB_MESSAGES->setVisible(feedback);
    _controls.LB_COMBAT->setVisible(combat);
    _controls.LBL_EFFECTS_GOOD->setVisible(effects);
    _controls.LBL_EFFECTS_BAD->setVisible(effects);
    _controls.LB_EFFECTS_GOOD->setVisible(effects);
    _controls.LB_EFFECTS_BAD->setVisible(effects);
}

} // namespace game

} // namespace reone
