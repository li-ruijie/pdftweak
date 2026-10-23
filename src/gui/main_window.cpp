#include "gui/main_window.h"

#include "core/error.h"
#include "core/text.h"
#include "core/version.h"
#include "gui/dialogs.h"
#include "gui/run.h"
#include "gui/tabs.h"

#include <commctrl.h>
#include <shobjidl.h>

#include <algorithm>
#include <cmath>
#include <new>

namespace pdftweak::gui {
namespace {

// Posted by the run to the content of the window.
const UINT kRunProgress = WM_APP + 20;
const UINT kRunDone = WM_APP + 21;

} // namespace

// The content of the window: the tab control over the Run and Quit buttons, which the progress
// panel replaces during a run.
class MainWindow::Content final : public Page {
public:
    explicit Content(MainWindow& owner) : owner_(owner) {}

    HWND tabControl = nullptr;
    HWND runButton = nullptr;
    HWND quitButton = nullptr;
    HWND fileLine = nullptr;
    HWND stageLine = nullptr;
    HWND bar = nullptr;
    HWND percent = nullptr;
    HWND cancelButton = nullptr;

    // Shows the page of the selected tab and hides the others.
    void showSelected() {
        const int selected = TabCtrl_GetCurSel(tabControl);
        for (int i = 0; i < owner_.tabCount(); ++i)
            ShowWindow(owner_.tab(i).handle(), i == selected ? SW_SHOW : SW_HIDE);
    }

    // Ctrl+Tab and Ctrl+Page Down select the next tab, with Shift or Page Up the one before.
    bool key(const MSG& msg) {
        if ((GetKeyState(VK_CONTROL) & 0x8000) == 0)
            return false;
        int step = 0;
        if (msg.wParam == VK_TAB)
            step = (GetKeyState(VK_SHIFT) & 0x8000) != 0 ? -1 : 1;
        else if (msg.wParam == VK_NEXT)
            step = 1;
        else if (msg.wParam == VK_PRIOR)
            step = -1;
        if (step == 0)
            return false;
        const int count = owner_.tabCount();
        TabCtrl_SetCurSel(tabControl, (TabCtrl_GetCurSel(tabControl) + step + count) % count);
        showSelected();
        return true;
    }

    // Shows the progress panel in place of the buttons, or the buttons again.
    void showProgress(bool running) {
        running_ = running;
        for (HWND hwnd : {runButton, quitButton})
            ShowWindow(hwnd, running ? SW_HIDE : SW_SHOW);
        for (HWND hwnd : {fileLine, stageLine, bar, percent, cancelButton})
            ShowWindow(hwnd, running ? SW_SHOW : SW_HIDE);
        enable(cancelButton, true);
        enable(tabControl, !running);
        for (int i = 0; i < owner_.tabCount(); ++i)
            enable(owner_.tab(i).handle(), !running);
        relayout();
    }

    void showStatus(const RunStatus& status) {
        setWindowText(fileLine, status.fileLine());
        setWindowText(stageLine, status.stageLine());
        const double fraction = status.fraction();
        SendMessageW(bar, PBM_SETPOS, static_cast<WPARAM>(std::lround(fraction * 1000)), 0);
        setWindowText(percent, std::to_wstring(static_cast<int>(fraction * 100)) + L" %");
    }

private:
    void build() override {
        tabControl =
            createChild(handle(), WC_TABCONTROLW, L"", WS_TABSTOP | WS_CLIPSIBLINGS | TCS_MULTILINE);
        for (int i = 0; i < owner_.tabCount(); ++i) {
            Tab& tab = owner_.tab(i);
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(tab.name());
            TabCtrl_InsertItem(tabControl, i, &item);
            SetWindowPos(tab.create(handle()), HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        onNotify(tabControl, [this](NMHDR* header) -> LRESULT {
            if (header->code == TCN_SELCHANGE)
                showSelected();
            return 0;
        });

        // the tab control as large as the largest page needs, with the pages over its display area
        Component tabs;
        tabs.preferred = [this]() -> SIZE {
            SIZE largest{0, 0};
            for (int i = 0; i < owner_.tabCount(); ++i) {
                const SIZE size = owner_.tab(i).preferredSize();
                largest.cx = std::max(largest.cx, size.cx);
                largest.cy = std::max(largest.cy, size.cy);
            }
            // the rows of tabs, which wrap as in Swing, depend on the width of the control
            RECT rect{0, 0, largest.cx, largest.cy};
            TabCtrl_AdjustRect(tabControl, TRUE, &rect);
            RECT current;
            GetWindowRect(tabControl, &current);
            SetWindowPos(tabControl, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            rect = {0, 0, largest.cx, largest.cy};
            TabCtrl_AdjustRect(tabControl, TRUE, &rect);
            SetWindowPos(tabControl, nullptr, 0, 0, current.right - current.left,
                         current.bottom - current.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            return {rect.right - rect.left, rect.bottom - rect.top};
        };
        tabs.place = [this](const RECT& rect) {
            SetWindowPos(tabControl, nullptr, rect.left, rect.top, rect.right - rect.left,
                         rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
            RECT display = rect;
            TabCtrl_AdjustRect(tabControl, FALSE, &display);
            for (int i = 0; i < owner_.tabCount(); ++i)
                SetWindowPos(owner_.tab(i).handle(), nullptr, display.left, display.top,
                             display.right - display.left, display.bottom - display.top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
        };

        FormLayout& buttons = newLayout(L"f:p:g,f:p,f:p", L"f:p");
        runButton = createChild(handle(), WC_BUTTONW, L"Run", WS_TABSTOP | BS_DEFPUSHBUTTON, 0, IDOK);
        onCommand(runButton, [this](int code) {
            if (code == BN_CLICKED)
                owner_.runTweaks();
        });
        buttons.add(runButton, 2, 1);
        quitButton = button(L"Quit", [] { PostQuitMessage(0); });
        buttons.add(quitButton, 3, 1);

        // the progress panel of spec section 9.1: the file, the stage, and the bar with Cancel
        FormLayout& panel = newLayout(L"f:p:g, f:p, f:p", L"f:p, f:p, f:p");
        fileLine = createChild(handle(), WC_STATICW, L"", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS);
        stageLine = createChild(handle(), WC_STATICW, L"", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS);
        const auto line = [this](HWND hwnd) {
            // as wide as the window allows, so that a long name does not push Cancel away
            Component component = control(hwnd);
            component.preferred = [hwnd]() -> SIZE { return {0, dluToPixelsY(hwnd, 8) + scaled(hwnd, 4)}; };
            return component;
        };
        panel.add(line(fileLine), 1, 1, 3);
        panel.add(line(stageLine), 1, 2, 3);
        bar = createChild(handle(), PROGRESS_CLASSW, L"", 0);
        SendMessageW(bar, PBM_SETRANGE32, 0, 1000);
        panel.add(sized(bar, {200, 20}), 1, 3);
        percent = label(L"100 %");
        panel.add(percent, 2, 3);
        cancelButton = button(L"Cancel", [this] { owner_.cancel(); });
        panel.add(cancelButton, 3, 3);

        Component bottom;
        bottom.preferred = [this, &buttons, &panel]() {
            return running_ ? panel.preferredSize() : buttons.preferredSize();
        };
        bottom.place = [this, &buttons, &panel](const RECT& rect) {
            if (running_)
                panel.layout(rect);
            else
                buttons.layout(rect);
        };

        FormLayout& layout = newLayout(L"f:p:g", L"f:p:g, f:p");
        layout.add(tabs, 1, 1);
        layout.add(bottom, 1, 2);
        setLayout(layout.asComponent());
        showSelected();
        showProgress(false);
    }

    LRESULT message(UINT msg, WPARAM wParam, LPARAM lParam) override {
        if (msg == kRunProgress) {
            owner_.showProgress();
            return 0;
        }
        if (msg == kRunDone) {
            owner_.finishRun();
            return 0;
        }
        return Page::message(msg, wParam, lParam);
    }

    MainWindow& owner_;
    bool running_ = false;
};

MainWindow::MainWindow(bool visible) {
    tabs_.push_back(std::make_unique<InputTab>(*this));
    tabs_.push_back(std::make_unique<PageSizeTab>(*this));
    tabs_.push_back(std::make_unique<WatermarkTab>(*this));
    tabs_.push_back(std::make_unique<ShuffleTab>(*this));
    tabs_.push_back(std::make_unique<PageNumberTab>(*this));
    tabs_.push_back(std::make_unique<BookmarkTab>(*this));
    tabs_.push_back(std::make_unique<AttachmentTab>(*this));
    tabs_.push_back(std::make_unique<InteractionTab>(*this));
    tabs_.push_back(std::make_unique<DocumentInfoTab>(*this));
    tabs_.push_back(std::make_unique<EncryptSignTab>(*this));
    tabs_.push_back(std::make_unique<OutputTab>(*this));
    content_ = std::make_unique<Content>(*this);
    frame_ = std::make_unique<Frame>(nullptr, programTitle(), *content_, true);
    frame_->onDialogCommand = [this](int id) {
        if (id == IDOK)
            runTweaks();
    };
    frame_->onClose = [this] {
        // a close during a run cancels it and quits when the worker has stopped
        if (run_) {
            quitAfterRun_ = true;
            cancel();
        } else {
            PostQuitMessage(0);
        }
    };
    frame_->onKey = [this](const MSG& msg) { return content_->key(msg); };
    frame_->onSetCursor = [this](HWND over) {
        if (!run_ || over == content_->cancelButton)
            return false;
        SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        return true;
    };
    if (SUCCEEDED(
            CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&taskbar_))))
        taskbar_->HrInit();
    if (visible)
        ShowWindow(frame_->handle(), SW_SHOW);
}

MainWindow::~MainWindow() {
    run_.reset();
    if (taskbar_ != nullptr)
        taskbar_->Release();
}

HWND MainWindow::handle() const { return frame_ ? frame_->handle() : nullptr; }

InputTab& MainWindow::inputTab() { return tab<InputTab>(); }

void MainWindow::runTweaks() {
    if (run_)
        return;
    std::vector<NamedOperation> operations;
    try {
        for (const auto& tab : tabs_)
            tab->checkRun();
        for (const auto& tab : tabs_)
            if (Operation operation = tab->prepare())
                operations.push_back({tab->name(), std::move(operation)});
    } catch (const Error& error) {
        messageBox(handle(), fromUtf8(error.what()), L"Error", MB_ICONERROR);
        return;
    }
    content_->showProgress(true);
    if (taskbar_ != nullptr)
        taskbar_->SetProgressState(handle(), TBPF_NORMAL);
    run_ = std::make_unique<Run>(content_->handle(), kRunProgress, kRunDone, std::move(operations),
                                 inputTab().fileNames(), stageObserver_);
    showProgress();
}

void MainWindow::cancel() {
    if (!run_)
        return;
    run_->progress().cancel();
    // Cancel takes effect at the next page or stage boundary
    enable(content_->cancelButton, false);
}

void MainWindow::showProgress() {
    if (!run_)
        return;
    const RunStatus status = run_->progress().status();
    content_->showStatus(status);
    if (taskbar_ != nullptr)
        taskbar_->SetProgressValue(handle(), static_cast<ULONGLONG>(std::lround(status.fraction() * 1000)),
                                   1000);
}

void MainWindow::finishRun() {
    if (!run_)
        return;
    const RunResult result = run_->finish();
    run_.reset();
    content_->showProgress(false);
    if (quitAfterRun_) {
        PostQuitMessage(0);
        return;
    }
    if (taskbar_ != nullptr)
        taskbar_->SetProgressState(handle(),
                                   result.kind == RunResult::Kind::Failed ? TBPF_ERROR : TBPF_NOPROGRESS);
    messageBox(handle(), result.message, result.title,
               result.kind == RunResult::Kind::Failed ? MB_ICONERROR : MB_ICONINFORMATION);
    if (taskbar_ != nullptr)
        taskbar_->SetProgressState(handle(), TBPF_NOPROGRESS);
}

} // namespace pdftweak::gui
