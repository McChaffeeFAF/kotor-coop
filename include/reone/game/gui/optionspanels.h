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

#include "../gui.h"
#include "../keymap.h"

#include "reone/gui/control.h"
#include "reone/gui/scrollview.h"

#include <optional>

namespace reone {

namespace gui {
class Slider;
class ToggleButton;
}

namespace game {

class OptionsPanels;

/** A column of rows that does not fit its panel and so scrolls inside a window. */
struct OptionsScroll {
    std::vector<std::shared_ptr<gui::Control>> controls;
    gui::ScrollView view;
    int barLeft {-1}; /**< where the scroll bar goes; right of the rows when unset */
};

/**
 * One options panel (optionsmain, optgameplay, optsound, ...): its Close /
 * OK / Cancel buttons return to the panel that opened it, its sub-panel
 * buttons open deeper panels, and its check boxes flip remembered settings
 * (design note gui_screens, "Options").
 */
class OptionsPanel : public GameGUI {
public:
    OptionsPanel(OptionsPanels &stack, Game &game, ServicesView &services, std::string base);

    const std::string &base() const { return _base; }
    bool handle(const input::Event &event) override;
    /** Show each check box as its remembered setting. */
    void refresh();

private:
    OptionsPanels &_stack;
    std::string _base;
    std::vector<std::shared_ptr<gui::ToggleButton>> _toggles;
    struct SliderBinding {
        std::shared_ptr<gui::Slider> control;
        std::string key;
    };
    std::vector<SliderBinding> _sliders;
    std::map<std::string, std::string> _stepperLabels; // authored text of the arrow selectors

    // A column too tall for its panel scrolls (mouse wheel, the bar's arrows,
    // pad navigation reaching past the last shown row); rows not wholly in
    // the window are hidden.
    struct ScrollItem {
        std::shared_ptr<gui::Control> control;
        gui::Control::Extent authored;
    };
    gui::ScrollView _scroll;
    int _scrollBarLeft {-1};
    std::vector<ScrollItem> _scrollItems;
    std::shared_ptr<gui::Control> _scrollThumb;
    std::shared_ptr<gui::Control> _scrollTrack;
    void installScroll(OptionsScroll scroll);
    void applyScroll();
    void scrollBy(int rows);
    bool scrollActive() const { return !_scrollItems.empty() && _scroll.scrollable(); }

    void onGUILoaded() override;
    std::string settingKey(const gui::Control &control) const;
    int currentVolume(const std::string &key) const;
    bool toggleDefault(const std::string &tag) const;
    void refreshKeyList();
};

/** A stack of options panels, opened from the main menu or in-game options. */
class OptionsPanels {
public:
    OptionsPanels(Game &game, ServicesView &services) :
        _game(game),
        _services(services) {
    }

    /** Open a panel over the current one. False if its GUI will not load. */
    bool open(const std::string &base);
    /** The GUI a panel request loads under the menu profile ("optionsmain" -> "optionsmain_p"). */
    std::string panelResRef(const std::string &requested) const;
    /** The current panel's Close: back to the previous one, or out. */
    void back();
    /** Open the key-entry popup; the next key pressed binds the action. */
    void beginKeyEntry(KeyAction action);
    const std::optional<KeyAction> &keyEntryAction() const { return _keyEntry; }
    void closeAll();

    bool isOpen() const { return !_stack.empty(); }
    OptionsPanel *active() const;
    const std::vector<std::string> &stack() const { return _stack; }

    /** Called when the last panel closes. */
    void setOnClosed(std::function<void()> fn) { _onClosed = std::move(fn); }

    // Delegation for the screen that hosts the stack.
    bool handle(const input::Event &event);
    void update(float dt);
    void render();

    Game &game() { return _game; }

private:
    Game &_game;
    ServicesView &_services;
    std::map<std::string, std::unique_ptr<OptionsPanel>> _panels;
    std::vector<std::string> _stack;
    std::function<void()> _onClosed;
    std::optional<KeyAction> _keyEntry;
};

} // namespace game

} // namespace reone
