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

#include "reone/system/textencoding.h"

#include <atomic>

namespace reone {

namespace {

// Windows-1252, 0x80-0x9F. The five bytes it leaves undefined map to the C1 controls of the same value.
constexpr uint16_t kCp1252High[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

// Windows-1250, 0x80-0xFF.
constexpr uint16_t kCp1250High[128] = {
    0x20AC, 0x0081, 0x201A, 0x0083, 0x201E, 0x2026, 0x2020, 0x2021, 0x0088, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x0098, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
    0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
    0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
    0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7, 0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
    0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7, 0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
    0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7, 0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
    0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7, 0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9};

struct Fallback {
    uint16_t codePoint;
    char base;
};

constexpr Fallback kFallbacks[] = {
    {0x00A0, ' '}, {0x00AB, '<'}, {0x00BB, '>'}, {0x00C0, 'A'}, {0x00C1, 'A'}, {0x00C2, 'A'},
    {0x00C3, 'A'}, {0x00C4, 'A'}, {0x00C5, 'A'}, {0x00C6, 'A'}, {0x00C7, 'C'}, {0x00C8, 'E'},
    {0x00C9, 'E'}, {0x00CA, 'E'}, {0x00CB, 'E'}, {0x00CC, 'I'}, {0x00CD, 'I'}, {0x00CE, 'I'},
    {0x00CF, 'I'}, {0x00D0, 'D'}, {0x00D1, 'N'}, {0x00D2, 'O'}, {0x00D3, 'O'}, {0x00D4, 'O'},
    {0x00D5, 'O'}, {0x00D6, 'O'}, {0x00D8, 'O'}, {0x00D9, 'U'}, {0x00DA, 'U'}, {0x00DB, 'U'},
    {0x00DC, 'U'}, {0x00DD, 'Y'}, {0x00DE, 'P'}, {0x00DF, 's'}, {0x00E0, 'a'}, {0x00E1, 'a'},
    {0x00E2, 'a'}, {0x00E3, 'a'}, {0x00E4, 'a'}, {0x00E5, 'a'}, {0x00E6, 'a'}, {0x00E7, 'c'},
    {0x00E8, 'e'}, {0x00E9, 'e'}, {0x00EA, 'e'}, {0x00EB, 'e'}, {0x00EC, 'i'}, {0x00ED, 'i'},
    {0x00EE, 'i'}, {0x00EF, 'i'}, {0x00F0, 'd'}, {0x00F1, 'n'}, {0x00F2, 'o'}, {0x00F3, 'o'},
    {0x00F4, 'o'}, {0x00F5, 'o'}, {0x00F6, 'o'}, {0x00F8, 'o'}, {0x00F9, 'u'}, {0x00FA, 'u'},
    {0x00FB, 'u'}, {0x00FC, 'u'}, {0x00FD, 'y'}, {0x00FE, 'p'}, {0x00FF, 'y'}, {0x0100, 'A'},
    {0x0101, 'a'}, {0x0102, 'A'}, {0x0103, 'a'}, {0x0104, 'A'}, {0x0105, 'a'}, {0x0106, 'C'},
    {0x0107, 'c'}, {0x0108, 'C'}, {0x0109, 'c'}, {0x010A, 'C'}, {0x010B, 'c'}, {0x010C, 'C'},
    {0x010D, 'c'}, {0x010E, 'D'}, {0x010F, 'd'}, {0x0110, 'D'}, {0x0111, 'd'}, {0x0112, 'E'},
    {0x0113, 'e'}, {0x0114, 'E'}, {0x0115, 'e'}, {0x0116, 'E'}, {0x0117, 'e'}, {0x0118, 'E'},
    {0x0119, 'e'}, {0x011A, 'E'}, {0x011B, 'e'}, {0x011C, 'G'}, {0x011D, 'g'}, {0x011E, 'G'},
    {0x011F, 'g'}, {0x0120, 'G'}, {0x0121, 'g'}, {0x0122, 'G'}, {0x0123, 'g'}, {0x0124, 'H'},
    {0x0125, 'h'}, {0x0126, 'H'}, {0x0127, 'h'}, {0x0128, 'I'}, {0x0129, 'i'}, {0x012A, 'I'},
    {0x012B, 'i'}, {0x012C, 'I'}, {0x012D, 'i'}, {0x012E, 'I'}, {0x012F, 'i'}, {0x0130, 'I'},
    {0x0131, 'i'}, {0x0134, 'J'}, {0x0135, 'j'}, {0x0136, 'K'}, {0x0137, 'k'}, {0x0139, 'L'},
    {0x013A, 'l'}, {0x013B, 'L'}, {0x013C, 'l'}, {0x013D, 'L'}, {0x013E, 'l'}, {0x0141, 'L'},
    {0x0142, 'l'}, {0x0143, 'N'}, {0x0144, 'n'}, {0x0145, 'N'}, {0x0146, 'n'}, {0x0147, 'N'},
    {0x0148, 'n'}, {0x014C, 'O'}, {0x014D, 'o'}, {0x014E, 'O'}, {0x014F, 'o'}, {0x0150, 'O'},
    {0x0151, 'o'}, {0x0152, 'O'}, {0x0153, 'o'}, {0x0154, 'R'}, {0x0155, 'r'}, {0x0156, 'R'},
    {0x0157, 'r'}, {0x0158, 'R'}, {0x0159, 'r'}, {0x015A, 'S'}, {0x015B, 's'}, {0x015C, 'S'},
    {0x015D, 's'}, {0x015E, 'S'}, {0x015F, 's'}, {0x0160, 'S'}, {0x0161, 's'}, {0x0162, 'T'},
    {0x0163, 't'}, {0x0164, 'T'}, {0x0165, 't'}, {0x0168, 'U'}, {0x0169, 'u'}, {0x016A, 'U'},
    {0x016B, 'u'}, {0x016C, 'U'}, {0x016D, 'u'}, {0x016E, 'U'}, {0x016F, 'u'}, {0x0170, 'U'},
    {0x0171, 'u'}, {0x0172, 'U'}, {0x0173, 'u'}, {0x0174, 'W'}, {0x0175, 'w'}, {0x0176, 'Y'},
    {0x0177, 'y'}, {0x0178, 'Y'}, {0x0179, 'Z'}, {0x017A, 'z'}, {0x017B, 'Z'}, {0x017C, 'z'},
    {0x017D, 'Z'}, {0x017E, 'z'}, {0x01A0, 'O'}, {0x01A1, 'o'}, {0x01AF, 'U'}, {0x01B0, 'u'},
    {0x01CD, 'A'}, {0x01CE, 'a'}, {0x01CF, 'I'}, {0x01D0, 'i'}, {0x01D1, 'O'}, {0x01D2, 'o'},
    {0x01D3, 'U'}, {0x01D4, 'u'}, {0x01D5, 'U'}, {0x01D6, 'u'}, {0x01D7, 'U'}, {0x01D8, 'u'},
    {0x01D9, 'U'}, {0x01DA, 'u'}, {0x01DB, 'U'}, {0x01DC, 'u'}, {0x01DE, 'A'}, {0x01DF, 'a'},
    {0x01E0, 'A'}, {0x01E1, 'a'}, {0x01E6, 'G'}, {0x01E7, 'g'}, {0x01E8, 'K'}, {0x01E9, 'k'},
    {0x01EA, 'O'}, {0x01EB, 'o'}, {0x01EC, 'O'}, {0x01ED, 'o'}, {0x01F0, 'j'}, {0x01F4, 'G'},
    {0x01F5, 'g'}, {0x01F8, 'N'}, {0x01F9, 'n'}, {0x01FA, 'A'}, {0x01FB, 'a'}, {0x0200, 'A'},
    {0x0201, 'a'}, {0x0202, 'A'}, {0x0203, 'a'}, {0x0204, 'E'}, {0x0205, 'e'}, {0x0206, 'E'},
    {0x0207, 'e'}, {0x0208, 'I'}, {0x0209, 'i'}, {0x020A, 'I'}, {0x020B, 'i'}, {0x020C, 'O'},
    {0x020D, 'o'}, {0x020E, 'O'}, {0x020F, 'o'}, {0x0210, 'R'}, {0x0211, 'r'}, {0x0212, 'R'},
    {0x0213, 'r'}, {0x0214, 'U'}, {0x0215, 'u'}, {0x0216, 'U'}, {0x0217, 'u'}, {0x0218, 'S'},
    {0x0219, 's'}, {0x021A, 'T'}, {0x021B, 't'}, {0x021E, 'H'}, {0x021F, 'h'}, {0x0226, 'A'},
    {0x0227, 'a'}, {0x0228, 'E'}, {0x0229, 'e'}, {0x022A, 'O'}, {0x022B, 'o'}, {0x022C, 'O'},
    {0x022D, 'o'}, {0x022E, 'O'}, {0x022F, 'o'}, {0x0230, 'O'}, {0x0231, 'o'}, {0x0232, 'Y'},
    {0x0233, 'y'}, {0x2013, '-'}, {0x2014, '-'}, {0x2018, '\''}, {0x2019, '\''}, {0x201A, ','},
    {0x201C, '"'}, {0x201D, '"'}, {0x201E, '"'}, {0x2022, '*'}, {0x2026, '.'}, {0x2039, '<'},
    {0x203A, '>'}, {0x20AC, 'E'}, {0x2122, 'T'},
};

uint32_t byteToUnicode(CodePage codePage, uint8_t byte) {
    if (byte < 0x80) {
        return byte;
    }
    switch (codePage) {
    case CodePage::Windows1252:
        return byte < 0xA0 ? kCp1252High[byte - 0x80] : byte;
    case CodePage::Windows1250:
        return kCp1250High[byte - 0x80];
    default:
        return byte;
    }
}

std::atomic<CodePage> g_activeCodePage {CodePage::Windows1252};

} // namespace

CodePage codePageForTlkLanguage(uint32_t languageId) {
    switch (languageId) {
    case kTlkLanguagePolish:
        return CodePage::Windows1250;
    case kTlkLanguageKorean:
    case kTlkLanguageChineseTraditional:
    case kTlkLanguageChineseSimplified:
    case kTlkLanguageJapanese:
        return CodePage::Multibyte;
    default:
        return CodePage::Windows1252;
    }
}

const char *codePageName(CodePage codePage) {
    switch (codePage) {
    case CodePage::Windows1252:
        return "Windows-1252";
    case CodePage::Windows1250:
        return "Windows-1250";
    default:
        return "multibyte";
    }
}

const char *tlkLanguageName(uint32_t languageId) {
    switch (languageId) {
    case kTlkLanguageEnglish:
        return "English";
    case kTlkLanguageFrench:
        return "French";
    case kTlkLanguageGerman:
        return "German";
    case kTlkLanguageItalian:
        return "Italian";
    case kTlkLanguageSpanish:
        return "Spanish";
    case kTlkLanguagePolish:
        return "Polish";
    case kTlkLanguageKorean:
        return "Korean";
    case kTlkLanguageChineseTraditional:
        return "Chinese (Traditional)";
    case kTlkLanguageChineseSimplified:
        return "Chinese (Simplified)";
    case kTlkLanguageJapanese:
        return "Japanese";
    default:
        return "unknown";
    }
}

static void appendUtf8(std::string &out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string codePageToUtf8(CodePage codePage, std::string_view bytes) {
    if (codePage == CodePage::Multibyte) {
        return std::string(bytes);
    }
    std::string out;
    out.reserve(bytes.size() + bytes.size() / 4);
    for (char c : bytes) {
        appendUtf8(out, byteToUnicode(codePage, static_cast<uint8_t>(c)));
    }
    return out;
}

std::string utf8ToCodePage(CodePage codePage, std::string_view text) {
    if (codePage == CodePage::Multibyte) {
        return std::string(text);
    }
    std::string out;
    out.reserve(text.size());
    for (uint32_t cp : decodeUtf8Lenient(text)) {
        uint8_t byte;
        out.push_back(unicodeToCodePage(codePage, cp, byte) ? static_cast<char>(byte) : '?');
    }
    return out;
}

bool unicodeToCodePage(CodePage codePage, uint32_t codePoint, uint8_t &byte) {
    if (codePoint < 0x80) {
        byte = static_cast<uint8_t>(codePoint);
        return true;
    }
    switch (codePage) {
    case CodePage::Windows1252:
        if (codePoint >= 0xA0 && codePoint <= 0xFF) {
            byte = static_cast<uint8_t>(codePoint);
            return true;
        }
        for (int i = 0; i < 32; ++i) {
            if (kCp1252High[i] == codePoint) {
                byte = static_cast<uint8_t>(0x80 + i);
                return true;
            }
        }
        return false;
    case CodePage::Windows1250:
        for (int i = 0; i < 128; ++i) {
            if (kCp1250High[i] == codePoint) {
                byte = static_cast<uint8_t>(0x80 + i);
                return true;
            }
        }
        return false;
    default:
        // Text that was never converted: a Latin-1 reading of its bytes.
        if (codePoint <= 0xFF) {
            byte = static_cast<uint8_t>(codePoint);
            return true;
        }
        return false;
    }
}

size_t utf8SequenceLength(unsigned char lead) {
    if ((lead & 0xE0) == 0xC0) {
        return 2;
    }
    if ((lead & 0xF0) == 0xE0) {
        return 3;
    }
    if ((lead & 0xF8) == 0xF0) {
        return 4;
    }
    return 1;
}

std::vector<uint32_t> decodeUtf8Lenient(std::string_view text) {
    std::vector<uint32_t> out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        auto lead = static_cast<unsigned char>(text[i]);
        size_t len = utf8SequenceLength(lead);
        bool valid = len > 1 && i + len <= text.size();
        uint32_t cp = 0;
        if (valid) {
            cp = len == 2 ? (lead & 0x1F) : len == 3 ? (lead & 0x0F) : (lead & 0x07);
            for (size_t k = 1; k < len; ++k) {
                auto cont = static_cast<unsigned char>(text[i + k]);
                if ((cont & 0xC0) != 0x80) {
                    valid = false;
                    break;
                }
                cp = (cp << 6) | (cont & 0x3F);
            }
            // Overlong forms and surrogates are not text.
            static const uint32_t kMinimum[5] = {0, 0, 0x80, 0x800, 0x10000};
            if (valid && (cp < kMinimum[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))) {
                valid = false;
            }
        }
        if (valid) {
            out.push_back(cp);
            i += len;
        } else {
            out.push_back(lead);
            ++i;
        }
    }
    return out;
}

uint32_t fallbackCodePoint(uint32_t codePoint) {
    for (const auto &entry : kFallbacks) {
        if (entry.codePoint == codePoint) {
            return static_cast<uint32_t>(static_cast<unsigned char>(entry.base));
        }
    }
    return '?';
}

uint8_t glyphIndexFor(CodePage codePage, uint32_t codePoint, size_t numGlyphs) {
    uint8_t index = 0;
    auto fits = [&](uint32_t cp) {
        return unicodeToCodePage(codePage, cp, index) && index < numGlyphs;
    };
    if (fits(codePoint) || fits(fallbackCodePoint(codePoint)) || fits('?')) {
        return index;
    }
    return 0;
}

CodePage activeCodePage() {
    return g_activeCodePage.load();
}

void setActiveCodePage(CodePage codePage) {
    g_activeCodePage.store(codePage);
}

} // namespace reone
