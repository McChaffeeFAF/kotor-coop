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

// Text in fonts: UTF-8 in, glyphs of the install's code page out (docs/dev/international_installs.md).

#include <gtest/gtest.h>

#include "reone/graphics/context.h"
#include "reone/graphics/font.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/options.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/statistic.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/textutil.h"
#include "reone/graphics/uniforms.h"
#include "reone/system/textencoding.h"

using namespace reone;
using namespace reone::graphics;

namespace {

// A font whose glyph n is (n + 1) hundredths wide, so what was drawn shows in a measurement.
class FontText : public testing::Test {
protected:
    GraphicsOptions options;
    Context context {options};
    Statistic statistic;
    MeshRegistry meshes {statistic};
    ShaderRegistry shaders;
    Uniforms uniforms {context};
    CodePage savedPage {activeCodePage()};

    void TearDown() override { setActiveCodePage(savedPage); }

    std::shared_ptr<Font> makeFont(int numGlyphs) {
        auto texture = std::make_shared<Texture>("font", TextureType::TwoDim, Texture::Properties {});
        texture->setPixels(1, 1, PixelFormat::RGBA8, Texture::Layer {std::make_shared<ByteBuffer>(4, 0)});
        Texture::Features features;
        features.fontHeight = 0.1f;
        features.numChars = numGlyphs;
        for (int i = 0; i < numGlyphs; ++i) {
            features.upperLeftCoords.push_back(glm::vec3(0.0f, 1.0f, 0.0f));
            features.lowerRightCoords.push_back(glm::vec3((i + 1) * 0.001f, 0.0f, 0.0f));
        }
        texture->setFeatures(std::move(features));
        auto font = std::make_shared<Font>(context, meshes, shaders, statistic, uniforms);
        font->load(texture);
        return font;
    }

    static float width(int glyph) { return (glyph + 1) * 0.01f; }
};

bool validUtf8(const std::string &s) {
    for (size_t i = 0; i < s.size();) {
        size_t n = utf8SequenceLength(static_cast<unsigned char>(s[i]));
        if (i + n > s.size()) {
            return false;
        }
        for (size_t k = 1; k < n; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) {
                return false;
            }
        }
        i += n;
    }
    return true;
}

} // namespace

TEST_F(FontText, ascii_draws_with_its_own_glyphs) {
    auto font = makeFont(256);
    EXPECT_EQ((std::vector<uint8_t> {'H', 'i', ' ', '!'}), font->glyphIndices("Hi !"));
    EXPECT_FLOAT_EQ(width('H') + width('i'), font->measure("Hi"));
    EXPECT_TRUE(font->glyphIndices("").empty());
}

TEST_F(FontText, accented_text_draws_with_the_glyphs_of_the_installs_code_page) {
    auto font = makeFont(256);
    setActiveCodePage(CodePage::Windows1252);
    EXPECT_EQ((std::vector<uint8_t> {'A', 0xE9, 0xFC, 0xF1, 0x97}), font->glyphIndices("Aéüñ—"));
    EXPECT_NEAR(width(0xE9), font->measure("é"), 1e-6f) << "one character, one glyph, not the two bytes of its UTF-8";
    setActiveCodePage(CodePage::Windows1250);
    EXPECT_EQ((std::vector<uint8_t> {0xB3, 0xF3, 'd', 0x9F}), font->glyphIndices("łódź"));
    EXPECT_NEAR(width(0xB3), font->measure("ł"), 1e-6f);
}

TEST_F(FontText, a_character_the_font_lacks_is_drawn_as_its_base_letter_or_a_question_mark) {
    auto ascii = makeFont(128);
    setActiveCodePage(CodePage::Windows1252);
    EXPECT_EQ((std::vector<uint8_t> {'c', 'a', 'f', 'e'}), ascii->glyphIndices("café"));
    EXPECT_EQ((std::vector<uint8_t> {'?', 'x'}), ascii->glyphIndices("中x")) << "no stand-in for a Chinese character";
    auto full = makeFont(256);
    setActiveCodePage(CodePage::Windows1252);
    EXPECT_EQ((std::vector<uint8_t> {'l', 'o', 'd', 'z'}), full->glyphIndices("łodź")) << "a Windows-1252 font has no Polish letters";
}

TEST_F(FontText, text_that_was_never_converted_still_shows_as_it_did) {
    auto font = makeFont(256);
    setActiveCodePage(CodePage::Windows1252);
    // A lone Latin-1 byte (what a raw dialog.tlk string held) is its own glyph.
    EXPECT_EQ((std::vector<uint8_t> {'c', 0xE9}), font->glyphIndices("c\xE9"));
    setActiveCodePage(CodePage::Multibyte);
    EXPECT_EQ((std::vector<uint8_t> {0xB0, 0xA1}), font->glyphIndices("\xB0\xA1"));
}

TEST_F(FontText, an_empty_font_draws_nothing) {
    Font font(context, meshes, shaders, statistic, uniforms);
    EXPECT_TRUE(font.glyphIndices("abc").empty());
    EXPECT_FLOAT_EQ(0.0f, font.measure("abc"));
}

TEST_F(FontText, a_font_with_few_glyphs_never_reads_outside_itself) {
    auto tiny = makeFont(70);
    setActiveCodePage(CodePage::Windows1252);
    for (uint8_t glyph : tiny->glyphIndices("AZaz09é中\xFF")) {
        EXPECT_LT(glyph, 70);
    }
    EXPECT_GE(tiny->measure("zzzz"), 0.0f);
}

TEST_F(FontText, line_breaking_never_splits_a_character) {
    auto font = makeFont(256);
    setActiveCodePage(CodePage::Windows1252);
    // é is 2.34 wide: three of them reach 7.02, over the limit of 5.
    auto lines = breakText("éééééé", *font, 5, 1.0f);
    ASSERT_EQ(3u, lines.size());
    for (const auto &line : lines) {
        EXPECT_EQ("éé", line);
        EXPECT_TRUE(validUtf8(line));
    }
    // Mixed text, at every limit, always breaks between characters and loses none.
    for (int limit = 1; limit < 40; ++limit) {
        auto pieces = breakText("Zażółć gęślą jaźń — très élégant", *font, limit, 1.0f);
        std::string joined;
        for (const auto &piece : pieces) {
            EXPECT_TRUE(validUtf8(piece)) << limit;
            joined += piece;
        }
        // Spaces at a break are dropped, nothing else.
        std::string expected;
        for (const auto &piece : breakText("Zażółć gęślą jaźń — très élégant", *font, 1000, 1.0f)) {
            expected += piece;
        }
        std::string withoutSpaces;
        std::string joinedWithoutSpaces;
        for (char c : expected) {
            if (c != ' ') withoutSpaces += c;
        }
        for (char c : joined) {
            if (c != ' ') joinedWithoutSpaces += c;
        }
        EXPECT_EQ(withoutSpaces, joinedWithoutSpaces) << limit;
    }
}

TEST_F(FontText, a_hyphen_between_accented_letters_is_a_break_point) {
    auto font = makeFont(256);
    setActiveCodePage(CodePage::Windows1252);
    auto lines = breakText("été-été-été", *font, 8, 1.0f);
    ASSERT_GE(lines.size(), 2u);
    EXPECT_EQ('-', lines[0].back()) << "breaks after the hyphen: " << lines[0];
}
