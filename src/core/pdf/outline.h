#pragma once

// The outline as jPDF Tweak reads and writes it: iText's SimpleBookmark conversions combined with
// PdfBookmark.parseBookmarks and makeBookmarks.

#include "core/formats/bookmark.h"

#include <qpdf/QPDF.hh>

#include <vector>

namespace pdftweak::pdf {

struct InputRange;

// PdfInputFile.getBookmarks: the outline of the document as bookmark lines, depth first, the top
// level at initialDepth. Empty when the document has no outline.
std::vector<formats::Bookmark> readBookmarks(QPDF& pdf, int initialDepth);

// PdfBookmark.buildBookmarks: for each range, an open bookmark at depth 1 named after its file on the
// first page of the range that is not empty, followed by the file's own bookmarks one level deeper.
// Each of these moves to where the range puts its page, or by the pages before the range when the
// range leaves that page out.
std::vector<formats::Bookmark> buildBookmarks(const std::vector<InputRange>& ranges);

} // namespace pdftweak::pdf
