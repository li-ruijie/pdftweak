#include "core/formats/page_labels.h"

#include "core/error.h"
#include "core/java_text.h"

#include <map>
#include <regex>

namespace pdftweak::formats {
namespace {

const char kStyleLetters[] = "xIiAa-";

std::string upper(std::string text) {
    for (char& c : text)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return text;
}

// RomanNumberFactory.getString.
std::string roman(int index) {
    struct Digit {
        char digit;
        int value;
        bool pre;
    };
    static const Digit digits[] = {{'m', 1000, false}, {'d', 500, false}, {'c', 100, true},
                                   {'l', 50, false},   {'x', 10, true},   {'v', 5, false},
                                   {'i', 1, true}};
    std::string text;
    long long n = index;
    if (n < 0) {
        text += '-';
        n = -n;
    }
    if (n > 3000) {
        text += '|' + roman(static_cast<int>(n / 1000)) + '|';
        n -= (n / 1000) * 1000;
    }
    for (size_t pos = 0;; ++pos) {
        const Digit& digit = digits[pos];
        while (n >= digit.value) {
            text += digit.digit;
            n -= digit.value;
        }
        if (n <= 0)
            break;
        size_t j = pos + 1;
        while (!digits[j].pre)
            ++j;
        if (n + digits[j].value >= digit.value) {
            text += digits[j].digit;
            text += digit.digit;
            n -= digit.value - digits[j].value;
        }
    }
    return text;
}

} // namespace

PageLabelFormat parsePageLabel(std::string_view text) {
    static const std::regex pattern(R"(([0-9]+)=(.*:)?([0-9]+)([aAiI-]?))");
    const std::string label(text);
    std::smatch m;
    if (!std::regex_match(label, m, pattern))
        throw Error(Error::Kind::Document, "Cannot parse page label: " + label);
    PageLabelFormat format;
    format.physicalPage = javaParseInt(m[1].str());
    if (m[2].matched && m[2].length() > 0)
        format.prefix = m[2].str().substr(0, static_cast<size_t>(m[2].length()) - 1);
    format.logicalPage = javaParseInt(m[3].str());
    if (m[4].length() > 0)
        format.style = static_cast<LabelStyle>(std::string_view(kStyleLetters).find(m[4].str()[0]));
    return format;
}

std::string formatPageLabel(const PageLabelFormat& format) {
    std::string text = std::to_string(format.physicalPage) + "=";
    if (!format.prefix.empty())
        text += format.prefix + ":";
    text += std::to_string(format.logicalPage);
    if (format.style != LabelStyle::Decimal)
        text += kStyleLetters[static_cast<int>(format.style)];
    return text;
}

std::string labelNumber(LabelStyle style, int number) {
    switch (style) {
    case LabelStyle::Decimal: return std::to_string(number);
    case LabelStyle::UpperRoman: return upper(roman(number));
    case LabelStyle::LowerRoman: return roman(number);
    case LabelStyle::UpperLetters:
    case LabelStyle::LowerLetters: {
        if (number < 1)
            throw Error(Error::Kind::Document,
                        "You can't translate a negative number into an alphabetical value.");
        const char base = style == LabelStyle::UpperLetters ? 'A' : 'a';
        const size_t repeats = static_cast<size_t>((number - 1) / 26 + 1);
        return std::string(repeats, static_cast<char>(base + (number - 1) % 26));
    }
    case LabelStyle::Empty: return std::string();
    }
    return std::string();
}

std::vector<std::string> labelStrings(const std::vector<PageLabelFormat>& formats, int pageCount) {
    std::map<int, const PageLabelFormat*> tree;
    for (const PageLabelFormat& format : formats)
        tree[format.physicalPage - 1] = &format;
    std::vector<std::string> labels;
    int pageNumber = 1;
    std::string prefix;
    LabelStyle style = LabelStyle::Decimal;
    for (int i = 0; i < pageCount; ++i) {
        if (const auto entry = tree.find(i); entry != tree.end()) {
            pageNumber = entry->second->logicalPage;
            prefix = entry->second->prefix;
            style = entry->second->style;
        }
        labels.push_back(prefix + labelNumber(style, pageNumber));
        ++pageNumber;
    }
    return labels;
}

} // namespace pdftweak::formats
