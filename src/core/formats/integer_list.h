#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace pdftweak::formats {

// IntegerList: comma-separated non-negative integers, such as the empty pages before a page range.
// Throws Error(Document) with Java's NumberFormatException message.
std::vector<int> parseIntegerList(std::string_view text);
std::string formatIntegerList(const std::vector<int>& values);

} // namespace pdftweak::formats
