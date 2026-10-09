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

#include "reone/system/textencoding.h"

using namespace reone;

TEST(TextEncoding, every_byte_of_a_single_byte_code_page_survives_a_round_trip) {
    for (CodePage page : {CodePage::Windows1252, CodePage::Windows1250}) {
        for (int b = 0; b < 256; ++b) {
            std::string original(1, static_cast<char>(b));
            auto utf8 = codePageToUtf8(page, original);
            auto codePoints = decodeUtf8Lenient(utf8);
            ASSERT_EQ(1u, codePoints.size()) << codePageName(page) << " byte " << b;
            uint8_t back;
            ASSERT_TRUE(unicodeToCodePage(page, codePoints[0], back)) << codePageName(page) << " byte " << b;
            EXPECT_EQ(b, back) << codePageName(page);
            EXPECT_EQ(original, utf8ToCodePage(page, utf8)) << codePageName(page) << " byte " << b;
        }
    }
}

TEST(TextEncoding, windows_1252_text_becomes_utf8) {
    // "Café — l'été à Paris, Ça va?" with an em dash, an e acute, a grave a and a cedilla.
    std::string bytes = "Caf\xE9 \x97 l'\xE9t\xE9 \xE0 Paris, \xC7""a va? \x80""5 \x93quoted\x94";
    EXPECT_EQ("Caf\xC3\xA9 \xE2\x80\x94 l'\xC3\xA9t\xC3\xA9 \xC3\xA0 Paris, \xC3\x87""a va? \xE2\x82\xAC""5 \xE2\x80\x9Cquoted\xE2\x80\x9D",
              codePageToUtf8(CodePage::Windows1252, bytes));
    // German and Spanish letters: ä ö ü ß ñ ¿ ¡
    EXPECT_EQ("\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F\xC3\xB1\xC2\xBF\xC2\xA1",
              codePageToUtf8(CodePage::Windows1252, "\xE4\xF6\xFC\xDF\xF1\xBF\xA1"));
    EXPECT_EQ("plain ascii", codePageToUtf8(CodePage::Windows1252, "plain ascii"));
}

TEST(TextEncoding, windows_1250_text_becomes_utf8) {
    // Polish: ą ć ę ł ń ó ś ź ż and the capitals Ą Ł Ś Ż
    std::string bytes = "\xB9\xE6\xEA\xB3\xF1\xF3\x9C\x9F\xBF \xA5\xA3\x8C\xAF";
    EXPECT_EQ("ąćęłńóśźż ĄŁŚŻ", codePageToUtf8(CodePage::Windows1250, bytes));
    // The same bytes in Windows-1252 are other letters: the code page matters.
    EXPECT_NE(codePageToUtf8(CodePage::Windows1250, bytes), codePageToUtf8(CodePage::Windows1252, bytes));
}

TEST(TextEncoding, the_tlk_language_ids_pick_the_code_page) {
    EXPECT_EQ(CodePage::Windows1252, codePageForTlkLanguage(kTlkLanguageEnglish));
    EXPECT_EQ(CodePage::Windows1252, codePageForTlkLanguage(kTlkLanguageFrench));
    EXPECT_EQ(CodePage::Windows1252, codePageForTlkLanguage(kTlkLanguageGerman));
    EXPECT_EQ(CodePage::Windows1252, codePageForTlkLanguage(kTlkLanguageItalian));
    EXPECT_EQ(CodePage::Windows1252, codePageForTlkLanguage(kTlkLanguageSpanish));
    EXPECT_EQ(CodePage::Windows1250, codePageForTlkLanguage(kTlkLanguagePolish));
    EXPECT_EQ(CodePage::Multibyte, codePageForTlkLanguage(kTlkLanguageKorean));
    EXPECT_EQ(CodePage::Multibyte, codePageForTlkLanguage(kTlkLanguageChineseTraditional));
    EXPECT_EQ(CodePage::Multibyte, codePageForTlkLanguage(kTlkLanguageChineseSimplified));
    EXPECT_EQ(CodePage::Multibyte, codePageForTlkLanguage(kTlkLanguageJapanese));
    EXPECT_EQ(CodePage::Windows1252, codePageForTlkLanguage(77)) << "an unknown id is read as Windows-1252";
    EXPECT_STREQ("Polish", tlkLanguageName(5));
    EXPECT_STREQ("unknown", tlkLanguageName(77));
    EXPECT_STREQ("Windows-1250", codePageName(CodePage::Windows1250));
    // Multibyte text is not touched.
    EXPECT_EQ("\xB9\xE6", codePageToUtf8(CodePage::Multibyte, "\xB9\xE6"));
}

TEST(TextEncoding, utf8_goes_back_to_a_code_page_and_unmappable_characters_become_question_marks) {
    EXPECT_EQ("Caf\xE9", utf8ToCodePage(CodePage::Windows1252, "Caf\xC3\xA9"));
    EXPECT_EQ("\xB3\xF3" "d\x9F", utf8ToCodePage(CodePage::Windows1250, "łódź"));
    EXPECT_EQ("?d?", utf8ToCodePage(CodePage::Windows1252, "łd\xE2\x82\xA7")) << "no ł in Windows-1252, no Pesetas sign either";
    EXPECT_EQ("a?b", utf8ToCodePage(CodePage::Windows1252, "a\xF0\x9F\x98\x80" "b")) << "one question mark per character";
    uint8_t byte = 0;
    EXPECT_FALSE(unicodeToCodePage(CodePage::Windows1252, 0x0142, byte));
    EXPECT_TRUE(unicodeToCodePage(CodePage::Windows1250, 0x0142, byte));
    EXPECT_EQ(0xB3, byte);
}

TEST(TextEncoding, lenient_utf8_decoding_never_fails) {
    EXPECT_EQ((std::vector<uint32_t> {'a', 0xE9, 0x20AC, 0x1F600}), decodeUtf8Lenient("a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"));
    // A stray byte, a truncated sequence, an overlong form and a surrogate are read as single Latin-1 bytes.
    EXPECT_EQ((std::vector<uint32_t> {'a', 0xE9, 'b'}), decodeUtf8Lenient("a\xE9" "b"));
    EXPECT_EQ((std::vector<uint32_t> {0xE2, 0x82}), decodeUtf8Lenient("\xE2\x82"));
    EXPECT_EQ((std::vector<uint32_t> {0xC0, 0x80}), decodeUtf8Lenient("\xC0\x80"));
    EXPECT_EQ((std::vector<uint32_t> {0xED, 0xA0, 0x80}), decodeUtf8Lenient("\xED\xA0\x80"));
    EXPECT_TRUE(decodeUtf8Lenient("").empty());
    EXPECT_EQ(2u, utf8SequenceLength(0xC3));
    EXPECT_EQ(3u, utf8SequenceLength(0xE2));
    EXPECT_EQ(4u, utf8SequenceLength(0xF0));
    EXPECT_EQ(1u, utf8SequenceLength('a'));
    EXPECT_EQ(1u, utf8SequenceLength(0x80));
}

TEST(TextEncoding, a_glyph_that_a_font_lacks_falls_back_to_the_base_letter) {
    EXPECT_EQ(static_cast<uint32_t>('e'), fallbackCodePoint(0xE9));
    EXPECT_EQ(static_cast<uint32_t>('E'), fallbackCodePoint(0xC9));
    EXPECT_EQ(static_cast<uint32_t>('l'), fallbackCodePoint(0x0142));
    EXPECT_EQ(static_cast<uint32_t>('S'), fallbackCodePoint(0x015A));
    EXPECT_EQ(static_cast<uint32_t>('z'), fallbackCodePoint(0x017C));
    EXPECT_EQ(static_cast<uint32_t>('c'), fallbackCodePoint(0x00E7));
    EXPECT_EQ(static_cast<uint32_t>('n'), fallbackCodePoint(0x00F1));
    EXPECT_EQ(static_cast<uint32_t>('-'), fallbackCodePoint(0x2014));
    EXPECT_EQ(static_cast<uint32_t>('"'), fallbackCodePoint(0x201C));
    EXPECT_EQ(static_cast<uint32_t>('\''), fallbackCodePoint(0x2019));
    EXPECT_EQ(static_cast<uint32_t>('?'), fallbackCodePoint(0x4E2D)) << "no stand-in for a Chinese character";
    EXPECT_EQ(static_cast<uint32_t>('?'), fallbackCodePoint(0x1F600));
}

TEST(TextEncoding, glyph_numbers_follow_the_code_page_and_what_the_font_has) {
    // A full font of 256 glyphs in Windows-1252: é is glyph 0xE9, an em dash 0x97.
    EXPECT_EQ(0xE9, glyphIndexFor(CodePage::Windows1252, 0xE9, 256));
    EXPECT_EQ(0x97, glyphIndexFor(CodePage::Windows1252, 0x2014, 256));
    // In a Polish font ł is glyph 0xB3; in a Windows-1252 font it is drawn as an l.
    EXPECT_EQ(0xB3, glyphIndexFor(CodePage::Windows1250, 0x0142, 256));
    EXPECT_EQ('l', glyphIndexFor(CodePage::Windows1252, 0x0142, 256));
    // A font with only ASCII glyphs: é becomes e, a character without a stand-in becomes '?'.
    EXPECT_EQ('e', glyphIndexFor(CodePage::Windows1252, 0xE9, 128));
    EXPECT_EQ('?', glyphIndexFor(CodePage::Windows1252, 0x4E2D, 128));
    EXPECT_EQ('A', glyphIndexFor(CodePage::Windows1252, 'A', 128));
    // A font too small even for '?' draws glyph 0 rather than reading outside itself.
    EXPECT_EQ(0, glyphIndexFor(CodePage::Windows1252, 0x4E2D, 10));
    EXPECT_EQ('A', glyphIndexFor(CodePage::Windows1252, 'A', 66));
    // Text that was never converted (a multibyte language) keeps its byte values.
    EXPECT_EQ(0xE9, glyphIndexFor(CodePage::Multibyte, 0xE9, 256));
}

TEST(TextEncoding, the_active_code_page_can_be_set) {
    auto before = activeCodePage();
    setActiveCodePage(CodePage::Windows1250);
    EXPECT_EQ(CodePage::Windows1250, activeCodePage());
    setActiveCodePage(before);
    EXPECT_EQ(before, activeCodePage());
}
