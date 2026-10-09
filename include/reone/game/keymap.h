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

#include "reone/input/event.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace reone {

namespace game {

class UserSettings;

/**
 * In-game keyboard actions: the rows of keymap.2da the engine acts on
 * (design note p1_ui, "Key bindings").
 */
enum class KeyAction {
    ActionUp,
    ActionDown,
    ActionLeft,
    ActionRight,
    CameraRotateLeft,
    CameraRotateRight,
    SelectPrev,
    SelectNext,
    ChangeChar,
    PartyActive,
    Freelook,
    Equip,
    Inventory,
    Character,
    SkillsAndFeats,
    Messages,
    Quests,
    Map,
    Options,
    Quicksave,
    Quickload,
    Pause,
    PauseKey,
    Flourish,
    TargetLeftAct,
    TargetMiddleAct,
    TargetRightAct,
    PersonalPowerAct,
    PersonalMedicalAct,
    PersonalOtherAct,
    PersonalMinesAct,
    DefaultAction,
    CancelCombat,
    ClearOneAction,
    Stealth,
    GameMenu,
    Count
};

struct KeyActionInfo {
    KeyAction action;
    const char *name;         // keymap.2da "name"
    input::KeyCode fallback;  // default when keymap.2da has no row
    input::KeyCode engineKey; // key an existing handler reacts to; Unknown when dispatched by action
    bool remappable;
};

const std::vector<KeyActionInfo> &keyActionInfos();
const KeyActionInfo &keyActionInfo(KeyAction action);
std::optional<KeyAction> keyActionByName(const std::string &name);

/** A keymap.2da "character" cell or a stored key name to a key; Unknown if none. */
input::KeyCode parseKeyName(const std::string &name);
/** A stable, readable key name (stored in settings, shown on the controls page). */
std::string keyName(input::KeyCode code);

class Keymap {
public:
    Keymap();

    /** Defaults: fallbacks overridden by (2da name, 2da character) rows. */
    void loadDefaults(const std::vector<std::pair<std::string, std::string>> &rows);
    /** Player bindings stored as "key.<Name>=<key>". */
    void applySettings(const UserSettings &settings);
    void storeSettings(UserSettings &settings) const;
    void resetToDefaults();

    input::KeyCode keyFor(KeyAction action) const { return _keys[static_cast<size_t>(action)]; }
    input::KeyCode defaultKeyFor(KeyAction action) const { return _defaults[static_cast<size_t>(action)]; }
    std::optional<KeyAction> actionFor(input::KeyCode code) const;

    /** Bind; an action that held the key takes this action's old key (a swap). */
    void bind(KeyAction action, input::KeyCode code);

    struct Translation {
        input::KeyCode forward {input::KeyCode::Unknown}; // key handed to the existing handlers
        std::optional<KeyAction> dispatch;                 // action the game performs itself
    };
    /**
     * A pressed key: the engine key the existing handlers see, or the action
     * to dispatch. An engine key whose action was rebound elsewhere is
     * dropped; keys the keymap does not know pass through unchanged.
     */
    Translation translate(input::KeyCode pressed) const;

private:
    std::vector<input::KeyCode> _keys;
    std::vector<input::KeyCode> _defaults;
};

} // namespace game

} // namespace reone
