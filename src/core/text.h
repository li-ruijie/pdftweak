#pragma once

#include <string>
#include <string_view>

namespace pdftweak {

// Conversions between the UTF-16 of the Windows API, the UTF-8 the core uses inside, and the
// Latin-1 bytes Java's getBytes("ISO-8859-1") produced for passwords.
std::string toUtf8(std::wstring_view text);
std::wstring fromUtf8(std::string_view utf8);

// Each code point above U+00FF becomes one '?', as Java's encoder replaces unmappable characters.
std::string toLatin1(std::string_view utf8);

// Latin-1 bytes back to UTF-8.
std::string latin1ToUtf8(std::string_view latin1);

// The code points of UTF-8 text. Invalid bytes become U+FFFD.
std::u32string codePoints(std::string_view utf8);
std::string fromCodePoints(std::u32string_view codePoints);

} // namespace pdftweak
