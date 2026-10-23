#pragma once

// The outline as jPDF Tweak reads and writes it: iText's SimpleBookmark conversions combined with
// PdfBookmark.parseBookmarks and makeBookmarks.

#include "core/formats/bookmark.h"

#include <qpdf/QPDF.hh>

#include <vector>

namespace pdftweak::pdf {

// PdfInputFile.getBookmarks: the outline of the document as bookmark lines, depth first, the top
// level at initialDepth. Empty when the document has no outline.
std::vector<formats::Bookmark> readBookmarks(QPDF& pdf, int initialDepth);

} // namespace pdftweak::pdf
