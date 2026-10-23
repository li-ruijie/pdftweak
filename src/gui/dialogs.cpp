#include "gui/dialogs.h"

#include "core/version.h"
#include "gui/window.h"

#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace pdftweak::gui {
namespace {

// The dialog states of the two choosers, kept apart by the Common Item Dialog.
const GUID kPdfChooser = {0x6f1c2a7e, 0x3b1d, 0x4c55, {0x9a, 0x0e, 0x51, 0x7d, 0x2c, 0x84, 0x4b, 0x01}};
const GUID kOtherChooser = {0x6f1c2a7e, 0x3b1d, 0x4c55, {0x9a, 0x0e, 0x51, 0x7d, 0x2c, 0x84, 0x4b, 0x02}};

template <typename T> struct ComPtr {
    T* p = nullptr;
    ~ComPtr() {
        if (p != nullptr)
            p->Release();
    }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
};

std::filesystem::path itemPath(IShellItem* item) {
    wchar_t* name = nullptr;
    std::filesystem::path path;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
        path = name;
        CoTaskMemFree(name);
    }
    return path;
}

// Sets the filter and starting folder of a chooser.
void prepare(IFileDialog* dialog, Chooser chooser) {
    if (chooser == Chooser::Pdf) {
        dialog->SetClientGuid(kPdfChooser);
        const COMDLG_FILTERSPEC types[] = {{L"PDF files (*.pdf)", L"*.pdf"}, {L"All Files", L"*.*"}};
        dialog->SetFileTypes(2, types);
        dialog->SetFileTypeIndex(1);
    } else {
        dialog->SetClientGuid(kOtherChooser);
        ComPtr<IShellItem> documents;
        if (SUCCEEDED(SHGetKnownFolderItem(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr,
                                           IID_PPV_ARGS(&documents))))
            dialog->SetFolder(documents.p);
    }
}

std::vector<std::filesystem::path> showOpen(HWND owner, Chooser chooser, bool multiple) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(
            CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return {};
    prepare(dialog.p, chooser);
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | (multiple ? FOS_ALLOWMULTISELECT : 0));
    if (dialog->Show(owner) != S_OK)
        return {};
    ComPtr<IShellItemArray> results;
    if (FAILED(dialog->GetResults(&results)))
        return {};
    DWORD count = 0;
    results->GetCount(&count);
    std::vector<std::filesystem::path> paths;
    for (DWORD i = 0; i < count; ++i) {
        ComPtr<IShellItem> item;
        if (SUCCEEDED(results->GetItemAt(i, &item)))
            paths.push_back(itemPath(item.p));
    }
    return paths;
}

std::optional<std::filesystem::path> showSave(HWND owner, Chooser chooser) {
    ComPtr<IFileSaveDialog> dialog;
    if (FAILED(
            CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return std::nullopt;
    prepare(dialog.p, chooser);
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    // the tabs add the extension and ask about overwriting themselves, as Java's did
    dialog->SetOptions((options | FOS_FORCEFILESYSTEM | FOS_NOTESTFILECREATE) & ~FOS_OVERWRITEPROMPT);
    if (dialog->Show(owner) != S_OK)
        return std::nullopt;
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item)))
        return std::nullopt;
    return itemPath(item.p);
}

std::optional<COLORREF> showColor(HWND owner, COLORREF initial) {
    static COLORREF custom[16] = {};
    CHOOSECOLORW choose{};
    choose.lStructSize = sizeof(choose);
    choose.hwndOwner = owner;
    choose.rgbResult = initial;
    choose.lpCustColors = custom;
    choose.Flags = CC_RGBINIT | CC_FULLOPEN;
    if (!ChooseColorW(&choose))
        return std::nullopt;
    return choose.rgbResult;
}

// PasswordInputBox: a password field over OK and Cancel.
class PasswordPage final : public Page {
public:
    std::function<void()> onOk;
    std::function<void()> onCancel;
    HWND password = nullptr;

private:
    void build() override {
        FormLayout& layout = newLayout(L"f:p:g, f:p, f:p", L"f:p, f:p, f:p:g");
        password = edit(L"", 60, ES_PASSWORD);
        layout.add(password, 1, 1, 3);
        layout.add(button(L"OK", [this] { onOk(); }), 2, 2);
        layout.add(button(L"Cancel", [this] { onCancel(); }), 3, 2);
        setLayout(layout.asComponent());
    }
};

std::optional<std::wstring> showPassword(HWND owner) {
    PasswordPage page;
    Frame frame(owner, L"Enter owner password", page, false);
    bool ok = false;
    const auto close = [&frame, owner] {
        if (owner != nullptr)
            EnableWindow(owner, TRUE);
        ShowWindow(frame.handle(), SW_HIDE);
    };
    page.onOk = [&] {
        ok = true;
        close();
    };
    page.onCancel = close;
    frame.onDialogCommand = [&](int id) { id == IDOK ? page.onOk() : page.onCancel(); };
    frame.onClose = close;
    SetFocus(page.password);
    frame.runModal();
    if (!ok)
        return std::nullopt;
    return windowText(page.password);
}

} // namespace

DialogHooks& dialogHooks() {
    static DialogHooks hooks{
        [](HWND owner, const std::wstring& text, const std::wstring& title, UINT flags) {
            return MessageBoxW(owner, text.c_str(), title.c_str(), flags);
        },
        showOpen, showSave, showColor, showPassword};
    return hooks;
}

int messageBox(HWND owner, const std::wstring& text, const std::wstring& title, UINT flags) {
    return dialogHooks().messageBox(owner, text, title, flags);
}

std::vector<std::filesystem::path> openFiles(HWND owner, Chooser chooser, bool multiple) {
    return dialogHooks().openFiles(owner, chooser, multiple);
}

std::optional<std::filesystem::path> saveFile(HWND owner, Chooser chooser) {
    return dialogHooks().saveFile(owner, chooser);
}

std::optional<COLORREF> chooseColor(HWND owner, COLORREF initial) {
    return dialogHooks().chooseColor(owner, initial);
}

std::optional<std::wstring> askForPassword(HWND owner) { return dialogHooks().askForPassword(owner); }

bool confirmOverwrite(HWND owner) {
    return messageBox(owner, L"Overwrite existing file?", L"Confirm Overwrite",
                      MB_YESNO | MB_ICONQUESTION) != IDNO;
}

std::wstring programTitle() { return L"" PDFTWEAK_PROGRAM_NAME " " PDFTWEAK_VERSION_STRING; }

} // namespace pdftweak::gui
