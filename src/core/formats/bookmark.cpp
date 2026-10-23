#include "core/formats/bookmark.h"

#include "core/error.h"
#include "core/java_text.h"
#include "core/text.h"

#include <regex>

namespace pdftweak::formats {
namespace {

int hexValue(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

} // namespace

std::string escapeBookmarkText(std::string_view text) {
    static const char hex[] = "0123456789ABCDEF";
    std::string result;
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 32 || c == '\\' || c == ';' || c == '"' || c == '\'') {
            result += '\\';
            result += hex[u >> 4];
            result += hex[u & 15];
        } else {
            result += c;
        }
    }
    return result;
}

std::string unescapeBookmarkText(std::string_view text) {
    std::u32string points;
    const std::u32string source = codePoints(text);
    for (size_t i = 0; i < source.size(); ++i) {
        if (source[i] == U'\\') {
            if (i + 2 >= source.size())
                throw Error(Error::Kind::Document, "Incomplete escape in bookmark: " + std::string(text));
            const int high = source[i + 1] < 128 ? hexValue(static_cast<char>(source[i + 1])) : -1;
            const int low = source[i + 2] < 128 ? hexValue(static_cast<char>(source[i + 2])) : -1;
            if (high < 0 || low < 0)
                throw Error(Error::Kind::Document, "Invalid escape in bookmark: " + std::string(text));
            points.push_back(static_cast<char32_t>(high * 16 + low));
            i += 2;
        } else {
            points.push_back(source[i]);
        }
    }
    return fromCodePoints(points);
}

std::string makeOptionString(const BookmarkOptions& options) {
    std::string text;
    for (const auto& [key, value] : options) {
        if (!text.empty())
            text += ' ';
        std::string doubled;
        for (const char c : value) {
            doubled += c;
            if (c == '"')
                doubled += '"';
        }
        text += key + "=\"" + doubled + "\"";
    }
    return text;
}

BookmarkOptions parseOptionString(std::string_view input) {
    BookmarkOptions result;
    std::string rest = javaTrim(input);
    while (!rest.empty()) {
        const size_t equals = rest.find("=\"");
        if (equals == std::string::npos)
            throw Error(Error::Kind::Document, "Invalid option (no equals sign): " + rest);
        const std::string key = rest.substr(0, equals);
        std::string value;
        rest = rest.substr(equals + 2);
        while (true) {
            const size_t quote = rest.find('"');
            if (quote == std::string::npos)
                throw Error(Error::Kind::Document, "Unterminated option value: " + key);
            if (quote + 1 < rest.size() && rest[quote + 1] == '"') {
                value += rest.substr(0, quote + 1);
                rest = rest.substr(quote + 2);
            } else {
                value += rest.substr(0, quote);
                rest = rest.substr(quote + 1);
                break;
            }
        }
        // A LinkedHashMap keeps the first position of a key and the last value.
        bool replaced = false;
        for (auto& entry : result) {
            if (entry.first == key) {
                entry.second = value;
                replaced = true;
            }
        }
        if (!replaced)
            result.emplace_back(key, value);
        rest = javaTrim(rest);
    }
    return result;
}

Bookmark makeBookmark(int depth, std::string title, bool open, int page, std::string pagePosition,
                      bool bold, bool italic, const std::string& moreOptions) {
    Bookmark b;
    b.depth = depth;
    b.title = std::move(title);
    b.open = open;
    b.page = page;
    b.pagePosition = std::move(pagePosition);
    b.bold = bold;
    b.italic = italic;
    if (!moreOptions.empty()) {
        b.more = parseOptionString(moreOptions);
        if (makeOptionString(b.more) != moreOptions)
            throw Error(Error::Kind::Document, "More options incorrect");
    }
    return b;
}

Bookmark parseBookmark(std::string_view text) {
    static const std::regex pattern(R"((-?[0-9]+);(O?B?I?);([^;]*);(-?[0-9]+)( [^;]+)?(;[^;]*)?)");
    const std::string line(text);
    std::smatch m;
    if (!std::regex_match(line, m, pattern))
        throw Error(Error::Kind::Document, "Cannot parse bookmark");
    const std::string flags = m[2].str();
    return makeBookmark(javaParseInt(m[1].str()), unescapeBookmarkText(m[3].str()),
                        flags.find('O') != std::string::npos, javaParseInt(m[4].str()),
                        m[5].matched ? unescapeBookmarkText(m[5].str().substr(1)) : std::string(),
                        flags.find('B') != std::string::npos, flags.find('I') != std::string::npos,
                        m[6].matched ? unescapeBookmarkText(m[6].str().substr(1)) : std::string());
}

std::string Bookmark::toString() const {
    std::string line = std::to_string(depth) + ";" + (open ? "O" : "") + (bold ? "B" : "") +
                       (italic ? "I" : "") + ";" + escapeBookmarkText(title) + ";" + std::to_string(page) +
                       (pagePosition.empty() ? "" : " ") + escapeBookmarkText(pagePosition);
    if (!more.empty())
        line += ";" + escapeBookmarkText(makeOptionString(more));
    return line;
}

} // namespace pdftweak::formats
