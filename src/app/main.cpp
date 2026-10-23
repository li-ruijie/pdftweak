#include <windows.h>
#include <shellapi.h>

#include <string_view>
#include <vector>

#include "core/cli.h"
#include "core/version.h"

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
        // Milestone 3 replaces this message box with the main window.
        MessageBoxW(nullptr, L"The window arrives in milestone 3.",
                    L"" PDFTWEAK_PROGRAM_NAME " " PDFTWEAK_VERSION_STRING, MB_OK | MB_ICONINFORMATION);
    } else {
        // Started by pdftweak.com, the process inherits the relay's standard handles. Started
        // directly from a console, it has none, and attaching to the parent console supplies them.
        AttachConsole(ATTACH_PARENT_PROCESS);
        exitCode = pdftweak::runCommandLine(args);
    }
    LocalFree(argv);
    return exitCode;
}
