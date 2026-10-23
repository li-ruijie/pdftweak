#pragma once

#include <string>
#include <vector>

namespace pdftweak::formats {

// PdfTweak.TRANSITION_NAMES: the page transitions, with index 0 for none. The index is the value of
// the transition in the -transition option and the Interaction tab.
const std::vector<std::string>& transitionNames();

// PdfTweak.permissionTexts and permissionBits: the permissions an encrypted file can grant, with the
// bit of the /P entry for each.
const std::vector<std::string>& permissionNames();
const std::vector<int>& permissionBits();

// PdfTweak.getKnownInfoNames: the standard keys of the document information dictionary.
const std::vector<std::string>& infoNames();

// The page modes and page layouts of the Interaction tab, in the order of their index. The -pagemode
// and -pagelayout options take the same names with underscores for spaces.
const std::vector<std::string>& pageModeNames();
const std::vector<std::string>& pageLayoutNames();

} // namespace pdftweak::formats
