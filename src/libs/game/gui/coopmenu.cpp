/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/gui/coopmenu.h"

#include <algorithm>
#include <cctype>

#include "reone/game/coopprompts.h"
#include "reone/game/coopsession.h"
#include "reone/game/game.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"
#include "reone/gui/gui.h"
#include "reone/gui/guis.h"
#include "reone/net/joincode.h"
#include "reone/resource/exception/notfound.h"
#include "reone/system/clipboard.h"
#include "reone/system/logutil.h"

using namespace reone::gui;

namespace reone {

namespace game {

namespace {

// Typed or pasted: a code (letters, digits, one dash) or ip:port.
constexpr size_t kMaxEntry = 24;

std::string sanitizeEntry(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == ':' || c == '.') {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
        if (out.size() >= kMaxEntry) {
            break;
        }
    }
    return out;
}

} // namespace

CoopMenu::CoopMenu(Game &game, ServicesView &services, Place place) :
    GameGUI(game, services),
    _place(place) {
    _resRef = guiResRef("optionsmain");
}

void CoopMenu::init() {
    // A private copy: the Options screen shares the cached optionsmain, and
    // this screen relabels and rewires its buttons.
    _gui = _presentation.guis.getPrivateCopy(_resRef, [this](IGUI &gui) { preload(gui); });
    if (!_gui) {
        throw resource::ResourceNotFoundException("GUI not found: " + _resRef);
    }
    _gui->setEventListener(*this);
    onGUILoaded();
}

void CoopMenu::preload(IGUI &gui) {
    GameGUI::preload(gui);
}

void CoopMenu::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    centerRootInCanvas(isTSL() ? 800 : 640, isTSL() ? 600 : 480);

    // The five option buttons, top to bottom, become this screen's buttons.
    std::vector<std::shared_ptr<Button>> stack;
    for (const char *tag : {"BTN_GAMEPLAY", "BTN_FEEDBACK", "BTN_AUTOPAUSE", "BTN_GRAPHICS", "BTN_SOUND"}) {
        if (auto button = findControl<Button>(tag)) {
            stack.push_back(button);
        }
    }
    std::sort(stack.begin(), stack.end(), [](const auto &a, const auto &b) { return a->extent().top < b->extent().top; });
    for (size_t i = 0; i < stack.size() && i < 5; ++i) {
        _controls.buttons[i] = stack[i];
    }
    _controls.BTN_BACK = findControl<Button>("BTN_BACK");
    _controls.LBL_TITLE = findControl<Label>("LBL_TITLE");
    _controls.LB_DESC = findControl<ListBox>("LB_DESC");
    if (_controls.LBL_TITLE) {
        _controls.LBL_TITLE->setTextMessage("Co-op");
    }
    if (_controls.LB_DESC) {
        _controls.LB_DESC->setProtoMatchContent(true);
        // The text starts inside the frame, not on its edge.
        if (auto proto = _controls.LB_DESC->protoItemOrNull()) {
            proto->setTextPaddingLeft(8);
        }
    }

    // The code box: the game's own name-entry popup, a private copy too.
    _entryGUI = _presentation.guis.getPrivateCopy(guiResRef("savename"), [this](IGUI &gui) {
        gui.setResolution(isTSL() ? 800 : 640, isTSL() ? 600 : 480);
        gui.setScaling(IGUI::ScalingMode::Center);
    });
    if (_entryGUI) {
        _entryText = std::static_pointer_cast<Label>(_entryGUI->findControl("EDITBOX"));
        _entryOk = std::static_pointer_cast<Button>(_entryGUI->findControl("BTN_OK"));
        _entryCancel = std::static_pointer_cast<Button>(_entryGUI->findControl("BTN_CANCEL"));
        _entryTitle = std::static_pointer_cast<Label>(_entryGUI->findControl("LBL_TITLE"));
        if (_entryTitle) {
            _entryTitle->setTextMessage(prompts().entryTitle());
        }
        if (_entryOk) {
            _entryOk->setTextMessage("Connect");
            _entryOk->setOnClick([this]() { connect(); });
        }
        if (_entryCancel) {
            _entryCancel->setOnClick([this]() { closeEntry(); });
        }
    }
}

void CoopMenu::open() {
    _open = true;
    _shownLayout.clear();
    _shownText.clear();
    refresh();
}

void CoopMenu::close() {
    if (!_open) {
        return;
    }
    closeEntry();
    _open = false;
    if (_onClosed) {
        _onClosed();
    }
}

CoopMenu::View CoopMenu::currentView() {
    auto *coop = _game.coop();
    if (!coop) {
        return View::Idle;
    }
    if (coop->isHost()) {
        return View::Hosting;
    }
    auto progress = coop->joinProgress();
    if (progress == CoopSession::JoinProgress::Joined && _place == Place::InGame) {
        return View::Playing;
    }
    return View::Connecting;
}

void CoopMenu::layout(const std::string &key, std::vector<std::pair<std::string, std::function<void()>>> buttons,
                      const std::string &back) {
    if (key == _shownLayout) {
        return;
    }
    _shownLayout = key;
    for (size_t i = 0; i < 5; ++i) {
        auto &button = _controls.buttons[i];
        if (!button) {
            continue;
        }
        if (i < buttons.size()) {
            button->setTextMessage(buttons[i].first);
            button->setOnClick(buttons[i].second);
            button->setVisible(true);
            button->setDisabled(false);
        } else {
            button->setVisible(false);
            button->setOnClick(nullptr);
        }
    }
    if (_controls.BTN_BACK) {
        _controls.BTN_BACK->setTextMessage(back);
        _controls.BTN_BACK->setOnClick([this]() {
            if (currentView() == View::Connecting) {
                leave(); // Cancel: stop trying
            } else {
                close();
            }
        });
    }
}

void CoopMenu::showText(const std::string &text) {
    if (text == _shownText || !_controls.LB_DESC) {
        return;
    }
    _shownText = text;
    _controls.LB_DESC->clearItems();
    _controls.LB_DESC->addTextLinesAsItems(text);
}

std::string CoopMenu::hostText() {
    auto *coop = _game.coop();
    auto online = coop->onlineStatus();
    std::string text;
    if (!online.online) {
        text = prompts().hostWithoutService();
    } else if (online.state == "hosted" && !online.code.empty()) {
        text = "Join code:   " + net::displayJoinCode(online.code) +
               "\n\nGive this code to your friends. It works while you host; New code cancels it.";
    } else if (online.state == "expired") {
        text = "The join code expired. Choose New code for another.";
    } else if (online.state == "offline") {
        text = "Cannot reach the co-op service" + (online.detail.empty() ? std::string() : " (" + online.detail + ")") +
               ". Retrying; your code will change when it is back.";
        if (online.error == "rate_limited") {
            text = "Too many new codes. Wait a minute, then choose New code.";
        }
    } else {
        text = "Getting a join code from the co-op service...";
    }
    auto players = coop->players();
    text += "\n\nPlayers:\n  " + _game.coopDefaults().playerName + (_game.coopDefaults().playerName.empty() ? "You" : "") + " (host)";
    for (const auto &player : players) {
        text += "\n  " + player.name;
        if (player.waiting) {
            text += " - waiting for your answer";
        } else if (!player.companion.empty()) {
            text += " - " + player.companion;
        } else {
            text += " - waiting for a companion";
        }
    }
    if (_place == Place::MainMenu) {
        text += "\n\nStart a new game or load a save: your friends join you there.";
    }
    return text;
}

std::string CoopMenu::connectingText() {
    auto *coop = _game.coop();
    switch (coop->joinProgress()) {
    case CoopSession::JoinProgress::Waiting:
        return "Connected. Waiting for the host to accept you and for a free companion...";
    case CoopSession::JoinProgress::Reconnecting:
        return "Connection to the host lost. Reconnecting...";
    case CoopSession::JoinProgress::Joined:
        return "Joined. Loading the host's game...";
    default: {
        auto online = coop->onlineStatus();
        if (online.online && online.state == "resolving") {
            return "Connecting... (looking up the code)";
        }
        return "Connecting...";
    }
    }
}

void CoopMenu::refresh() {
    auto view = currentView();
    _view = view;
    auto prompts = this->prompts();
    switch (view) {
    case View::Idle: {
        std::string text;
        if (!_message.empty()) {
            text = _message + "\n\n";
        }
        if (_place == Place::MainMenu) {
            layout("idle-main-modes",
                   {{"Host Story Mode", [this]() { host("story"); }},
                    {"Host Free Mode", [this]() { host("free"); }},
                    {prompts.joinButton(), [this]() { openEntry(); }}},
                   "Back");
            text += "Play the campaign together.\n\nStory Mode: friends play your companions (3 players).\n"
                    "Free Mode: friends bring their own characters (8 players).\n\n" +
                    prompts.idleHelp(true);
        } else {
            layout("idle-game", {{"Host this game", [this]() { host(); }}}, "Close");
            text += prompts.idleHelp(false);
        }
        showText(text);
        break;
    }
    case View::Hosting: {
        bool online = _game.coop()->onlineStatus().online;
        std::vector<std::pair<std::string, std::function<void()>>> buttons;
        if (_place == Place::MainMenu) {
            buttons.push_back({"New Game", [this]() {
                                   auto action = _newGame;
                                   close();
                                   if (action) {
                                       action();
                                   }
                               }});
            buttons.push_back({"Load Game", [this]() {
                                   auto action = _loadGame;
                                   close();
                                   if (action) {
                                       action();
                                   }
                               }});
        }
        if (online) {
            buttons.push_back({"Copy code", [this]() { copyCode(); }});
            buttons.push_back({"New code", [this]() {
                                   if (auto *coop = _game.coop()) {
                                       coop->reissueCode();
                                   }
                               }});
        }
        if (_place == Place::InGame) {
            buttons.push_back(nameTagsButton());
        }
        buttons.push_back({"Stop hosting", [this]() { leave(); }});
        layout(std::string("host-") + (_place == Place::MainMenu ? "main" : "game") + (online ? "-code" : "") + (_game.coopNameTags() ? "-t" : "-n"), std::move(buttons),
               _place == Place::MainMenu ? "Back" : "Close");
        showText((_message.empty() ? std::string() : _message + "\n\n") + hostText());
        break;
    }
    case View::Connecting:
        layout("connecting", {{"Cancel", [this]() { leave(); }}}, "Cancel");
        showText(connectingText());
        break;
    case View::Playing:
        layout("client-game-" + std::string(_game.coopNameTags() ? "tags" : "notags"), {nameTagsButton(), {"Leave the game", [this]() { leave(); }}}, "Close");
        showText("You are playing in the host's game. Leave the game to go back to the main menu.");
        break;
    }
}

std::pair<std::string, std::function<void()>> CoopMenu::nameTagsButton() {
    // Name tags above the players and the guest portraits on the HUD.
    return {std::string("Name tags: ") + (_game.coopNameTags() ? "On" : "Off"), [this]() {
                _game.userSettings().setBool(kCoopNameTagsSetting, !_game.coopNameTags());
                _game.userSettings().save();
                _shownLayout.clear();
                refresh();
            }};
}

void CoopMenu::host(const std::string &mode) {
    std::string error;
    _message.clear();
    if (!_game.startCoopHost(error, mode)) {
        _message = "Cannot host: " + error;
    }
    refresh();
}

void CoopMenu::openEntry() {
    if (!_entryGUI) {
        _message = "The code box did not load.";
        refresh();
        return;
    }
    _entryOpen = true;
    if (_entryTitle) {
        _entryTitle->setTextMessage(prompts().entryTitle());
    }
    _entryInput.clear();
    refreshEntry();
}

void CoopMenu::closeEntry() {
    _entryOpen = false;
}

void CoopMenu::refreshEntry() {
    if (!_entryText) {
        return;
    }
    std::string text = sanitizeEntry(_entryBuffer.empty() ? std::string_view() : _entryBuffer.str());
    auto code = net::normalizeJoinCode(text);
    _entryText->setTextMessage((code.empty() ? text : net::displayJoinCode(code)) + "_");
}

void CoopMenu::connect() {
    std::string text = sanitizeEntry(_entryBuffer.empty() ? std::string_view() : _entryBuffer.str());
    if (text.empty()) {
        return;
    }
    closeEntry();
    _message.clear();
    std::string error;
    if (!_game.startCoopJoin(prompts().joinTarget(text), error)) {
        _message = error;
    }
    refresh();
}

CoopPrompts CoopMenu::prompts() const {
    const auto &defaults = _game.coopDefaults();
    return CoopPrompts::forServer(defaults.service, defaults.port);
}

void CoopMenu::copyCode() {
    auto *coop = _game.coop();
    if (!coop) {
        return;
    }
    auto online = coop->onlineStatus();
    if (online.code.empty()) {
        return;
    }
    bool copied = setClipboardText(net::displayJoinCode(online.code));
    _message = copied ? "Code copied to the clipboard." : "Could not copy the code.";
    refresh();
}

void CoopMenu::leave() {
    _message.clear();
    _endRequested = true;
}

bool CoopMenu::handle(const input::Event &event) {
    if (!_open) {
        return false;
    }
    if (_entryOpen && _entryGUI) {
        if (event.type == input::EventType::KeyDown) {
            const auto &key = event.key;
            if (key.code == input::KeyCode::Return) {
                connect();
                return true;
            }
            if (key.code == input::KeyCode::Escape) {
                closeEntry();
                return true;
            }
            if ((key.mod & input::KeyModifiers::control) && key.code == input::KeyCode::V) {
                // Paste, any format: the box keeps what a code or ip:port can hold.
                if (!key.repeat) {
                    std::string pasted = std::string(_entryBuffer.empty() ? std::string_view() : _entryBuffer.str()) + clipboardText();
                    _entryInput.setText(sanitizeEntry(pasted));
                    refreshEntry();
                }
                return true;
            }
            if (_entryInput.handle(event)) {
                auto text = sanitizeEntry(_entryBuffer.empty() ? std::string_view() : _entryBuffer.str());
                if (text != std::string(_entryBuffer.empty() ? std::string_view() : _entryBuffer.str())) {
                    _entryInput.setText(text);
                }
                refreshEntry();
                return true;
            }
        }
        return _entryGUI->handle(event);
    }
    if (event.type == input::EventType::KeyDown && event.key.code == input::KeyCode::Escape) {
        if (currentView() == View::Connecting) {
            leave();
        } else {
            close();
        }
        return true;
    }
    GameGUI::handle(event);
    return true; // the screen is modal while open
}

void CoopMenu::update(float dt) {
    if (!_open) {
        return;
    }
    if (_endRequested) {
        _endRequested = false;
        closeEntry();
        _game.endCoopSession();
        if (_place == Place::InGame && !_game.coop()) {
            // A client leaving goes back to the main menu with the world; a
            // host keeps playing alone.
            _shownLayout.clear();
        }
    }
    if (auto *coop = _game.coop(); coop && coop->isClient() && coop->joinProgress() == CoopSession::JoinProgress::Failed) {
        _message = coop->failureMessage();
        warn("Co-op: join failed: " + _message);
        _game.endCoopSession();
    }
    if (!_open) {
        return;
    }
    refresh();
    GameGUI::update(dt);
    if (_entryOpen && _entryGUI) {
        _entryGUI->update(dt);
    }
}

void CoopMenu::render() {
    if (!_open) {
        return;
    }
    GameGUI::render();
    if (_entryOpen && _entryGUI) {
        _entryGUI->render();
    }
}

} // namespace game

} // namespace reone
