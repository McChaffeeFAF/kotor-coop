/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "reone/game/coopchat.h"
#include "reone/game/coopchatui.h"
#include "reone/game/keymap.h"

using namespace reone;
using namespace reone::game;
using input::Event;
using input::KeyCode;
using input::KeyEvent;
using Result = CoopChatInput::Result;

namespace {

Event down(KeyCode code, uint16_t mod = 0, bool repeat = false) {
    return Event::newKeyDown(KeyEvent(true, code, mod, repeat));
}

Event up(KeyCode code) {
    return Event::newKeyUp(KeyEvent(false, code, 0, false));
}

ChatLine lineAt(double time, const std::string &text) {
    ChatLine line;
    line.time = time;
    line.name = "Alice";
    line.text = text;
    return line;
}

} // namespace

TEST(CoopChatInput, closed_it_only_answers_the_open_key) {
    CoopChatInput input;
    EXPECT_EQ(Result::Ignored, input.handle(down(KeyCode::W), KeyCode::Return));
    EXPECT_EQ(Result::Ignored, input.handle(up(KeyCode::Return), KeyCode::Return));
    EXPECT_EQ(Result::Ignored, input.handle(down(KeyCode::Return, 0, true), KeyCode::Return)) << "a held Enter does not reopen it";
    EXPECT_FALSE(input.isOpen());
    EXPECT_EQ(Result::Opened, input.handle(down(KeyCode::Return), KeyCode::Return));
    EXPECT_TRUE(input.isOpen());
    EXPECT_TRUE(input.text().empty());
}

TEST(CoopChatInput, the_open_key_follows_the_binding) {
    CoopChatInput input;
    Keymap keymap;
    EXPECT_EQ(KeyCode::Return, keymap.keyFor(KeyAction::CoopChat));
    keymap.bind(KeyAction::CoopChat, KeyCode::T);
    EXPECT_EQ(Result::Ignored, input.handle(down(KeyCode::Return), keymap.keyFor(KeyAction::CoopChat)));
    EXPECT_EQ(Result::Opened, input.handle(down(KeyCode::T), keymap.keyFor(KeyAction::CoopChat)));
    // Once open, Enter sends whatever opened it.
    EXPECT_EQ(Result::Send, input.handle(down(KeyCode::Return), keymap.keyFor(KeyAction::CoopChat)));
}

TEST(CoopChatInput, open_it_takes_every_key_press_so_game_keys_do_not_fire) {
    CoopChatInput input;
    input.handle(down(KeyCode::Return), KeyCode::Return);
    // Movement, the inventory, pause, the ping key and number hotkeys all type instead.
    for (auto code : {KeyCode::W, KeyCode::I, KeyCode::Space, KeyCode::N, KeyCode::Key1, KeyCode::Tab, KeyCode::F4, KeyCode::CapsLock}) {
        EXPECT_EQ(Result::Consumed, input.handle(down(code), KeyCode::Return)) << static_cast<int>(code);
    }
    EXPECT_EQ("wi n1", input.text()) << "printable keys type; Tab, F4 and CapsLock are swallowed";
    // A release passes through: a key held when the line opened is still let go.
    EXPECT_EQ(Result::Ignored, input.handle(up(KeyCode::W), KeyCode::Return));
    // The mouse is not the chat line's.
    auto motion = Event::newMouseMotion(input::MouseMotionEvent(10, 10, 0.0f, 0.0f));
    EXPECT_EQ(Result::Ignored, input.handle(motion, KeyCode::Return));
}

TEST(CoopChatInput, shift_backspace_and_the_length_limit) {
    CoopChatInput input;
    input.handle(down(KeyCode::Return), KeyCode::Return);
    input.handle(down(KeyCode::H, input::KeyModifiers::leftShift), KeyCode::Return);
    input.handle(down(KeyCode::I), KeyCode::Return);
    input.handle(down(KeyCode::Key1, input::KeyModifiers::rightShift), KeyCode::Return);
    input.handle(down(KeyCode::X), KeyCode::Return);
    input.handle(down(KeyCode::Backspace), KeyCode::Return);
    input.handle(down(KeyCode::C, input::KeyModifiers::leftControl), KeyCode::Return);
    EXPECT_EQ("Hi!", input.text()) << "Ctrl+C types nothing";
    for (int i = 0; i < 300; ++i) {
        input.handle(down(KeyCode::A), KeyCode::Return);
    }
    EXPECT_EQ(CoopChatInput::kMaxLength, input.text().size());
}

TEST(CoopChatInput, enter_sends_the_trimmed_line_and_escape_cancels) {
    CoopChatInput input;
    input.handle(down(KeyCode::Return), KeyCode::Return);
    for (auto code : {KeyCode::Space, KeyCode::G, KeyCode::O, KeyCode::Space}) {
        input.handle(down(code), KeyCode::Return);
    }
    EXPECT_EQ(Result::Send, input.handle(down(KeyCode::Return), KeyCode::Return));
    EXPECT_FALSE(input.isOpen());
    EXPECT_EQ("go", input.takeLine());
    EXPECT_TRUE(input.text().empty());
    // The Enter release after sending is a plain release for the game.
    EXPECT_EQ(Result::Ignored, input.handle(up(KeyCode::Return), KeyCode::Return));

    input.handle(down(KeyCode::Return), KeyCode::Return);
    input.handle(down(KeyCode::Q), KeyCode::Return);
    EXPECT_EQ(Result::Cancel, input.handle(down(KeyCode::Escape), KeyCode::Return)) << "Escape closes the line, not the game menu";
    EXPECT_FALSE(input.isOpen());
    EXPECT_TRUE(input.text().empty());
    EXPECT_EQ(Result::Ignored, input.handle(down(KeyCode::Escape), KeyCode::Return)) << "closed, Escape is the game's again";
}

TEST(CoopChatOverlay, lines_stay_while_typing_and_fade_after_the_line_closes) {
    ChatLog log;
    CoopChatInput input;
    log.add(lineAt(0.0, "hello"));
    ASSERT_EQ(1u, log.visible(1.0).size());
    EXPECT_FLOAT_EQ(1.0f, log.visible(1.0)[0].alpha);
    EXPECT_TRUE(log.visible(ChatLog::kShowSeconds + ChatLog::kFadeSeconds + 1.0).empty()) << "faded out a few seconds later";

    // Opening the chat line brings the faded line back, opaque.
    applyChatInputResult(input.handle(down(KeyCode::Return), KeyCode::Return), log);
    EXPECT_TRUE(log.open());
    auto shown = log.visible(30.0);
    ASSERT_EQ(1u, shown.size());
    EXPECT_FLOAT_EQ(1.0f, shown[0].alpha);

    // Typing does not change that; cancelling returns to fading by age.
    applyChatInputResult(input.handle(down(KeyCode::A), KeyCode::Return), log);
    EXPECT_TRUE(log.open());
    applyChatInputResult(input.handle(down(KeyCode::Escape), KeyCode::Return), log);
    EXPECT_FALSE(log.open());
    EXPECT_TRUE(log.visible(30.0).empty());

    // Halfway through the fade a line is half transparent.
    log.add(lineAt(30.0, "again"));
    auto half = log.visible(30.0 + ChatLog::kShowSeconds + ChatLog::kFadeSeconds * 0.5);
    ASSERT_EQ(1u, half.size());
    EXPECT_NEAR(0.5f, half[0].alpha, 1e-4f);

    // Sending closes the log too.
    applyChatInputResult(input.handle(down(KeyCode::Return), KeyCode::Return), log);
    applyChatInputResult(input.handle(down(KeyCode::Return), KeyCode::Return), log);
    EXPECT_FALSE(log.open());
}

TEST(CoopChatOverlay, the_ping_key_is_rebindable_and_does_not_take_enter) {
    Keymap keymap;
    EXPECT_EQ(KeyCode::N, keymap.keyFor(KeyAction::CoopPing));
    EXPECT_TRUE(keyActionInfo(KeyAction::CoopPing).remappable);
    auto translation = keymap.translate(KeyCode::N);
    ASSERT_TRUE(translation.dispatch);
    EXPECT_EQ(KeyAction::CoopPing, *translation.dispatch);
    keymap.bind(KeyAction::CoopPing, KeyCode::B);
    EXPECT_EQ(KeyCode::B, keymap.keyFor(KeyAction::CoopPing));
    EXPECT_EQ(KeyCode::Return, keymap.keyFor(KeyAction::CoopChat));
}
