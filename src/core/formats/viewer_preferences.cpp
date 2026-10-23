#include "core/formats/viewer_preferences.h"

namespace pdftweak::formats {

const std::vector<ViewerPreference>& viewerPreferences() {
    static const std::vector<std::string> flag = {"true", "false"};
    static const std::vector<std::string> boxes = {"/MediaBox", "/CropBox", "/BleedBox", "/TrimBox",
                                                   "/ArtBox"};
    static const std::vector<ViewerPreference> preferences = {
        {"Hide Toolbar", "/HideToolbar", flag},
        {"Hide Menubar", "/HideMenubar", flag},
        {"Hide Window UI", "/HideWindowUI", flag},
        {"Fit Window To PDF", "/FitWindow", flag},
        {"Center Window on screen", "/CenterWindow", flag},
        {"Display document title", "/DisplayDocTitle", flag},
        {"Page Mode after leaving fullscreen",
         "/NonFullScreenPageMode",
         {"/UseNone", "/UseOutlines", "/UseThumbs", "/UseOC"}},
        {"Direction", "/Direction", {"/L2R", "/R2L"}},
        {"View area", "/ViewArea", boxes},
        {"View clip", "/ViewClip", boxes},
        {"Print area", "/PrintArea", boxes},
        {"Print clip", "/PrintClip", boxes},
        {"Print scaling", "/PrintScaling", {"/AppDefault", "/None"}},
        {"Duplex", "/Duplex", {"/Simplex", "/DuplexFlipShortEdge", "/DuplexFlipLongEdge"}},
        {"Pick Tray by PDF size", "/PickTrayByPDFSize", flag},
        {"Number of copies", "/NumCopies", {"1", "2", "3", "4", "5"}},
    };
    return preferences;
}

} // namespace pdftweak::formats
