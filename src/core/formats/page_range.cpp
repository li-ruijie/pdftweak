#include "core/formats/page_range.h"

namespace pdftweak::formats {

PageRange makePageRange(int inputIndex, int pageCount, int from, int to, bool includeOdd, bool includeEven,
                        std::vector<int> emptyBefore) {
    PageRange range;
    range.inputIndex = inputIndex;
    range.from = from < 0 ? from + pageCount + 1 : from;
    range.to = to < 0 ? to + pageCount + 1 : to;
    range.includeOdd = includeOdd;
    range.includeEven = includeEven;
    range.emptyBefore = std::move(emptyBefore);
    return range;
}

std::vector<int> PageRange::pages(int pagesBefore) const {
    const int empty = emptyBefore[static_cast<size_t>(pagesBefore) % emptyBefore.size()];
    std::vector<int> result(static_cast<size_t>(empty), -1);
    const int step = from > to ? -1 : 1;
    for (int i = from;; i += step) {
        if ((i % 2 == 0 && includeEven) || (i % 2 == 1 && includeOdd))
            result.push_back(i);
        if (i == to)
            break;
    }
    return result;
}

} // namespace pdftweak::formats
