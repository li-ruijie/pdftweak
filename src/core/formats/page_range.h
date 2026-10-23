#pragma once

#include <vector>

namespace pdftweak::formats {

// PdfPageRange: pages from one input file, in either direction, optionally only odd or only even
// ones, after a number of empty pages chosen by the parity of the pages before the range.
struct PageRange {
    int inputIndex = 0; // which input file, in the order the caller keeps them
    int from = 1;       // 1-based, after counting negative values from the end
    int to = 1;
    bool includeOdd = true;
    bool includeEven = true;
    std::vector<int> emptyBefore{0};

    // The page numbers of the range, with -1 for each empty page.
    std::vector<int> pages(int pagesBefore) const;
};

// Builds a range as the PdfPageRange constructor does, where a negative from or to counts from
// the end, so that -1 is the last page of a file with pageCount pages.
PageRange makePageRange(int inputIndex, int pageCount, int from, int to, bool includeOdd, bool includeEven,
                        std::vector<int> emptyBefore);

} // namespace pdftweak::formats
