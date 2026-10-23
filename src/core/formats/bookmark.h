#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pdftweak::formats {

// Extra bookmark keys, such as Color or URI, in the order they were given.
using BookmarkOptions = std::vector<std::pair<std::string, std::string>>;

// PdfBookmark: one line of the bookmark CSV format,
// depth;[O][B][I];title;page[ position][;options]
struct Bookmark {
    int depth = 1;
    std::string title;
    bool open = false;
    bool bold = false;
    bool italic = false;
    int page = 0;             // 0 for a bookmark without a GoTo action
    std::string pagePosition; // such as "Fit" or "XYZ 0 800 0"
    BookmarkOptions more;     // empty for none

    std::string toString() const;
};

// Builds a bookmark as the PdfBookmark constructor does, which refuses options that do not survive
// a round trip through the options string with Error(Document, "More options incorrect").
Bookmark makeBookmark(int depth, std::string title, bool open, int page, std::string pagePosition,
                      bool bold, bool italic, const std::string& moreOptions);

// PdfBookmark.parseBookmark. Throws Error(Document, "Cannot parse bookmark") for a line that does
// not match the format.
Bookmark parseBookmark(std::string_view line);

// The options string: KEY="VALUE" pairs separated by spaces, with quotes doubled inside values.
std::string makeOptionString(const BookmarkOptions& options);
BookmarkOptions parseOptionString(std::string_view text);

// The escaping of titles and positions in a line, which writes characters below 32
// and \ ; " ' as \XX.
std::string escapeBookmarkText(std::string_view text);
std::string unescapeBookmarkText(std::string_view text);

} // namespace pdftweak::formats
