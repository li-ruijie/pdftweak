#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <string_view>
#include <vector>

#include "core/cli.h"
#include "gui/main_window.h"

// pdftweak.exe opens the window when started without arguments and runs the command-line
// interface otherwise, as jpdftweak.jar does.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr)
        return 1;
    const std::vector<std::wstring_view> args(argv + 1, argv + argc);
    int exitCode = 0;
    if (args.empty()) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES |
                                                            ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES |
                                                            ICC_PROGRESS_CLASS};
        InitCommonControlsEx(&controls);
        {
            pdftweak::gui::MainWindow window(true);
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                if (!pdftweak::gui::translateFrameMessage(msg)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
        }
        CoUninitialize();
    } else {
        // Started by pdftweak.com, the process inherits the relay's standard handles. Started
        // directly from a console, it has none, and attaching to the parent console supplies them.
        AttachConsole(ATTACH_PARENT_PROCESS);
        exitCode = pdftweak::runCommandLine(args);
    }
    LocalFree(argv);
    return exitCode;
}
