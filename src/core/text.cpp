#include "core/text.h"

#include <windows.h>

namespace pdftweak {

std::string toUtf8(std::wstring_view text) {
    if (text.empty())
        return {};
    const int size = static_cast<int>(text.size());
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), size, nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), size, utf8.data(), length, nullptr, nullptr);
    return utf8;
}

std::wstring fromUtf8(std::string_view utf8) {
    if (utf8.empty())
        return {};
    const int size = static_cast<int>(utf8.size());
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, nullptr, 0);
    std::wstring text(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, text.data(), length);
    return text;
}

std::u32string codePoints(std::string_view utf8) {
    std::u32string result;
    size_t i = 0;
    while (i < utf8.size()) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        const int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (extra < 0 || i + static_cast<size_t>(extra) >= utf8.size()) {
            result.push_back(U'�');
            ++i;
            continue;
        }
        char32_t cp = extra == 0 ? c : extra == 1 ? (c & 0x1F) : extra == 2 ? (c & 0x0F) : (c & 0x07);
        bool valid = true;
        for (int k = 1; k <= extra; ++k) {
            const unsigned char d = static_cast<unsigned char>(utf8[i + k]);
            if ((d >> 6) != 2) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (d & 0x3F);
        }
        if (!valid) {
            result.push_back(U'�');
            ++i;
            continue;
        }
        result.push_back(cp);
        i += static_cast<size_t>(extra) + 1;
    }
    return result;
}

std::string fromCodePoints(std::u32string_view points) {
    std::string utf8;
    for (const char32_t cp : points) {
        if (cp < 0x80) {
            utf8.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            utf8.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            utf8.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            utf8.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            utf8.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            utf8.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            utf8.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return utf8;
}

std::string toLatin1(std::string_view utf8) {
    std::string latin1;
    for (const char32_t cp : codePoints(utf8))
        latin1.push_back(cp <= 0xFF ? static_cast<char>(cp) : '?');
    return latin1;
}

std::string latin1ToUtf8(std::string_view latin1) {
    std::u32string points;
    for (const char c : latin1)
        points.push_back(static_cast<unsigned char>(c));
    return fromCodePoints(points);
}

} // namespace pdftweak
