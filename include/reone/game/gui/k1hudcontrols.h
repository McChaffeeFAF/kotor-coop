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

#include "reone/gui/control.h"

namespace reone {

namespace gui {
class Button;
class IGUI;
class Label;
} // namespace gui

namespace game {

class Creature;
class Item;

/**
 * K1 HUD parity (design note k1hud_parity): the controls a K1 layout lacks
 * for mechanics the build has, drawn with K1 GUI art only.
 *
 * - The weapon-set swap button, styled as a K1 quick-bar slot.
 * - The weapon-set indicator: the in-hand set's weapon lit, the stowed set's
 *   dimmed. The swap exchanges items, so "the active set" is always what is in
 *   hand; the indicator shows both sets' contents rather than a set number.
 * - The auto-combat toggle: dim K1 slot frame when off, K1 gold frame when on.
 *
 * The controls sit left of the quick bar, in the layout's authored space, so
 * every HUD scaling mode places them as it places the quick bar.
 */
class K1HUDControls {
public:
    /** Authored-space extents (800x600 canvas). */
    struct Layout {
        gui::Control::Extent swap;
        gui::Control::Extent swapIcon;
        gui::Control::Extent indicator;
        gui::Control::Extent activeSet;
        gui::Control::Extent stowedSet;
        gui::Control::Extent autoCombat;
        gui::Control::Extent autoCombatIcon;
    };

    /** Places the controls left of the first quick-bar slot's authored extent. */
    static Layout layoutFor(const gui::Control::Extent &firstQuickSlot);
    /** The K1 quick-bar slot 0 extent, used when the layout lacks one. */
    static gui::Control::Extent defaultFirstQuickSlot();

    // K1 GUI art (swpc_tex_gui.erf of K1).
    static constexpr const char *kSlotFrame = "lbl_mibox00";
    static constexpr const char *kSlotFrameHilight = "lbl_mibox00a";
    static constexpr const char *kSwapIcon = "i_2weap01";
    static constexpr const char *kSetFrameActive = "lbl_mibox01";
    static constexpr const char *kSetFrameStowed = "lbl_mibox03";
    static constexpr const char *kIndicatorFrame = "lbl_mibox03";
    static constexpr const char *kToggleOff = "lbl_mibox03";
    static constexpr const char *kToggleOn = "lbl_mibox02";
    static constexpr const char *kAutoCombatIcon = "i_attack";

    /** The combat-queue icon for a queued weapon switch on the K1 HUD. */
    static constexpr const char *kQueuedSwapIcon = kSwapIcon;

    explicit K1HUDControls(gui::IGUI &gui) :
        _gui(gui) {}

    void build(const gui::Control::Extent &firstQuickSlot,
               std::function<void()> onSwap,
               std::function<void()> onAutoCombat);

    /**
     * Per frame. character is the creature the HUD presents (may be null);
     * the swap and indicator show out of combat too, as K2's swap button does;
     * the auto-combat toggle shows in combat, as on the K2 HUD.
     */
    void update(const Creature *character, bool inCombat);

    const std::shared_ptr<gui::Button> &swapButton() const { return _swap; }
    const std::shared_ptr<gui::Button> &autoCombatButton() const { return _autoCombat; }
    const std::shared_ptr<gui::Label> &activeSetIcon() const { return _activeSet; }
    const std::shared_ptr<gui::Label> &stowedSetIcon() const { return _stowedSet; }
    bool built() const { return static_cast<bool>(_swap); }

    /**
     * The item that stands for a weapon set in the indicator: the set's main
     * hand, else its off hand. stowed = the set not in hand (slots 18/19).
     */
    static std::shared_ptr<Item> setItem(const Creature &creature, bool stowed);

    // Tints (multiplying K1 art). Auto-combat on shows K1's gold frame.
    static constexpr float kStowedDim = 0.45f;
    static glm::vec3 autoCombatIconColor(bool on) {
        return on ? glm::vec3(1.0f) : glm::vec3(0.5f);
    }

private:
    gui::IGUI &_gui;

    std::shared_ptr<gui::Button> _swap;
    std::shared_ptr<gui::Label> _swapIcon;
    std::shared_ptr<gui::Label> _indicator;
    std::shared_ptr<gui::Label> _activeSetFrame;
    std::shared_ptr<gui::Label> _stowedSetFrame;
    std::shared_ptr<gui::Label> _activeSet;
    std::shared_ptr<gui::Label> _stowedSet;
    std::shared_ptr<gui::Button> _autoCombat;
    std::shared_ptr<gui::Label> _autoCombatIcon;

    std::shared_ptr<gui::Label> addLabel(const std::string &tag, const gui::Control::Extent &extent,
                                         const std::string &fill);
};

} // namespace game

} // namespace reone
