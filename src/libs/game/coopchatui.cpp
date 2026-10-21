/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/coopchatui.h"

#include "reone/game/coopchat.h"

namespace reone {

namespace game {

using input::EventType;
using input::KeyCode;

namespace {

// US layout: the characters a shifted key types.
char shifted(char c) {
    if (c >= 'a' && c <= 'z') {
        return static_cast<char>(c - 'a' + 'A');
    }
    switch (c) {
    case '1': return '!';
    case '2': return '@';
    case '3': return '#';
    case '4': return '$';
    case '5': return '%';
    case '6': return '^';
    case '7': return '&';
    case '8': return '*';
    case '9': return '(';
    case '0': return ')';
    case '-': return '_';
    case '=': return '+';
    case '[': return '{';
    case ']': return '}';
    case ';': return ':';
    case '\'': return '"';
    case ',': return '<';
    case '.': return '>';
    case '/': return '?';
    case '\\': return '|';
    case '`': return '~';
    default: return c;
    }
}

bool printable(KeyCode code) {
    int value = static_cast<int>(code);
    return value >= 0x20 && value < 0x7f;
}

} // namespace

void CoopChatInput::open() {
    _open = true;
    _text.clear();
}

void CoopChatInput::close() {
    _open = false;
    _text.clear();
}

std::string CoopChatInput::takeLine() {
    size_t begin = _text.find_first_not_of(' ');
    size_t end = _text.find_last_not_of(' ');
    std::string line = begin == std::string::npos ? std::string() : _text.substr(begin, end - begin + 1);
    _text.clear();
    return line;
}

void CoopChatInput::type(const input::KeyEvent &key) {
    if (key.code == KeyCode::Backspace) {
        if (!_text.empty()) {
            _text.pop_back();
        }
        return;
    }
    if ((key.mod & (input::KeyModifiers::control | input::KeyModifiers::alt | input::KeyModifiers::gui)) != 0) {
        return;
    }
    if (!printable(key.code) || _text.size() >= kMaxLength) {
        return;
    }
    char c = static_cast<char>(key.code);
    bool shift = (key.mod & input::KeyModifiers::shift) != 0;
    bool caps = (key.mod & input::KeyModifiers::capsLock) != 0;
    if (c >= 'a' && c <= 'z') {
        if (shift != caps) {
            c = shifted(c);
        }
    } else if (shift) {
        c = shifted(c);
    }
    _text.push_back(c);
}

CoopChatInput::Result CoopChatInput::handle(const input::Event &event, KeyCode openKey) {
    if (event.type != EventType::KeyDown && event.type != EventType::KeyUp) {
        return Result::Ignored;
    }
    if (!_open) {
        if (event.type == EventType::KeyDown && !event.key.repeat && openKey != KeyCode::Unknown && event.key.code == openKey) {
            open();
            return Result::Opened;
        }
        return Result::Ignored;
    }
    if (event.type == EventType::KeyUp) {
        // Releasing a key cannot start anything; the game may need to let go of it.
        return Result::Ignored;
    }
    if (event.key.code == KeyCode::Return) {
        _open = false;
        return Result::Send;
    }
    if (event.key.code == KeyCode::Escape) {
        close();
        return Result::Cancel;
    }
    type(event.key);
    return Result::Consumed;
}

void applyChatInputResult(CoopChatInput::Result result, ChatLog &log) {
    switch (result) {
    case CoopChatInput::Result::Opened:
        log.setOpen(true);
        break;
    case CoopChatInput::Result::Send:
    case CoopChatInput::Result::Cancel:
        log.setOpen(false);
        break;
    default:
        break;
    }
}

} // namespace game

} // namespace reone
