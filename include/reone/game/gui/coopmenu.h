/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include "../coopprompts.h"
#include "../gui.h"

#include "reone/gui/textinput.h"
#include "reone/system/textbuffer.h"

namespace reone {

namespace gui {
class Button;
class Label;
class ListBox;
} // namespace gui

namespace game {

/**
 * The Co-op screen (co-op design notes, "Hosting and joining a session";
 * design note coop_online). It is drawn with the game's own options panel
 * (optionsmain, a private copy) and its name-entry popup (savename, also a
 * private copy), so it looks right under the K1 and K2 layouts alike.
 *
 * From the main menu: Host (then New Game or Load Game; the join code shows
 * here and in-game) and Join (a code box: typed or pasted, any case, dash
 * optional; ip:port works too), with Connecting... and the reason when a
 * join fails. Without a join-code service (CoopPrompts) Join asks for the
 * host's IP:port and Host shows the UDP port to forward. In game, from Options: the join code with Copy and New code,
 * the players, Stop hosting or Leave; a single-player game can start
 * hosting from here.
 */
class CoopMenu : public GameGUI {
public:
    enum class Place {
        MainMenu,
        InGame
    };

    CoopMenu(Game &game, ServicesView &services, Place place);

    void init() override;

    void open();
    void close();
    bool isOpen() const { return _open; }

    /** Main menu: what New Game and Load Game do once hosting. */
    void setHostActions(std::function<void()> newGame, std::function<void()> loadGame) {
        _newGame = std::move(newGame);
        _loadGame = std::move(loadGame);
    }
    void setOnClosed(std::function<void()> fn) { _onClosed = std::move(fn); }

    bool handle(const input::Event &event) override;
    void update(float dt) override;
    void render() override;

    /** Harness: the text the screen shows now. */
    const std::string &statusText() const { return _shownText; }

private:
    enum class View {
        Idle,
        Hosting,
        Connecting,
        Playing
    };

    struct Controls {
        std::shared_ptr<gui::Button> buttons[5]; // top to bottom
        std::shared_ptr<gui::Button> BTN_BACK;
        std::shared_ptr<gui::Label> LBL_TITLE;
        std::shared_ptr<gui::ListBox> LB_DESC;
    };

    Place _place;
    bool _open {false};
    Controls _controls;
    View _view {View::Idle};
    std::string _message; // the last failure, shown on the idle page
    std::string _shownText;
    std::string _shownLayout;
    std::function<void()> _newGame;
    std::function<void()> _loadGame;
    std::function<void()> _onClosed;
    bool _endRequested {false};

    // The code box (savename popup)
    std::shared_ptr<gui::IGUI> _entryGUI;
    std::shared_ptr<gui::Label> _entryTitle;
    std::shared_ptr<gui::Label> _entryText;
    std::shared_ptr<gui::Button> _entryOk;
    std::shared_ptr<gui::Button> _entryCancel;
    bool _entryOpen {false};
    TextBuffer _entryBuffer;
    gui::TextInput _entryInput {_entryBuffer, gui::TextInputFlags::digits | gui::TextInputFlags::letters | gui::TextInputFlags::symbols};

    void onGUILoaded() override;
    void preload(gui::IGUI &gui) override;

    CoopPrompts prompts() const;
    View currentView();
    void refresh();
    void layout(const std::string &key, std::vector<std::pair<std::string, std::function<void()>>> buttons,
                const std::string &back);
    void showText(const std::string &text);
    std::string hostText();
    std::string connectingText();

    void host(const std::string &mode = std::string());
    void openEntry();
    void closeEntry();
    void connect();
    void refreshEntry();
    void copyCode();
    void leave();
};

} // namespace game

} // namespace reone
