#include "core/formats/tables.h"

namespace pdftweak::formats {

const std::vector<std::string>& transitionNames() {
    static const std::vector<std::string> names = {
        "None",
        "Out Vertical Split",
        "Out Horizontal Split",
        "In Vertical Split",
        "In Horizontal Split",
        "Vertical Blinds",
        "Horizontal Blinds",
        "Inward Box",
        "Outward Box",
        "Left-Right Wipe",
        "Right-Left Wipe",
        "Bottom-Top Wipe",
        "Top-Bottom Wipe",
        "Dissolve",
        "Left-Right Glitter",
        "Top-Bottom Glitter",
        "Diagonal Glitter",
    };
    return names;
}

const std::vector<std::string>& permissionNames() {
    static const std::vector<std::string> names = {"Printing", "ModifyContents", "Copy",
                                                   "ModifyAnnotations", "FillIn", "ScreenReaders",
                                                   "Assembly", "HQPrinting"};
    return names;
}

const std::vector<int>& permissionBits() {
    static const std::vector<int> bits = {4, 8, 16, 32, 256, 512, 1024, 2048};
    return bits;
}

const std::vector<std::string>& infoNames() {
    static const std::vector<std::string> names = {"Title",   "Subject",  "Keywords",     "Author",
                                                   "Creator", "Producer", "CreationDate", "ModDate"};
    return names;
}

const std::vector<std::string>& pageModeNames() {
    static const std::vector<std::string> names = {"None",        "Outline",          "Thumbnails",
                                                   "Full Screen", "Optional Content", "Attachments"};
    return names;
}

const std::vector<std::string>& pageLayoutNames() {
    static const std::vector<std::string> names = {"Single Page", "One Column", "Two Columns Left",
                                                   "Two Columns Right", "Two Pages Left",
                                                   "Two Pages Right"};
    return names;
}

} // namespace pdftweak::formats
