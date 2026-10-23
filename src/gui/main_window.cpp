#include "gui/main_window.h"

#include "core/error.h"
#include "core/text.h"
#include "core/version.h"
#include "gui/dialogs.h"
#include "gui/tabs.h"

#include <commctrl.h>

#include <algorithm>
#include <new>

namespace pdftweak::gui {

// The content of the window: the tab control over the Run and Quit buttons.
class MainWindow::Content final : public Page {
public:
    explicit Content(MainWindow& owner) : owner_(owner) {}

    HWND tabControl = nullptr;

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

        FormLayout& layout = newLayout(L"f:p:g,f:p,f:p", L"f:p:g,f:p");
        layout.add(tabs, 1, 1, 3);
        const HWND run = createChild(handle(), WC_BUTTONW, L"Run", WS_TABSTOP | BS_DEFPUSHBUTTON, 0, IDOK);
        onCommand(run, [this](int code) {
            if (code == BN_CLICKED)
                owner_.runTweaks();
        });
        layout.add(run, 2, 2);
        layout.add(button(L"Quit", [] { PostQuitMessage(0); }), 3, 2);
        setLayout(layout.asComponent());
        showSelected();
    }

    MainWindow& owner_;
};

MainWindow::MainWindow(bool visible) {
    tabs_.push_back(std::make_unique<InputTab>(*this));
    tabs_.push_back(std::make_unique<PageSizeTab>(*this));
    tabs_.push_back(std::make_unique<WatermarkTab>(*this));
    tabs_.push_back(std::make_unique<ShuffleTab>(*this));
    tabs_.push_back(std::make_unique<PageNumberTab>(*this));
    for (const wchar_t* name :
         {L"Bookmarks", L"Attachments", L"Interaction", L"Document Info", L"Encrypt/Sign", L"Output"})
        tabs_.push_back(std::make_unique<PlaceholderTab>(*this, name));
    content_ = std::make_unique<Content>(*this);
    frame_ = std::make_unique<Frame>(nullptr, programTitle(), *content_, true);
    frame_->onDialogCommand = [this](int id) {
        if (id == IDOK)
            runTweaks();
    };
    frame_->onClose = [] { PostQuitMessage(0); };
    frame_->onKey = [this](const MSG& msg) { return content_->key(msg); };
    if (visible)
        ShowWindow(frame_->handle(), SW_SHOW);
}

MainWindow::~MainWindow() = default;

HWND MainWindow::handle() const { return frame_ ? frame_->handle() : nullptr; }

InputTab& MainWindow::inputTab() { return tab<InputTab>(); }

void MainWindow::runTweaks() {
    try {
        for (const auto& tab : tabs_)
            tab->checkRun();
        std::vector<Operation> operations;
        for (const auto& tab : tabs_)
            if (Operation operation = tab->prepare())
                operations.push_back(std::move(operation));
        NullProgress progress;
        for (int task = 0; task < inputTab().batchLength(); ++task) {
            RunContext context{task, progress, nullptr};
            try {
                for (const Operation& operation : operations)
                    operation(context);
            } catch (...) {
                if (context.tweak)
                    context.tweak->cleanup();
                throw;
            }
            if (context.tweak)
                context.tweak->cleanup();
        }
        messageBox(handle(), L"Finished", L"" PDFTWEAK_PROGRAM_NAME, MB_ICONINFORMATION);
    } catch (const Error& error) {
        messageBox(handle(), fromUtf8(error.what()), L"Error", MB_ICONERROR);
    } catch (const std::bad_alloc& error) {
        messageBox(
            handle(),
            L"" PDFTWEAK_PROGRAM_NAME L" has run out of memory. You can enable the Tempfile option on the "
            L"output tab.",
            L"Out of memory: " + fromUtf8(error.what()), MB_ICONERROR);
    } catch (const std::exception& error) {
        messageBox(handle(), fromUtf8(error.what()), L"Error", MB_ICONERROR);
    }
}

} // namespace pdftweak::gui
