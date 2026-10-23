#pragma once

// iText's PdfEncodings.convertToBytes for the encodings jPDF Tweak writes text in. Java strings are
// UTF-16, so a code point above U+FFFF counts as two characters, neither of which either table maps.

#include <string>
#include <string_view>

namespace pdftweak::pdf {

// The "Cp1252" (WinAnsiEncoding) bytes of UTF-8 text: characters below 128 and from 161 to 255
// stay as they are, the others go through the WinAnsi table, and characters it lacks are dropped,
// as is U+0000.
std::string winAnsiBytes(std::string_view utf8);

// The PDFDocEncoding bytes of UTF-8 text by the same rules, with iText's PDFDocEncoding table.
std::string pdfDocBytes(std::string_view utf8);

// The conversion without an encoding: the low byte of each UTF-16 character.
std::string lowBytes(std::string_view utf8);

} // namespace pdftweak::pdf
