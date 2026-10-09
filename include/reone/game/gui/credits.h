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

#include "reone/gui/control/listbox.h"

namespace reone {

namespace game {

/**
 * The end credits (StartCreditSequence): credits.2da's texts scrolling up
 * LB_CREDITS. The scroll advances in simulation time (advance, from the
 * tick), so the sequence ends on the same tick at any frame rate.
 */
class CreditsGUI : public GameGUI {
public:
    // Seconds each line takes to scroll past.
    static constexpr float kSecondsPerLine = 0.6f;

    CreditsGUI(Game &game, ServicesView &services) :
        GameGUI(game, services) {
        _resRef = guiResRef("credits");
    }

    bool handle(const input::Event &event) override;

    void start(bool transparentBackground);
    /** One tick of scrolling; false once the last line has gone. */
    bool advance(float dt);
    void finish() { _running = false; }

    bool isRunning() const { return _running; }
    bool transparentBackground() const { return _transparent; }
    int lineCount() const { return _lineCount; }
    int offset() const { return _offset; }

private:
    std::shared_ptr<gui::ListBox> _list;
    bool _running {false};
    bool _transparent {false};
    float _elapsed {0.0f};
    int _lineCount {0};
    int _offset {0};

    void onGUILoaded() override;
};

} // namespace game

} // namespace reone
