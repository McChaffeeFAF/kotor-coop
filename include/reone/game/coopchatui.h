/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <string>

#include "reone/input/event.h"

namespace reone {

namespace game {

class ChatLog;

/*
 * The co-op chat line the player types into (the in-game chat overlay). It
 * needs no engine: the game feeds it raw input events and acts on the result.
 *
 * Closed, it only looks for the open key. Open, it takes every key press so
 * nothing reaches the game's bindings while the player types; key releases
 * pass through, so a movement key held when the line opened is still let go.
 * Mouse events always pass through.
 */
class CoopChatInput {
public:
    enum class Result {
        Ignored,  // not for the chat line: the game handles the event
        Consumed, // taken by the chat line
        Opened,   // the open key opened the line
        Send,     // Enter: takeLine() has the text (may be empty)
        Cancel    // Escape: the line closed without sending
    };

    static constexpr size_t kMaxLength = 200; // net::kMaxChatBytes

    Result handle(const input::Event &event, input::KeyCode openKey);

    bool isOpen() const { return _open; }
    void open();
    void close();
    const std::string &text() const { return _text; }
    /** The text, trimmed of blanks at both ends; the line is then empty. */
    std::string takeLine();

private:
    bool _open {false};
    std::string _text;

    void type(const input::KeyEvent &key);
};

/** What the game does with a result: opens or closes the log so its lines stay opaque while typing. */
void applyChatInputResult(CoopChatInput::Result result, ChatLog &log);

} // namespace game

} // namespace reone
