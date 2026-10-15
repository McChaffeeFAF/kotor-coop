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

#include <gtest/gtest.h>

#include <sstream>

#include <SDL3/SDL_gamepad.h>

#include "reone/game/keymap.h"
#include "reone/game/padinput.h"
#include "reone/game/usersettings.h"
#include "reone/graphics/gamepadevents.h"

using namespace reone;
using namespace reone::game;
using input::GamepadAxis;
using input::GamepadButton;
using input::KeyCode;
using input::NavigateAction;

namespace {

constexpr uint32_t kPad = 7;

input::Event down(GamepadButton button, uint32_t pad = kPad) {
    return input::Event::newGamepadButton({pad, button, true});
}

input::Event up(GamepadButton button, uint32_t pad = kPad) {
    return input::Event::newGamepadButton({pad, button, false});
}

input::Event axis(GamepadAxis which, float value, uint32_t pad = kPad) {
    return input::Event::newGamepadAxis({pad, which, value});
}

input::Event plug(uint32_t pad, bool connected) {
    return input::Event::newGamepadDevice({pad, connected});
}

UserSettings settingsFrom(const std::string &text) {
    UserSettings settings;
    std::istringstream stream(text);
    settings.read(stream);
    return settings;
}

std::string textOf(const UserSettings &settings) {
    std::ostringstream stream;
    settings.write(stream);
    return stream.str();
}

class PadInputTest : public ::testing::Test {
protected:
    PadBindings bindings;
    PadInput pad {bindings};

    std::vector<PadOutput> world(const input::Event &event) { return pad.handle(event, PadMode::World); }
    std::vector<PadOutput> menu(const input::Event &event) { return pad.handle(event, PadMode::Menu); }
    std::vector<PadOutput> minigame(const input::Event &event) { return pad.handle(event, PadMode::Minigame); }

    static void expectAction(const PadOutput &out, KeyAction action, bool pressed) {
        EXPECT_EQ(PadOutput::Kind::Action, out.kind);
        EXPECT_EQ(action, out.action);
        EXPECT_EQ(pressed, out.pressed);
    }
    static void expectKey(const PadOutput &out, KeyCode key, bool pressed) {
        EXPECT_EQ(PadOutput::Kind::Key, out.kind);
        EXPECT_EQ(key, out.key);
        EXPECT_EQ(pressed, out.pressed);
    }
    static void expectNavigate(const PadOutput &out, NavigateAction action) {
        EXPECT_EQ(PadOutput::Kind::Navigate, out.kind);
        EXPECT_EQ(action, out.navigate);
    }
};

} // namespace

// Bindings

TEST(PadBindings, the_defaults_put_the_common_actions_on_the_face_buttons_bumpers_triggers_and_dpad) {
    PadBindings bindings;
    EXPECT_EQ(GamepadButton::A, bindings.buttonFor(KeyAction::DefaultAction));
    EXPECT_EQ(GamepadButton::B, bindings.buttonFor(KeyAction::CancelCombat));
    EXPECT_EQ(GamepadButton::X, bindings.buttonFor(KeyAction::Pause));
    EXPECT_EQ(GamepadButton::Y, bindings.buttonFor(KeyAction::Map));
    EXPECT_EQ(GamepadButton::LeftShoulder, bindings.buttonFor(KeyAction::SelectPrev));
    EXPECT_EQ(GamepadButton::RightShoulder, bindings.buttonFor(KeyAction::SelectNext));
    EXPECT_EQ(GamepadButton::LeftTrigger, bindings.buttonFor(KeyAction::ChangeChar));
    EXPECT_EQ(GamepadButton::RightTrigger, bindings.buttonFor(KeyAction::PartyActive));
    EXPECT_EQ(GamepadButton::DPadLeft, bindings.buttonFor(KeyAction::TargetLeftAct));
    EXPECT_EQ(GamepadButton::DPadUp, bindings.buttonFor(KeyAction::TargetMiddleAct));
    EXPECT_EQ(GamepadButton::DPadRight, bindings.buttonFor(KeyAction::TargetRightAct));
    EXPECT_FALSE(bindings.buttonFor(KeyAction::Inventory));
    EXPECT_FALSE(bindings.buttonFor(KeyAction::GameMenu)) << "Start opens the menu; it is not a binding";
    EXPECT_EQ(KeyAction::Map, bindings.actionFor(GamepadButton::Y));
    EXPECT_FALSE(bindings.actionFor(GamepadButton::Start));
}

TEST(PadBindings, rebinding_swaps_with_the_action_that_held_the_button) {
    PadBindings bindings;
    EXPECT_EQ(KeyAction::Map, bindings.conflict(KeyAction::Inventory, GamepadButton::Y));
    EXPECT_FALSE(bindings.conflict(KeyAction::Map, GamepadButton::Y)) << "an action never conflicts with itself";
    EXPECT_FALSE(bindings.conflict(KeyAction::Inventory, GamepadButton::DPadUp) == std::nullopt);

    EXPECT_TRUE(bindings.bind(KeyAction::Inventory, GamepadButton::Y));
    EXPECT_EQ(GamepadButton::Y, bindings.buttonFor(KeyAction::Inventory));
    EXPECT_FALSE(bindings.buttonFor(KeyAction::Map)) << "Map had Y, Inventory had nothing, so Map goes unbound";

    EXPECT_TRUE(bindings.bind(KeyAction::Pause, GamepadButton::A));
    EXPECT_EQ(GamepadButton::A, bindings.buttonFor(KeyAction::Pause));
    EXPECT_EQ(GamepadButton::X, bindings.buttonFor(KeyAction::DefaultAction)) << "the two swapped";
}

TEST(PadBindings, start_and_guide_and_fixed_actions_refuse_a_rebinding) {
    PadBindings bindings;
    EXPECT_TRUE(PadBindings::reserved(GamepadButton::Start));
    EXPECT_TRUE(PadBindings::reserved(GamepadButton::Guide));
    EXPECT_FALSE(PadBindings::reserved(GamepadButton::A));
    EXPECT_FALSE(bindings.bind(KeyAction::Inventory, GamepadButton::Start));
    EXPECT_FALSE(bindings.bind(KeyAction::Inventory, GamepadButton::Guide));
    EXPECT_FALSE(bindings.bind(KeyAction::GameMenu, GamepadButton::A)) << "the game menu is not remappable";
    EXPECT_FALSE(PadBindings::bindable(KeyAction::GameMenu));
    EXPECT_FALSE(PadBindings::bindable(KeyAction::PauseKey));
    EXPECT_EQ(GamepadButton::A, bindings.buttonFor(KeyAction::DefaultAction)) << "a refused bind changes nothing";
    EXPECT_FALSE(bindings.buttonFor(KeyAction::Inventory));
}

TEST(PadBindings, unbinding_leaves_an_action_without_a_button) {
    PadBindings bindings;
    bindings.unbind(KeyAction::Map);
    EXPECT_FALSE(bindings.buttonFor(KeyAction::Map));
    EXPECT_FALSE(bindings.actionFor(GamepadButton::Y));
    bindings.unbind(KeyAction::GameMenu);
    bindings.resetToDefaults();
    EXPECT_EQ(GamepadButton::Y, bindings.buttonFor(KeyAction::Map));
}

TEST(PadBindings, changed_bindings_save_and_load_and_defaults_are_not_written) {
    PadBindings bindings;
    UserSettings settings;
    bindings.storeSettings(settings);
    EXPECT_TRUE(settings.values().empty()) << "nothing changed, nothing stored";

    bindings.bind(KeyAction::Inventory, GamepadButton::RightStick);
    bindings.unbind(KeyAction::Map);
    bindings.storeSettings(settings);
    EXPECT_EQ("RightStick", settings.get("pad.Inventory"));
    EXPECT_EQ("None", settings.get("pad.Map"));
    EXPECT_EQ("None", settings.get("pad.Flourish")) << "Flourish had RightStick and lost it to Inventory";

    PadBindings loaded;
    auto reread = settingsFrom(textOf(settings));
    loaded.applySettings(reread);
    for (const auto &info : keyActionInfos()) {
        EXPECT_EQ(bindings.buttonFor(info.action), loaded.buttonFor(info.action)) << info.name;
    }

    // Back to the defaults: the keys are erased again.
    loaded.resetToDefaults();
    loaded.storeSettings(reread);
    EXPECT_TRUE(reread.values().empty());
}

TEST(PadBindings, junk_in_the_settings_is_ignored) {
    auto settings = settingsFrom("pad.Map=Banana\npad.Pause=Start\npad.NoSuchAction=A\npad.Inventory=lt\npad.GUI=A\nkey.Inventory=I\n");
    PadBindings bindings;
    bindings.applySettings(settings);
    EXPECT_EQ(GamepadButton::Y, bindings.buttonFor(KeyAction::Map)) << "an unknown button name";
    EXPECT_EQ(GamepadButton::X, bindings.buttonFor(KeyAction::Pause)) << "Start is reserved";
    EXPECT_EQ(GamepadButton::LeftTrigger, bindings.buttonFor(KeyAction::Inventory)) << "short names are understood";
    EXPECT_EQ(KeyAction::Inventory, *bindings.actionFor(GamepadButton::LeftTrigger));
    EXPECT_FALSE(bindings.buttonFor(KeyAction::ChangeChar)) << "which took it from ChangeChar";
    EXPECT_FALSE(bindings.buttonFor(KeyAction::GameMenu));
}

TEST(PadBindings, button_names_round_trip) {
    for (int i = 0; i < static_cast<int>(GamepadButton::Count); ++i) {
        auto button = static_cast<GamepadButton>(i);
        auto name = gamepadButtonName(button);
        ASSERT_TRUE(parseGamepadButton(name)) << name;
        EXPECT_EQ(button, *parseGamepadButton(name)) << name;
    }
    EXPECT_EQ(GamepadButton::Back, parseGamepadButton("select"));
    EXPECT_EQ(GamepadButton::RightShoulder, parseGamepadButton("RB"));
    EXPECT_FALSE(parseGamepadButton(""));
    EXPECT_FALSE(parseGamepadButton("Banana"));
}

TEST(Keymap, conflicts_and_locks_are_reported_before_a_bind) {
    Keymap keymap;
    EXPECT_EQ(KeyAction::Inventory, keymap.conflict(KeyAction::Map, KeyCode::I));
    EXPECT_FALSE(keymap.conflict(KeyAction::Inventory, KeyCode::I)) << "its own key is no conflict";
    EXPECT_FALSE(keymap.conflict(KeyAction::Map, KeyCode::Key9)) << "a free key";
    EXPECT_FALSE(keymap.locked(KeyAction::Map, KeyCode::I));
    EXPECT_TRUE(keymap.locked(KeyAction::Map, KeyCode::Escape)) << "Escape belongs to the game menu";
    EXPECT_TRUE(keymap.locked(KeyAction::GameMenu, KeyCode::Key9)) << "and the game menu cannot move";
}

TEST(Keymap, an_action_from_elsewhere_translates_as_its_own_key_would) {
    Keymap keymap;
    auto forward = keymap.translateAction(KeyAction::ActionUp);
    EXPECT_EQ(KeyCode::W, forward.forward) << "walking is the existing handlers' key";
    EXPECT_FALSE(forward.dispatch);
    auto dispatch = keymap.translateAction(KeyAction::Inventory);
    EXPECT_TRUE(dispatch.dispatch);
    EXPECT_EQ(KeyAction::Inventory, *dispatch.dispatch);
    keymap.bind(KeyAction::Inventory, KeyCode::Key9);
    EXPECT_EQ(KeyAction::Inventory, *keymap.translateAction(KeyAction::Inventory).dispatch) << "whatever its key is now";
    keymap.bind(KeyAction::ActionUp, KeyCode::Key8);
    EXPECT_EQ(KeyCode::W, keymap.translateAction(KeyAction::ActionUp).forward) << "and the pad still walks after the key is rebound";
}

// In the world

TEST_F(PadInputTest, buttons_do_their_bound_actions_while_held) {
    pad.handle(plug(kPad, true), PadMode::World);
    auto pressed = world(down(GamepadButton::A));
    ASSERT_EQ(1u, pressed.size());
    expectAction(pressed[0], KeyAction::DefaultAction, true);
    EXPECT_TRUE(world(down(GamepadButton::A)).empty()) << "a repeat of the same press";
    auto released = world(up(GamepadButton::A));
    ASSERT_EQ(1u, released.size());
    expectAction(released[0], KeyAction::DefaultAction, false);
    EXPECT_TRUE(world(up(GamepadButton::A)).empty());

    EXPECT_TRUE(world(down(GamepadButton::Guide)).empty()) << "an unbound button does nothing";
    EXPECT_TRUE(world(up(GamepadButton::Guide)).empty());
}

TEST_F(PadInputTest, start_opens_the_menu_as_escape_does) {
    auto pressed = world(down(GamepadButton::Start));
    ASSERT_EQ(1u, pressed.size());
    expectKey(pressed[0], KeyCode::Escape, true);
    auto released = world(up(GamepadButton::Start));
    ASSERT_EQ(1u, released.size());
    expectKey(released[0], KeyCode::Escape, false);
}

TEST_F(PadInputTest, the_dpad_triggers_and_bumpers_use_their_slots) {
    auto slot = [&](GamepadButton button, KeyAction action) {
        auto outs = world(down(button));
        ASSERT_EQ(1u, outs.size()) << gamepadButtonName(button);
        expectAction(outs[0], action, true);
        world(up(button));
    };
    slot(GamepadButton::DPadLeft, KeyAction::TargetLeftAct);
    slot(GamepadButton::DPadUp, KeyAction::TargetMiddleAct);
    slot(GamepadButton::DPadRight, KeyAction::TargetRightAct);
    slot(GamepadButton::LeftShoulder, KeyAction::SelectPrev);
    slot(GamepadButton::RightShoulder, KeyAction::SelectNext);

    // A trigger is pulled past half to press and let off below a third.
    EXPECT_TRUE(world(axis(GamepadAxis::LeftTrigger, 0.4f)).empty());
    auto pulled = world(axis(GamepadAxis::LeftTrigger, 0.6f));
    ASSERT_EQ(1u, pulled.size());
    expectAction(pulled[0], KeyAction::ChangeChar, true);
    EXPECT_TRUE(world(axis(GamepadAxis::LeftTrigger, 0.35f)).empty()) << "still held between the two";
    auto let = world(axis(GamepadAxis::LeftTrigger, 0.1f));
    ASSERT_EQ(1u, let.size());
    expectAction(let[0], KeyAction::ChangeChar, false);
    auto solo = world(axis(GamepadAxis::RightTrigger, 1.0f));
    ASSERT_EQ(1u, solo.size());
    expectAction(solo[0], KeyAction::PartyActive, true);
}

TEST_F(PadInputTest, the_left_stick_walks_and_the_right_stick_turns_the_camera) {
    auto walking = [&](GamepadAxis which, float value, KeyAction action, bool pressed) {
        auto outs = world(axis(which, value));
        ASSERT_EQ(1u, outs.size()) << static_cast<int>(which) << " " << value;
        expectAction(outs[0], action, pressed);
    };
    walking(GamepadAxis::LeftY, -0.9f, KeyAction::ActionUp, true);
    walking(GamepadAxis::LeftY, 0.0f, KeyAction::ActionUp, false);
    walking(GamepadAxis::LeftY, 0.9f, KeyAction::ActionDown, true);
    walking(GamepadAxis::LeftY, 0.0f, KeyAction::ActionDown, false);
    walking(GamepadAxis::LeftX, -1.0f, KeyAction::ActionLeft, true);
    walking(GamepadAxis::LeftX, 0.0f, KeyAction::ActionLeft, false);
    walking(GamepadAxis::LeftX, 1.0f, KeyAction::ActionRight, true);
    walking(GamepadAxis::LeftX, 0.0f, KeyAction::ActionRight, false);
    walking(GamepadAxis::RightX, -0.8f, KeyAction::CameraRotateLeft, true);
    walking(GamepadAxis::RightX, 0.0f, KeyAction::CameraRotateLeft, false);
    walking(GamepadAxis::RightX, 0.8f, KeyAction::CameraRotateRight, true);
    walking(GamepadAxis::RightX, 0.0f, KeyAction::CameraRotateRight, false);
    EXPECT_TRUE(world(axis(GamepadAxis::RightY, 1.0f)).empty()) << "the camera has no vertical turn";
}

TEST_F(PadInputTest, a_stick_has_a_dead_zone_and_a_little_hysteresis) {
    EXPECT_TRUE(world(axis(GamepadAxis::LeftY, -0.2f)).empty()) << "inside the dead zone";
    EXPECT_TRUE(world(axis(GamepadAxis::LeftY, -0.45f)).empty()) << "out of the dead zone but not yet at half travel";
    auto pressed = world(axis(GamepadAxis::LeftY, -0.55f));
    ASSERT_EQ(1u, pressed.size());
    expectAction(pressed[0], KeyAction::ActionUp, true);
    EXPECT_TRUE(world(axis(GamepadAxis::LeftY, -0.35f)).empty()) << "eased off but not below the dead zone: still walking";
    EXPECT_TRUE(world(axis(GamepadAxis::LeftY, -0.9f)).empty()) << "pushing further is not another press";
    auto released = world(axis(GamepadAxis::LeftY, -0.2f));
    ASSERT_EQ(1u, released.size());
    expectAction(released[0], KeyAction::ActionUp, false);

    pad.setDeadZone(0.6f);
    EXPECT_FLOAT_EQ(0.6f, pad.deadZone());
    EXPECT_TRUE(world(axis(GamepadAxis::LeftY, -0.62f)).empty()) << "a larger dead zone raises the press point too";
    EXPECT_EQ(1u, world(axis(GamepadAxis::LeftY, -0.7f)).size());
    pad.setDeadZone(5.0f);
    EXPECT_FLOAT_EQ(0.9f, pad.deadZone());
    pad.setDeadZone(-1.0f);
    EXPECT_FLOAT_EQ(0.0f, pad.deadZone());
}

TEST_F(PadInputTest, a_stick_swung_across_lets_go_of_one_direction_and_takes_the_other) {
    world(axis(GamepadAxis::LeftX, -1.0f));
    auto swing = world(axis(GamepadAxis::LeftX, 1.0f));
    ASSERT_EQ(2u, swing.size());
    expectAction(swing[0], KeyAction::ActionLeft, false);
    expectAction(swing[1], KeyAction::ActionRight, true);
}

TEST_F(PadInputTest, junk_axis_values_are_harmless) {
    EXPECT_TRUE(world(axis(GamepadAxis::LeftY, std::numeric_limits<float>::quiet_NaN())).empty());
    EXPECT_TRUE(world(axis(GamepadAxis::LeftY, std::numeric_limits<float>::infinity())).size() <= 1u);
    EXPECT_TRUE(menu(input::Event::newKeyDown(input::KeyEvent(true, KeyCode::W, 0, false))).empty()) << "not a pad event";
}

TEST_F(PadInputTest, rebinding_changes_what_a_button_does_and_a_held_button_lets_go_of_what_it_pressed) {
    world(down(GamepadButton::A));
    bindings.bind(KeyAction::Inventory, GamepadButton::A); // while A is held
    auto released = world(up(GamepadButton::A));
    ASSERT_EQ(1u, released.size());
    expectAction(released[0], KeyAction::DefaultAction, false);
    auto pressed = world(down(GamepadButton::A));
    ASSERT_EQ(1u, pressed.size());
    expectAction(pressed[0], KeyAction::Inventory, true);
}

// Menus

TEST_F(PadInputTest, in_a_menu_the_dpad_and_stick_move_focus_a_uses_and_b_backs_out) {
    auto right = menu(down(GamepadButton::DPadRight));
    ASSERT_EQ(1u, right.size());
    expectNavigate(right[0], NavigateAction::Right);
    menu(up(GamepadButton::DPadRight));

    auto accept = menu(down(GamepadButton::A));
    ASSERT_EQ(1u, accept.size());
    expectNavigate(accept[0], NavigateAction::Accept);
    menu(up(GamepadButton::A));

    auto back = menu(down(GamepadButton::B));
    ASSERT_EQ(1u, back.size());
    expectKey(back[0], KeyCode::Escape, true);
    auto backUp = menu(up(GamepadButton::B));
    ASSERT_EQ(1u, backUp.size());
    expectKey(backUp[0], KeyCode::Escape, false);

    auto stick = menu(axis(GamepadAxis::LeftY, 0.9f));
    ASSERT_EQ(1u, stick.size());
    expectNavigate(stick[0], NavigateAction::Down);
    menu(axis(GamepadAxis::LeftY, 0.0f));
    EXPECT_TRUE(menu(down(GamepadButton::X)).empty()) << "world buttons do nothing in a menu";
    EXPECT_TRUE(menu(down(GamepadButton::Y)).empty());
}

TEST_F(PadInputTest, a_held_menu_direction_repeats_after_a_delay_until_it_is_let_go) {
    menu(down(GamepadButton::DPadDown));
    EXPECT_TRUE(pad.update(0.3f, PadMode::Menu).empty()) << "inside the delay";
    auto first = pad.update(0.15f, PadMode::Menu);
    ASSERT_EQ(1u, first.size());
    expectNavigate(first[0], NavigateAction::Down);
    EXPECT_TRUE(pad.update(0.05f, PadMode::Menu).empty());
    auto more = pad.update(0.25f, PadMode::Menu);
    EXPECT_EQ(2u, more.size()) << "two intervals passed";
    menu(up(GamepadButton::DPadDown));
    EXPECT_TRUE(pad.update(1.0f, PadMode::Menu).empty());

    // The stick repeats the same way, and letting it go stops it.
    menu(axis(GamepadAxis::LeftX, -1.0f));
    EXPECT_EQ(1u, pad.update(0.5f, PadMode::Menu).size());
    menu(axis(GamepadAxis::LeftX, 0.0f));
    EXPECT_TRUE(pad.update(1.0f, PadMode::Menu).empty());
}

TEST_F(PadInputTest, changing_the_screen_lets_go_of_what_was_held) {
    world(down(GamepadButton::A));
    world(axis(GamepadAxis::LeftY, -1.0f));
    auto outs = menu(down(GamepadButton::DPadLeft));
    // Both held world inputs are released, then the menu press follows.
    ASSERT_EQ(3u, outs.size());
    bool aReleased = false;
    bool walkReleased = false;
    for (size_t i = 0; i < 2; ++i) {
        ASSERT_EQ(PadOutput::Kind::Action, outs[i].kind);
        EXPECT_FALSE(outs[i].pressed);
        aReleased |= outs[i].action == KeyAction::DefaultAction;
        walkReleased |= outs[i].action == KeyAction::ActionUp;
    }
    EXPECT_TRUE(aReleased);
    EXPECT_TRUE(walkReleased);
    expectNavigate(outs[2], NavigateAction::Left);
    EXPECT_TRUE(menu(up(GamepadButton::A)).empty()) << "A was let go of with the mode";
}

// A race or a turret

TEST_F(PadInputTest, in_a_minigame_the_stick_and_dpad_steer_with_arrows_and_a_fires) {
    auto left = minigame(axis(GamepadAxis::LeftX, -1.0f));
    ASSERT_EQ(1u, left.size());
    expectKey(left[0], KeyCode::Left, true);
    auto centre = minigame(axis(GamepadAxis::LeftX, 0.0f));
    ASSERT_EQ(1u, centre.size());
    expectKey(centre[0], KeyCode::Left, false);
    auto up = minigame(down(GamepadButton::DPadUp));
    ASSERT_EQ(1u, up.size());
    expectKey(up[0], KeyCode::Up, true);
    minigame(::up(GamepadButton::DPadUp));
    auto fire = minigame(down(GamepadButton::A));
    ASSERT_EQ(1u, fire.size());
    expectKey(fire[0], KeyCode::Space, true);
    auto trigger = minigame(axis(GamepadAxis::RightTrigger, 1.0f));
    ASSERT_EQ(1u, trigger.size());
    expectKey(trigger[0], KeyCode::Space, true);
    auto leave = minigame(down(GamepadButton::Start));
    ASSERT_EQ(1u, leave.size());
    expectKey(leave[0], KeyCode::Escape, true);
}

TEST_F(PadInputTest, in_a_cutscene_only_back_and_start_do_anything) {
    EXPECT_TRUE(pad.handle(down(GamepadButton::A), PadMode::None).empty());
    EXPECT_TRUE(pad.handle(axis(GamepadAxis::LeftY, 1.0f), PadMode::None).empty());
    auto skip = pad.handle(down(GamepadButton::B), PadMode::None);
    ASSERT_EQ(1u, skip.size());
    expectKey(skip[0], KeyCode::Escape, true);
}

// Hot-plug and several pads

TEST_F(PadInputTest, the_first_pad_drives_the_player_and_the_others_wait) {
    pad.handle(plug(3, true), PadMode::World);
    pad.handle(plug(9, true), PadMode::World);
    EXPECT_EQ(3u, *pad.activePad());
    EXPECT_EQ((std::vector<uint32_t> {3, 9}), pad.pads());
    EXPECT_TRUE(world(down(GamepadButton::A, 9)).empty()) << "the second pad does nothing";
    EXPECT_TRUE(world(axis(GamepadAxis::LeftY, -1.0f, 9)).empty());
    EXPECT_EQ(1u, world(down(GamepadButton::A, 3)).size());
    world(up(GamepadButton::A, 3));
}

TEST_F(PadInputTest, unplugging_the_first_pad_lets_go_and_hands_over_to_the_next) {
    pad.handle(plug(3, true), PadMode::World);
    pad.handle(plug(9, true), PadMode::World);
    world(down(GamepadButton::A, 3));
    world(axis(GamepadAxis::LeftY, -1.0f, 3));
    auto unplugged = pad.handle(plug(3, false), PadMode::World);
    ASSERT_EQ(2u, unplugged.size()) << "the button and the stick direction held are released";
    for (const auto &out : unplugged) {
        EXPECT_FALSE(out.pressed);
    }
    EXPECT_EQ(9u, *pad.activePad());
    EXPECT_EQ(1u, world(down(GamepadButton::A, 9)).size());
    world(up(GamepadButton::A, 9));

    pad.handle(plug(9, false), PadMode::World);
    EXPECT_FALSE(pad.activePad());
    EXPECT_TRUE(pad.pads().empty());
    EXPECT_TRUE(world(down(GamepadButton::A, 9)).size() == 1u) << "a pad that never announced itself is taken when none is active";
}

TEST_F(PadInputTest, unplugging_an_idle_or_unknown_pad_changes_nothing) {
    pad.handle(plug(3, true), PadMode::World);
    EXPECT_TRUE(pad.handle(plug(42, false), PadMode::World).empty());
    EXPECT_TRUE(pad.handle(plug(3, true), PadMode::World).empty()) << "plugged in twice";
    EXPECT_EQ(1u, pad.pads().size());
}

// SDL

TEST(GamepadEvents, sdl_gamepad_events_become_engine_events) {
    SDL_Event event {};
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.which = 5;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    event.gbutton.down = true;
    auto mapped = graphics::gamepadEventFromSDL(event);
    ASSERT_TRUE(mapped);
    EXPECT_EQ(input::EventType::GamepadButtonDown, mapped->type);
    EXPECT_EQ(5u, mapped->padButton.pad);
    EXPECT_EQ(GamepadButton::A, mapped->padButton.button);

    event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    event.gbutton.down = false;
    mapped = graphics::gamepadEventFromSDL(event);
    ASSERT_TRUE(mapped);
    EXPECT_EQ(input::EventType::GamepadButtonUp, mapped->type);
    EXPECT_EQ(GamepadButton::DPadLeft, mapped->padButton.button);

    event.gbutton.button = SDL_GAMEPAD_BUTTON_MISC1;
    EXPECT_FALSE(graphics::gamepadEventFromSDL(event)) << "a button the engine has no use for";

    SDL_Event motion {};
    motion.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    motion.gaxis.which = 5;
    motion.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTY;
    motion.gaxis.value = -32768;
    mapped = graphics::gamepadEventFromSDL(motion);
    ASSERT_TRUE(mapped);
    EXPECT_EQ(GamepadAxis::LeftY, mapped->padAxis.axis);
    EXPECT_FLOAT_EQ(-1.0f, mapped->padAxis.value);
    motion.gaxis.axis = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    motion.gaxis.value = 16384;
    mapped = graphics::gamepadEventFromSDL(motion);
    ASSERT_TRUE(mapped);
    EXPECT_EQ(GamepadAxis::RightTrigger, mapped->padAxis.axis);
    EXPECT_NEAR(0.5f, mapped->padAxis.value, 0.001f);

    SDL_Event plugged {};
    plugged.type = SDL_EVENT_GAMEPAD_ADDED;
    plugged.gdevice.which = 11;
    mapped = graphics::gamepadEventFromSDL(plugged);
    ASSERT_TRUE(mapped);
    EXPECT_TRUE(mapped->padDevice.connected);
    EXPECT_EQ(11u, mapped->padDevice.pad);
    plugged.type = SDL_EVENT_GAMEPAD_REMOVED;
    mapped = graphics::gamepadEventFromSDL(plugged);
    ASSERT_TRUE(mapped);
    EXPECT_FALSE(mapped->padDevice.connected);

    SDL_Event key {};
    key.type = SDL_EVENT_KEY_DOWN;
    EXPECT_FALSE(graphics::gamepadEventFromSDL(key)) << "keyboard and mouse events are not this layer's";
}

TEST(GamepadEvents, a_synthetic_pad_and_a_synthetic_key_reach_the_same_action) {
    // The pad's A and the keyboard's R are the one action: the keymap translates both.
    Keymap keymap;
    PadBindings bindings;
    PadInput pad {bindings};
    auto outs = pad.handle(down(GamepadButton::A), PadMode::World);
    ASSERT_EQ(1u, outs.size());
    auto fromPad = keymap.translateAction(outs[0].action);
    auto fromKey = keymap.translate(KeyCode::R);
    EXPECT_EQ(fromKey.forward, fromPad.forward);
    ASSERT_EQ(fromKey.dispatch.has_value(), fromPad.dispatch.has_value());
    if (fromKey.dispatch) {
        EXPECT_EQ(*fromKey.dispatch, *fromPad.dispatch);
    }
}
