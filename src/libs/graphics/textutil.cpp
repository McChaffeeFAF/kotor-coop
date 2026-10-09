/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#include "reone/graphics/textutil.h"

#include "reone/graphics/font.h"
#include "reone/system/textencoding.h"

#include <cctype>
#include <string_view>

namespace reone {

namespace graphics {

// The text is UTF-8: a character is a whole sequence, never one of its bytes.
static size_t characterLength(const std::string &text, size_t pos) {
    size_t length = utf8SequenceLength(static_cast<unsigned char>(text[pos]));
    if (pos + length > text.size()) {
        return 1;
    }
    for (size_t i = 1; i < length; ++i) {
        if ((static_cast<unsigned char>(text[pos + i]) & 0xC0) != 0x80) {
            return 1;
        }
    }
    return length;
}

static float measureCharacter(Font &font, const std::string &text, size_t pos, size_t length, float scale) {
    return font.measure(std::string_view(text.data() + pos, length), scale);
}

// Letters of other alphabets (any byte of a multi-byte sequence) count as letters.
static bool isAlphabetic(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || static_cast<unsigned char>(ch) >= 0x80;
}

std::vector<std::string> breakText(const std::string &text, Font &font, int maxWidth, float scale) {
    std::vector<std::string> result;
    if (text.empty()) {
        return result;
    }

    size_t lineStart = 0;
    while (lineStart < text.size()) {
        if (text[lineStart] == '\n') {
            result.emplace_back();
            ++lineStart;
            continue;
        }

        float width = 0.0f;
        size_t breakAt = std::string::npos;
        size_t cursor = lineStart;
        bool wrapped = false;

        while (cursor < text.size() && text[cursor] != '\n') {
            char ch = text[cursor];
            size_t length = characterLength(text, cursor);
            width += measureCharacter(font, text, cursor, length, scale);

            if (ch == ' ' && cursor > lineStart) {
                breakAt = cursor;
            } else if (ch == '-' &&
                       cursor > lineStart &&
                       cursor + 1 < text.size() &&
                       isAlphabetic(text[cursor - 1]) &&
                       isAlphabetic(text[cursor + 1])) {
                breakAt = cursor;
            }

            if (width >= static_cast<float>(maxWidth)) {
                size_t lineEnd;
                size_t nextLineStart;
                if (breakAt != std::string::npos) {
                    bool breakAfter = text[breakAt] == '-';
                    lineEnd = breakAt + static_cast<size_t>(breakAfter);
                    nextLineStart = breakAt + 1;
                } else if (cursor > lineStart) {
                    lineEnd = cursor;
                    nextLineStart = cursor;
                } else {
                    lineEnd = cursor + length;
                    nextLineStart = cursor + length;
                }

                result.emplace_back(text.substr(lineStart, lineEnd - lineStart));
                lineStart = nextLineStart;
                wrapped = true;
                break;
            }
            cursor += length;
        }

        if (wrapped) {
            continue;
        }

        result.emplace_back(text.substr(lineStart, cursor - lineStart));
        if (cursor == text.size()) {
            lineStart = cursor;
        } else {
            lineStart = cursor + 1;
        }
    }

    return result;
}

} // namespace graphics

} // namespace reone
