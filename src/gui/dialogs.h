#pragma once

// The dialogs of the interface: file choosers, the colour chooser, the owner password box, and the
// message boxes of JOptionPane. Each goes through a hook that the gui tests replace, so that a test
// answers the dialog instead of a user.

#include <windows.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pdftweak::gui {

// The file choosers of the Java version. MainForm's chooser of PDF files keeps its folder between
// uses and filters "PDF files (*.pdf)". The others, made new for each use, start in the Documents
// folder and show every file.
enum class Chooser { Pdf, Other };

struct DialogHooks {
    // JOptionPane.showMessageDialog and showConfirmDialog, as MessageBoxW: returns IDOK, IDYES, or IDNO.
    std::function<int(HWND owner, const std::wstring& text, const std::wstring& title, UINT flags)>
        messageBox;
    // JFileChooser.showOpenDialog, with one or more files. Empty when cancelled.
    std::function<std::vector<std::filesystem::path>(HWND owner, Chooser chooser, bool multiple)> openFiles;
    // JFileChooser.showSaveDialog. Empty when cancelled.
    std::function<std::optional<std::filesystem::path>(HWND owner, Chooser chooser)> saveFile;
    // JColorChooser.showDialog. Empty when cancelled.
    std::function<std::optional<COLORREF>(HWND owner, COLORREF initial)> chooseColor;
    // PasswordInputBox.askForPassword. Empty when cancelled.
    std::function<std::optional<std::wstring>(HWND owner)> askForPassword;
};

// The hooks in use, which start with the real dialogs.
DialogHooks& dialogHooks();

int messageBox(HWND owner, const std::wstring& text, const std::wstring& title, UINT flags);
std::vector<std::filesystem::path> openFiles(HWND owner, Chooser chooser, bool multiple);
std::optional<std::filesystem::path> saveFile(HWND owner, Chooser chooser);
std::optional<COLORREF> chooseColor(HWND owner, COLORREF initial);
std::optional<std::wstring> askForPassword(HWND owner);

// The "Overwrite existing file?" question of the Output and Bookmarks tabs. Returns whether to go on.
bool confirmOverwrite(HWND owner);

// The program name and version, as the window title and message boxes show them.
std::wstring programTitle();

// Main's uncaught-exception handler: the error box for a failure that no handler expected, with the
// message of the failure, or none, and the version. Java's box also asked for a report at the
// project's tracker, which the port does not have.
void showUnexpectedError(HWND owner, const char* message);

} // namespace pdftweak::gui
