#include "core/formats/page_dimension.h"

namespace pdftweak::formats {
namespace {

struct Size {
    const char* name;
    float width;
    float height;
};

// iText 5 PageSize values, in the order of PageDimension.getCommonSizes.
constexpr Size kSizes[] = {
    {"A4", 595, 842},
    {"Letter", 612, 792},
    {"Note", 540, 720},
    {"Legal", 612, 1008},
    {"Tabloid", 792, 1224},
    {"Executive", 522, 756},
    {"Postcard", 283, 416},
    {"A0", 2384, 3370},
    {"A1", 1684, 2384},
    {"A2", 1191, 1684},
    {"A3", 842, 1191},
    {"A5", 420, 595},
    {"A6", 297, 420},
    {"A7", 210, 297},
    {"A8", 148, 210},
    {"A9", 105, 148},
    {"A10", 73, 105},
    {"B0", 2834, 4008},
    {"B1", 2004, 2834},
    {"B2", 1417, 2004},
    {"B3", 1000, 1417},
    {"B4", 708, 1000},
    {"B5", 498, 708},
    {"B6", 354, 498},
    {"B7", 249, 354},
    {"B8", 175, 249},
    {"B9", 124, 175},
    {"B10", 87, 124},
    {"archE", 2592, 3456},
    {"archD", 1728, 2592},
    {"archC", 1296, 1728},
    {"archB", 864, 1296},
    {"archA", 648, 864},
    {"American Foolscap", 612, 936},
    {"European Foolscap", 648, 936},
    {"Halfletter", 396, 612},
    {"11x17", 792, 1224},
    {"Crown Quarto", 535, 697},
    {"Large Crown Quarto", 569, 731},
    {"Crown Octavo", 348, 527},
    {"Large Crown Octavo", 365, 561},
    {"Demy Octavo", 391, 612},
    {"Demy Quarto", 620, 782},
    {"Royal Quarto", 671, 884},
    {"Royal Octavo", 442, 663},
    {"Small paperback", 314, 504},
    {"Penguin small paperback", 314, 513},
    {"Penguin large paperback", 365, 561},
};

std::vector<PageDimension> build() {
    std::vector<PageDimension> sizes;
    for (const Size& s : kSizes) {
        sizes.push_back({std::string(s.name) + " Portrait", s.width, s.height});
        sizes.push_back({std::string(s.name) + " Landscape", s.height, s.width});
    }
    sizes.push_back({"Ledger", 1224, 792});
    sizes.push_back({"ISO 7810 ID-1", 242.65f, 153});
    sizes.push_back({"ISO 7810 ID-2", 297, 210});
    sizes.push_back({"ISO 7810 ID-3", 354, 249});
    return sizes;
}

} // namespace

const std::vector<PageDimension>& commonSizes() {
    static const std::vector<PageDimension> sizes = build();
    return sizes;
}

} // namespace pdftweak::formats
