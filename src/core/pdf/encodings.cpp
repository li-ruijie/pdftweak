#include "core/pdf/encodings.h"

#include "core/text.h"

#include <array>

namespace pdftweak::pdf {
namespace {

// iText's winansiByteToChar and pdfEncodingByteToChar for the bytes 128 to 160, with 65533 where
// the byte has no character.
constexpr std::array<char16_t, 33> kWinAnsi = {8364, 65533, 8218,  402,  8222,  8230, 8224,  8225,  710,
                                               8240, 352,   8249,  338,  65533, 381,  65533, 65533, 8216,
                                               8217, 8220,  8221,  8226, 8211,  8212, 732,   8482,  353,
                                               8250, 339,   65533, 382,  376,   160};
constexpr std::array<char16_t, 33> kPdfDoc = {
    0x2022, 0x2020, 0x2021, 0x2026, 0x2014, 0x2013, 0x0192, 0x2044, 0x2039, 0x203a, 0x2212,
    0x2030, 0x201e, 0x201c, 0x201d, 0x2018, 0x2019, 0x201a, 0x2122, 0xfb01, 0xfb02, 0x0141,
    0x0152, 0x0160, 0x0178, 0x017d, 0x0131, 0x0142, 0x0153, 0x0161, 0x017e, 65533,  0x20ac};

// The UTF-16 characters of UTF-8 text.
std::u16string javaChars(std::string_view utf8) {
    std::u16string chars;
    for (const char32_t c : codePoints(utf8)) {
        if (c > 0xFFFF) {
            chars += static_cast<char16_t>(0xD800 + ((c - 0x10000) >> 10));
            chars += static_cast<char16_t>(0xDC00 + ((c - 0x10000) & 0x3FF));
        } else {
            chars += static_cast<char16_t>(c);
        }
    }
    return chars;
}

std::string convert(std::string_view utf8, const std::array<char16_t, 33>& table) {
    std::string bytes;
    for (const char16_t c : javaChars(utf8)) {
        int code = 0;
        if (c < 128 || (c > 160 && c <= 255)) {
            code = c;
        } else {
            for (size_t k = 0; k < table.size(); ++k)
                if (table[k] == c && c != 65533)
                    code = static_cast<int>(k) + 128;
        }
        if (code != 0)
            bytes += static_cast<char>(code);
    }
    return bytes;
}

} // namespace

std::string winAnsiBytes(std::string_view utf8) { return convert(utf8, kWinAnsi); }

std::string pdfDocBytes(std::string_view utf8) { return convert(utf8, kPdfDoc); }

std::string lowBytes(std::string_view utf8) {
    std::string bytes;
    for (const char16_t c : javaChars(utf8))
        bytes += static_cast<char>(c & 0xFF);
    return bytes;
}

} // namespace pdftweak::pdf
