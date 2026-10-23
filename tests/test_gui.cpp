// The gui group: the main window, created hidden and driven through window messages, as the gui_
// tests of the Java project drove the Swing components.

#include "runner.h"

#include "core/text.h"
#include "gui/main_window.h"
#include "gui/tabs.h"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>

#include <string>
#include <vector>

using namespace pdftweak;
using namespace pdftweak::test;

namespace {

// The common controls and COM, as pdftweak.exe starts them before it opens the window.
const struct GuiSetup {
    GuiSetup() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES |
                                                            ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES |
                                                            ICC_PROGRESS_CLASS};
        InitCommonControlsEx(&controls);
    }
} guiSetup;

HWND findChild(HWND parent, const wchar_t* className) {
    struct Search {
        const wchar_t* className;
        HWND found;
    } search{className, nullptr};
    EnumChildWindows(
        parent,
        [](HWND child, LPARAM param) -> BOOL {
            auto* s = reinterpret_cast<Search*>(param);
            wchar_t name[64];
            GetClassNameW(child, name, 64);
            if (_wcsicmp(name, s->className) == 0) {
                s->found = child;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.found;
}

std::string tabText(HWND tabControl, int index) {
    wchar_t text[64] = {};
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = text;
    item.cchTextMax = 64;
    TabCtrl_GetItem(tabControl, index, &item);
    return toUtf8(text);
}

} // namespace

// OperationTests.gui_mainFormBuildsEveryTab: the window has the eleven tabs, in Java's order.
TEST(gui_mainFormBuildsEveryTab) {
    gui::MainWindow window(false);
    const HWND tabControl = findChild(window.handle(), WC_TABCONTROLW);
    CHECK(tabControl != nullptr);
    const std::vector<std::string> names{"Input",         "Page Size",    "Watermark",   "Shuffle/N-up",
                                         "Page Numbers",  "Bookmarks",    "Attachments", "Interaction",
                                         "Document Info", "Encrypt/Sign", "Output"};
    CHECK_EQUAL(TabCtrl_GetItemCount(tabControl), static_cast<int>(names.size()));
    for (size_t i = 0; i < names.size(); ++i)
        CHECK_EQUAL(tabText(tabControl, static_cast<int>(i)), names[i]);
    CHECK_EQUAL(window.tabCount(), 11);
}
