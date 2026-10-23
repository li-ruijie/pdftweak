#include "core/formats/integer_list.h"

#include "core/error.h"
#include "core/java_text.h"

namespace pdftweak::formats {

std::vector<int> parseIntegerList(std::string_view text) {
    std::vector<int> values;
    for (const std::string& part : javaSplit(text, ',', true)) {
        const int value = javaParseInt(part);
        if (value < 0)
            throw Error(Error::Kind::Document, "Value may not be negative: " + std::to_string(value));
        values.push_back(value);
    }
    return values;
}

std::string formatIntegerList(const std::vector<int>& values) {
    std::string text;
    for (size_t i = 0; i < values.size(); ++i)
        text += (i ? "," : "") + std::to_string(values[i]);
    return text;
}

} // namespace pdftweak::formats
