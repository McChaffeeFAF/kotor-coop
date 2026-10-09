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

namespace reone {

/**
 * Text encodings of localised KotOR installs. Talk tables (dialog.tlk) and other
 * game text are stored in the single-byte code page of the install's language;
 * the engine keeps text as UTF-8 and converts at the edges.
 */
enum class CodePage {
    Windows1252, ///< English, French, German, Italian, Spanish
    Windows1250, ///< Polish
    Multibyte    ///< Korean, Chinese, Japanese: not converted (bytes pass through)
};

/// Language ids of a talk table header (public format documentation).
constexpr uint32_t kTlkLanguageEnglish = 0;
constexpr uint32_t kTlkLanguageFrench = 1;
constexpr uint32_t kTlkLanguageGerman = 2;
constexpr uint32_t kTlkLanguageItalian = 3;
constexpr uint32_t kTlkLanguageSpanish = 4;
constexpr uint32_t kTlkLanguagePolish = 5;
constexpr uint32_t kTlkLanguageKorean = 128;
constexpr uint32_t kTlkLanguageChineseTraditional = 129;
constexpr uint32_t kTlkLanguageChineseSimplified = 130;
constexpr uint32_t kTlkLanguageJapanese = 131;

/// The code page of a talk table language id; an id nobody knows is read as Windows-1252.
CodePage codePageForTlkLanguage(uint32_t languageId);

const char *codePageName(CodePage codePage);
const char *tlkLanguageName(uint32_t languageId);

/// Converts text in a single-byte code page to UTF-8. A Multibyte page is returned as it is.
std::string codePageToUtf8(CodePage codePage, std::string_view bytes);

/// Converts UTF-8 text to a single-byte code page; what the page has no character for becomes '?'.
std::string utf8ToCodePage(CodePage codePage, std::string_view text);

/// The byte that stands for a code point in a single-byte code page, if there is one.
bool unicodeToCodePage(CodePage codePage, uint32_t codePoint, uint8_t &byte);

/**
 * Splits UTF-8 into code points without ever failing. A byte that does not start a valid
 * sequence becomes the code point of the same value (read as Latin-1), so text that was
 * never converted still shows up as it always did.
 */
std::vector<uint32_t> decodeUtf8Lenient(std::string_view text);

/// How many bytes the UTF-8 sequence starting at the lead byte claims (1 for a byte that is no lead byte).
size_t utf8SequenceLength(unsigned char lead);

/**
 * A code point to show in place of one a font has no glyph for: the unaccented base letter
 * of a Latin letter, a plain stand-in for typographic punctuation, otherwise '?'.
 */
uint32_t fallbackCodePoint(uint32_t codePoint);

/**
 * The glyph number a font laid out in a code page draws a code point with: the code point's
 * own byte, else that of its fallback (see fallbackCodePoint), else '?', as long as the
 * glyph exists (the font has numGlyphs of them); 0 when even '?' is missing.
 */
uint8_t glyphIndexFor(CodePage codePage, uint32_t codePoint, size_t numGlyphs);

/// The code page the loaded game's fonts index their glyphs by (set when the talk table is read).
CodePage activeCodePage();
void setActiveCodePage(CodePage codePage);

} // namespace reone
