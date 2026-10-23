#pragma once

#include <string>
#include <vector>

namespace pdftweak::formats {

// ViewerPreference: one optional entry of the /ViewerPreferences dictionary. The values are written
// as Java prints the iText objects, which is also how the -viewpref option spells them: "true" and
// "false" for booleans, "/Name" for names, and digits for numbers.
struct ViewerPreference {
    std::string name; // as the GUI shows it, with spaces where -viewpref takes underscores
    std::string key;  // such as "/DisplayDocTitle"
    std::vector<std::string> values;
};

// ViewerPreference.SUPPORTED_VIEWER_PREFERENCES, in the Java order.
const std::vector<ViewerPreference>& viewerPreferences();

} // namespace pdftweak::formats
