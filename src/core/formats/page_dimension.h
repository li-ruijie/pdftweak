#pragma once

#include <string>
#include <vector>

namespace pdftweak::formats {

// PageDimension: a named page size in PostScript points.
struct PageDimension {
    std::string name;
    float width;
    float height;
};

// The 100 sizes of PageDimension.getCommonSizes, in its order: 48 sizes in portrait and landscape,
// then Ledger and the three ISO 7810 card sizes, with iText's PageSize values.
const std::vector<PageDimension>& commonSizes();

} // namespace pdftweak::formats
