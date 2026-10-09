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

#include <gtest/gtest.h>

#include "reone/resource/format/tlkreader.h"
#include "reone/resource/talktable.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/stringbuilder.h"
#include "reone/system/textencoding.h"
#include "reone/resource/format/tlkwriter.h"
#include "reone/system/stream/memoryoutput.h"

using namespace reone;
using namespace reone::resource;

TEST(TlkReader, should_read_tlk) {
    // given

    auto input = StringBuilder()
                     // header
                     .append("TLK V3.0", 8)
                     .append("\x00\x00\x00\x00", 4) // language id
                     .append("\x02\x00\x00\x00", 4) // number of strings
                     .append("\x64\x00\x00\x00", 4) // offset to std::string entries
                     // std::string data 0
                     .append("\x07\x00\x00\x00", 4)                                                  // flags
                     .append("\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 16) // sound res ref
                     .append("\x00\x00\x00\x00", 4)                                                  // volume variance
                     .append("\x00\x00\x00\x00", 4)                                                  // pitch variance
                     .append("\x00\x00\x00\x00", 4)                                                  // offset to string
                     .append("\x04\x00\x00\x00", 4)                                                  // std::string size
                     .append("\x00\x00\x00\x00", 4)                                                  // sound length
                     // std::string data 1
                     .append("\x07\x00\x00\x00", 4)                                      // flags
                     .append("jane\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 16) // sound res ref
                     .append("\x00\x00\x00\x00", 4)                                      // volume variance
                     .append("\x00\x00\x00\x00", 4)                                      // pitch variance
                     .append("\x04\x00\x00\x00", 4)                                      // offset to string
                     .append("\x04\x00\x00\x00", 4)                                      // std::string size
                     .append("\x00\x00\x00\x00", 4)                                      // sound length
                     // std::string entries
                     .append("John")
                     .append("Jane")
                     .string();

    auto stream = MemoryInputStream(input);
    auto reader = TlkReader(stream);

    // when

    reader.load();

    // then

    auto table = reader.table();
    EXPECT_EQ(2, table->getStringCount());
    EXPECT_EQ("John", table->getString(0).text);
    EXPECT_EQ("", table->getString(0).soundResRef);
    EXPECT_EQ("Jane", table->getString(1).text);
    EXPECT_EQ("jane", table->getString(1).soundResRef);
}

namespace {

// A TLK file of the given language whose strings are the given bytes, stored as they are.
std::string makeTlk(uint32_t languageId, const std::vector<std::string> &texts) {
    auto u32 = [](uint32_t v) {
        std::string s(4, '\0');
        for (int i = 0; i < 4; ++i) {
            s[i] = static_cast<char>((v >> (8 * i)) & 0xff);
        }
        return s;
    };
    std::string out = std::string("TLK V3.0", 8) + u32(languageId) + u32(static_cast<uint32_t>(texts.size())) +
                      u32(static_cast<uint32_t>(20 + 40 * texts.size()));
    uint32_t offset = 0;
    for (const auto &text : texts) {
        out += u32(7) + std::string(16, '\0') + u32(0) + u32(0) + u32(offset) + u32(static_cast<uint32_t>(text.size())) + u32(0);
        offset += static_cast<uint32_t>(text.size());
    }
    for (const auto &text : texts) {
        out += text;
    }
    return out;
}

std::shared_ptr<TalkTable> readTlk(std::string bytes, uint32_t *languageId = nullptr) {
    auto stream = MemoryInputStream(bytes);
    auto reader = TlkReader(stream);
    reader.load();
    if (languageId) {
        *languageId = reader.languageId();
    }
    return reader.table();
}

} // namespace

TEST(TlkReader, keeps_english_text_and_the_language_id) {
    uint32_t id = 99;
    auto table = readTlk(makeTlk(kTlkLanguageEnglish, {"Hello", "It\x92s \x93quoted\x94"}), &id);
    EXPECT_EQ(0u, id);
    EXPECT_EQ(0u, table->languageId());
    EXPECT_EQ("Hello", table->getString(0).text);
    EXPECT_EQ("It\xE2\x80\x99s \xE2\x80\x9Cquoted\xE2\x80\x9D", table->getString(1).text) << "curly quotes of Windows-1252 become UTF-8";
}

TEST(TlkReader, reads_french_german_italian_and_spanish_as_windows_1252) {
    struct Case {
        uint32_t language;
        std::string bytes;
        std::string utf8;
    };
    std::vector<Case> cases {
        {kTlkLanguageFrench, "L'\xE9p\xE9" "e du Chevalier \xE0 Ta\xEFn\xE9, \xC7""a ira \x9C""uvre", "L'épée du Chevalier à Taïné, Ça ira œuvre"},
        {kTlkLanguageGerman, "Gr\xF6\xDF""e \xC4rger f\xFCr \xD6l und Stra\xDF""e", "Größe Ärger für Öl und Straße"},
        {kTlkLanguageItalian, "Perch\xE9 la citt\xE0 \xE8 pi\xF9 lontana", "Perché la città è più lontana"},
        {kTlkLanguageSpanish, "\xBFQu\xE9 pasa, se\xF1or? \xA1""Vamos! Pe\xF1""a", "¿Qué pasa, señor? ¡Vamos! Peña"},
    };
    for (const auto &c : cases) {
        uint32_t id = 0;
        auto table = readTlk(makeTlk(c.language, {c.bytes}), &id);
        EXPECT_EQ(c.language, id);
        EXPECT_EQ(c.utf8, table->getString(0).text) << tlkLanguageName(c.language);
    }
}

TEST(TlkReader, reads_polish_as_windows_1250) {
    auto table = readTlk(makeTlk(kTlkLanguagePolish, {"Za\xBF\xF3\xB3\xE6 g\xEA\x9Cl\xB9 ja\x9F\xF1", "\xA3\xF3" "d\x9F, \x8C""ci\xB9gnij \xAF""yci"}));
    EXPECT_EQ(5u, table->languageId());
    EXPECT_EQ("Zażółć gęślą jaźń", table->getString(0).text);
    EXPECT_EQ("Łódź, Ściągnij Życi", table->getString(1).text);
}

TEST(TlkReader, an_unknown_language_id_is_read_as_windows_1252) {
    auto table = readTlk(makeTlk(77, {"caf\xE9"}));
    EXPECT_EQ("caf\xC3\xA9", table->getString(0).text);
    EXPECT_EQ(77u, table->languageId());
}

TEST(TlkReader, multibyte_languages_pass_their_bytes_through) {
    for (uint32_t language : {kTlkLanguageKorean, kTlkLanguageChineseTraditional, kTlkLanguageChineseSimplified, kTlkLanguageJapanese}) {
        auto table = readTlk(makeTlk(language, {"\xB0\xA1\xB3\xAA"}));
        EXPECT_EQ("\xB0\xA1\xB3\xAA", table->getString(0).text) << tlkLanguageName(language);
    }
}

TEST(TlkReader, a_string_without_text_stays_empty_and_an_empty_table_loads) {
    auto table = readTlk(makeTlk(kTlkLanguageFrench, {"", "\xE9"}));
    EXPECT_EQ("", table->getString(0).text);
    EXPECT_EQ("\xC3\xA9", table->getString(1).text);
    EXPECT_EQ(0, readTlk(makeTlk(kTlkLanguageGerman, {}))->getStringCount());
}

TEST(TlkReader, text_written_and_read_again_keeps_its_language_and_bytes) {
    // Writing converts the UTF-8 strings back to the code page of the table's language.
    for (uint32_t language : {kTlkLanguageEnglish, kTlkLanguageFrench, kTlkLanguageGerman, kTlkLanguageItalian, kTlkLanguageSpanish, kTlkLanguagePolish}) {
        std::string original;
        for (int b = 0x20; b < 256; ++b) {
            // Skip the bytes Windows-1252/1250 leave undefined: they have no character to round trip as text.
            original.push_back(static_cast<char>(b));
        }
        auto file = makeTlk(language, {original, "plain"});
        auto table = readTlk(file);
        ASSERT_EQ(language, table->languageId());

        ByteBuffer written;
        MemoryOutputStream out(written);
        TlkWriter(*table).save(out);
        EXPECT_EQ(file, std::string(written.begin(), written.end())) << tlkLanguageName(language) << ": the bytes come back as they were";
    }
}

TEST(TlkWriter, characters_the_language_cannot_hold_are_written_as_question_marks) {
    auto table = TalkTable::Builder().string("łódź").language(kTlkLanguageFrench).build();
    ByteBuffer written;
    MemoryOutputStream out(written);
    TlkWriter(*table).save(out);
    std::string file(written.begin(), written.end());
    EXPECT_EQ("?\xC3\xB3" "d?", readTlk(file)->getString(0).text) << "no l-stroke or z-acute in French";
    EXPECT_EQ(static_cast<char>(kTlkLanguageFrench), file[8]) << "the language id is the table's";
}
