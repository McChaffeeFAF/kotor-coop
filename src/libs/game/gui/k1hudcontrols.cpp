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

#include "reone/game/gui/k1hudcontrols.h"

#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/gui.h"

#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/types.h"

using namespace reone::gui;

namespace reone {

namespace game {

// Authored-space geometry. K1's quick-bar slot is 37x60; the bar draws its
// icons 26 px square at the slot centre.
static constexpr int kGroupGap = 4;
static constexpr int kSlotIcon = 26;
static constexpr int kSetCell = 27;
static constexpr int kToggleSize = 35;

Control::Extent K1HUDControls::defaultFirstQuickSlot() {
    return {615, 537, 37, 60};
}

K1HUDControls::Layout K1HUDControls::layoutFor(const Control::Extent &slot) {
    Layout layout;
    const int width = slot.width > 0 ? slot.width : 37;
    const int height = slot.height > 0 ? slot.height : 60;

    // Swap: one more quick-bar slot, left of slot 0.
    layout.swap = {slot.left - kGroupGap - width, slot.top, width, height};
    // The icon at the quick bar's own icon size, centred as the bar draws its.
    layout.swapIcon = {layout.swap.left + (width - kSlotIcon) / 2, layout.swap.top + (height - kSlotIcon) / 2,
                       kSlotIcon, kSlotIcon};

    // Indicator: a slot-sized frame with the two sets stacked, in hand on top.
    layout.indicator = {layout.swap.left - kGroupGap - width, slot.top, width, height};
    const int cellLeft = layout.indicator.left + (width - kSetCell) / 2;
    const int spare = std::max(0, height - 2 * kSetCell);
    layout.activeSet = {cellLeft, slot.top + spare / 3, kSetCell, kSetCell};
    layout.stowedSet = {cellLeft, slot.top + height - spare / 3 - kSetCell, kSetCell, kSetCell};

    // Auto-combat: a 35x35 toggle (the size of K1's pause/solo/stealth),
    // bottom-aligned with the slots.
    layout.autoCombat = {layout.indicator.left - kGroupGap - kToggleSize,
                         slot.top + height - kToggleSize, kToggleSize, kToggleSize};
    const int ringInset = 6;
    layout.autoCombatIcon = {layout.autoCombat.left + ringInset, layout.autoCombat.top + ringInset,
                             kToggleSize - 2 * ringInset, kToggleSize - 2 * ringInset};
    return layout;
}

static void placeAuthored(Control &control, const Control::Extent &extent) {
    control.setAuthoredExtent(extent);
    control.setExtent(extent);
}

std::shared_ptr<Label> K1HUDControls::addLabel(const std::string &tag, const Control::Extent &extent,
                                               const std::string &fill) {
    auto label = std::static_pointer_cast<Label>(std::shared_ptr<Control>(_gui.newControl(ControlType::Label, tag)));
    label->setBorder(Control::Border());
    if (!fill.empty()) {
        label->setBorderFill(fill);
    }
    label->setSelectable(false);
    label->setSharpenBorderFillAlpha(true);
    placeAuthored(*label, extent);
    _gui.addControlToBack(label, IGUI::ControlCoordinates::Authored);
    return label;
}

void K1HUDControls::build(const Control::Extent &firstQuickSlot,
                          std::function<void()> onSwap,
                          std::function<void()> onAutoCombat) {
    Layout layout = layoutFor(firstQuickSlot);

    // Children render in order and hit-test last first, so each frame goes in
    // before its icon, and icons are never selectable.
    _swap = std::static_pointer_cast<Button>(std::shared_ptr<Control>(_gui.newControl(ControlType::Button, "BTN_K1SWAPWEAPONS")));
    _swap->setBorder(Control::Border());
    _swap->setBorderFill(kSlotFrame);
    _swap->setHilightFill(kSlotFrameHilight);
    placeAuthored(*_swap, layout.swap);
    _swap->setOnClick(std::move(onSwap));
    _gui.addControlToBack(_swap, IGUI::ControlCoordinates::Authored);
    _swapIcon = addLabel("LBL_K1SWAPWEAPONS", layout.swapIcon, kSwapIcon);

    _indicator = addLabel("LBL_K1WEAPONSETS", layout.indicator, kIndicatorFrame);
    _activeSetFrame = addLabel("LBL_K1WEAPONSET_ACTIVE_FRAME", layout.activeSet, kSetFrameActive);
    _stowedSetFrame = addLabel("LBL_K1WEAPONSET_STOWED_FRAME", layout.stowedSet, kSetFrameStowed);
    _activeSet = addLabel("LBL_K1WEAPONSET_ACTIVE", layout.activeSet, "");
    _stowedSet = addLabel("LBL_K1WEAPONSET_STOWED", layout.stowedSet, "");
    _stowedSet->setTintBorderFill(true);
    _stowedSet->setBorderColor(glm::vec3(kStowedDim));

    _autoCombat = std::static_pointer_cast<Button>(std::shared_ptr<Control>(_gui.newControl(ControlType::Button, "BTN_AUTOCOMBAT")));
    _autoCombat->setBorder(Control::Border());
    _autoCombat->setBorderFill(kToggleOff);
    _autoCombat->setHilightFill(kSlotFrameHilight);
    placeAuthored(*_autoCombat, layout.autoCombat);
    _autoCombat->setOnClick(std::move(onAutoCombat));
    _gui.addControlToBack(_autoCombat, IGUI::ControlCoordinates::Authored);
    _autoCombatIcon = addLabel("LBL_AUTOCOMBAT", layout.autoCombatIcon, kAutoCombatIcon);
    _autoCombatIcon->setTintBorderFill(true);

    update(nullptr, false);
}

std::shared_ptr<Item> K1HUDControls::setItem(const Creature &creature, bool stowed) {
    const int mainHand = stowed ? InventorySlots::rightWeapon2 : InventorySlots::rightWeapon;
    const int offHand = stowed ? InventorySlots::leftWeapon2 : InventorySlots::leftWeapon;
    for (int slot : {mainHand, offHand}) {
        if (auto item = creature.getEquippedItem(slot)) {
            return item;
        }
    }
    return nullptr;
}

static std::shared_ptr<graphics::Texture> iconOf(const std::shared_ptr<Item> &item) {
    return item ? item->icon() : nullptr;
}

void K1HUDControls::update(const Creature *character, bool inCombat) {
    if (!built()) {
        return;
    }
    const bool present = character != nullptr;
    for (auto control : std::initializer_list<Control *> {
             _swap.get(), _swapIcon.get(), _indicator.get(), _activeSetFrame.get(), _stowedSetFrame.get(),
             _activeSet.get(), _stowedSet.get()}) {
        control->setVisible(present);
    }
    if (present) {
        _activeSet->setBorderFill(iconOf(setItem(*character, false)));
        _stowedSet->setBorderFill(iconOf(setItem(*character, true)));
        // setBorderFill(nullptr) keeps the border; re-apply the dim tint.
        _stowedSet->setBorderColor(glm::vec3(kStowedDim));
    }

    // Usable out of combat too (owner, playtest 3); the U key already was.
    static_cast<void>(inCombat);
    const bool showToggle = present;
    _autoCombat->setVisible(showToggle);
    _autoCombatIcon->setVisible(showToggle);
    const bool on = present && character->autoCombat();
    const char *frame = on ? kToggleOn : kToggleOff;
    if (_autoCombat->borderFillResRef() != frame) {
        _autoCombat->setBorderFill(frame);
    }
    _autoCombatIcon->setBorderColor(autoCombatIconColor(on));
}

} // namespace game

} // namespace reone
