#pragma once

#include <string_view>

namespace pdftweak {

// Write UTF-8 text to standard output or standard error. A console receives the text as UTF-16
// through WriteConsoleW, so that it displays correctly whatever the console code page. A pipe or
// file receives the UTF-8 bytes unchanged.
void writeOut(std::string_view utf8);
void writeErr(std::string_view utf8);

} // namespace pdftweak
