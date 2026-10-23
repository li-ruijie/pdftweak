#include "gui/window.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>

#pragma comment(lib, "comctl32.lib")

namespace pdftweak::gui {
namespace {

const wchar_t* const kPageClass = L"PdfTweakPage";
const wchar_t* const kFrameClass = L"PdfTweakFrame";
const wchar_t* const kTooltipProperty = L"PdfTweakTooltip";
// Posted by a combo box's CBN_SELENDOK, to run after any CBN_SELCHANGE of the same choice.
const UINT kComboChosen = WM_APP + 1;
// Posted by Page::later, with the work in lParam.
const UINT kLater = WM_APP + 2;

} // namespace

HFONT messageFont(HWND window) {
    static std::map<UINT, HFONT> fonts;
    const UINT dpi = GetDpiForWindow(window);
    auto found = fonts.find(dpi);
    if (found != fonts.end())
        return found->second;
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi);
    const HFONT font = CreateFontIndirectW(&metrics.lfMessageFont);
    fonts[dpi] = font;
    return font;
}

int scaled(HWND window, int pixels) {
    return MulDiv(pixels, static_cast<int>(GetDpiForWindow(window)), 96);
}

std::wstring windowText(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(static_cast<size_t>(copied));
    return text;
}

void setWindowText(HWND hwnd, const std::wstring& text) { SetWindowTextW(hwnd, text.c_str()); }

bool isChecked(HWND button) { return SendMessageW(button, BM_GETCHECK, 0, 0) == BST_CHECKED; }

void setChecked(HWND button, bool checked) {
    SendMessageW(button, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

bool isEnabled(HWND hwnd) { return IsWindowEnabled(hwnd) != FALSE; }

void enable(HWND hwnd, bool enabled) { EnableWindow(hwnd, enabled ? TRUE : FALSE); }

int selectedIndex(HWND combo) { return static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0)); }

void selectIndex(HWND combo, int index) {
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
}

int itemCount(HWND combo) { return static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0)); }

std::wstring itemText(HWND combo, int index) {
    const LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, static_cast<WPARAM>(index), 0);
    if (length == CB_ERR)
        return {};
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(text.data()));
    text.resize(static_cast<size_t>(length));
    return text;
}

void setItems(HWND combo, const std::vector<std::wstring>& items) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const std::wstring& item : items)
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    selectIndex(combo, items.empty() ? -1 : 0);
}

HWND createChild(HWND parent, const wchar_t* className, const std::wstring& text, DWORD style,
                 DWORD exStyle, int id) {
    const HWND hwnd = CreateWindowExW(exStyle, className, text.c_str(), WS_CHILD | WS_VISIBLE | style, 0, 0,
                                      0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                      GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(messageFont(parent)), FALSE);
    return hwnd;
}

void setTooltip(HWND control, const std::wstring& text) {
    auto tooltip = static_cast<HWND>(GetPropW(control, kTooltipProperty));
    TTTOOLINFOW info{};
    info.cbSize = sizeof(info);
    info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    info.hwnd = GetParent(control);
    info.uId = reinterpret_cast<UINT_PTR>(control);
    info.lpszText = const_cast<wchar_t*>(text.c_str());
    if (tooltip == nullptr) {
        tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
                                  CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                  GetParent(control), nullptr, GetModuleHandleW(nullptr), nullptr);
        SetPropW(control, kTooltipProperty, tooltip);
        SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    } else {
        SendMessageW(tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&info));
    }
}

struct Page::Split {
    Component left;
    Component right;
    RECT area{};
    RECT divider{};
    int adjust = 0;
    int dragStartX = 0;
    int dragStartAdjust = 0;

    void place(HWND page, const RECT& rect) {
        area = rect;
        const int dividerWidth = scaled(page, 6);
        const int width = rect.right - rect.left;
        const int leftPreferred = left.preferred().cx;
        const int extra = width - leftPreferred - right.preferred().cx - dividerWidth;
        const int leftWidth =
            std::clamp(leftPreferred + extra / 2 + adjust, 0, std::max(0, width - dividerWidth));
        left.place({rect.left, rect.top, rect.left + leftWidth, rect.bottom});
        divider = {rect.left + leftWidth, rect.top, rect.left + leftWidth + dividerWidth, rect.bottom};
        right.place({divider.right, rect.top, rect.right, rect.bottom});
    }
};

Page::Page() = default;

Page::~Page() {
    if (hwnd_ != nullptr && IsWindow(hwnd_)) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
}

HWND Page::create(HWND parent) {
    static const ATOM registered = [] {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = proc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kPageClass;
        return RegisterClassExW(&windowClass);
    }();
    (void)registered;
    hwnd_ = CreateWindowExW(WS_EX_CONTROLPARENT, kPageClass, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0,
                            parent, nullptr, GetModuleHandleW(nullptr), this);
    build();
    return hwnd_;
}

SIZE Page::preferredSize() const {
    SIZE size = root_.preferred ? root_.preferred() : SIZE{0, 0};
    const int margin = scaled(hwnd_, margin_);
    return {size.cx + 2 * margin, size.cy + 2 * margin};
}

void Page::relayout() {
    if (!root_.place)
        return;
    RECT client;
    GetClientRect(hwnd_, &client);
    const int margin = scaled(hwnd_, margin_);
    InflateRect(&client, -margin, -margin);
    root_.place(client);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void Page::chooseItem(HWND combo, int index) {
    selectIndex(combo, index);
    SendMessageW(hwnd_, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(combo), CBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(combo));
}

void Page::later(std::function<void()> work) {
    PostMessageW(hwnd_, kLater, 0, reinterpret_cast<LPARAM>(new std::function<void()>(std::move(work))));
}

LRESULT Page::message(UINT msg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

FormLayout& Page::newLayout(std::wstring_view columns, std::wstring_view rows) {
    layouts_.push_back(std::make_unique<FormLayout>(hwnd_, columns, rows));
    return *layouts_.back();
}

HWND Page::button(const std::wstring& text, std::function<void()> onClick) {
    const HWND hwnd = createChild(hwnd_, WC_BUTTONW, text, WS_TABSTOP | BS_PUSHBUTTON);
    onCommand(hwnd, [onClick = std::move(onClick)](int code) {
        if (code == BN_CLICKED && onClick)
            onClick();
    });
    return hwnd;
}

HWND Page::checkBox(const std::wstring& text, std::function<void()> onClick) {
    const HWND hwnd = createChild(hwnd_, WC_BUTTONW, text, WS_TABSTOP | BS_AUTOCHECKBOX);
    onCommand(hwnd, [onClick = std::move(onClick)](int code) {
        if (code == BN_CLICKED && onClick)
            onClick();
    });
    return hwnd;
}

HWND Page::label(const std::wstring& text) {
    return createChild(hwnd_, WC_STATICW, text, SS_LEFTNOWORDWRAP | SS_NOPREFIX);
}

HWND Page::edit(const std::wstring& text, int columns, DWORD style) {
    const HWND hwnd =
        createChild(hwnd_, WC_EDITW, text, WS_TABSTOP | ES_AUTOHSCROLL | style, WS_EX_CLIENTEDGE);
    if (columns > 0)
        SetPropW(hwnd, kColumnsProperty, reinterpret_cast<HANDLE>(static_cast<INT_PTR>(columns)));
    return hwnd;
}

HWND Page::comboBox(const std::vector<std::wstring>& items, std::function<void()> onAction) {
    const HWND hwnd = createChild(hwnd_, WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST);
    setItems(hwnd, items);
    onCommand(hwnd, [this, hwnd, onAction = std::move(onAction)](int code) {
        if (!onAction)
            return;
        if (code == CBN_DROPDOWN) {
            droppedIndex_[hwnd] = selectedIndex(hwnd);
        } else if (code == CBN_SELCHANGE) {
            droppedIndex_.erase(hwnd);
            onAction();
        } else if (code == CBN_SELENDOK) {
            PostMessageW(hwnd_, kComboChosen, 0, reinterpret_cast<LPARAM>(hwnd));
        } else if (code == CBN_SELENDCANCEL) {
            droppedIndex_.erase(hwnd);
        }
    });
    return hwnd;
}

HWND Page::separator() { return createChild(hwnd_, WC_STATICW, L"", SS_ETCHEDHORZ); }

HWND Page::groupBox(const std::wstring& text) { return createChild(hwnd_, WC_BUTTONW, text, BS_GROUPBOX); }

void Page::onCommand(HWND control, std::function<void(int code)> handler) {
    commands_[control] = std::move(handler);
}

void Page::forget(HWND control) {
    commands_.erase(control);
    notifies_.erase(control);
    droppedIndex_.erase(control);
}

Component Page::childPage(Page& child) {
    const HWND hwnd = child.create(hwnd_);
    ShowWindow(hwnd, SW_SHOW);
    Component component;
    component.preferred = [&child]() { return child.preferredSize(); };
    component.place = [hwnd](const RECT& rect) {
        SetWindowPos(hwnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    };
    return component;
}

void Page::onNotify(HWND control, std::function<LRESULT(NMHDR*)> handler) {
    notifies_[control] = std::move(handler);
}

Component Page::titled(HWND groupBox, Component inner) {
    Component component;
    component.preferred = [groupBox, inner]() -> SIZE {
        const SIZE size = inner.preferred();
        const int side = scaled(groupBox, 6);
        const int top = dluToPixelsY(groupBox, 8) + scaled(groupBox, 2);
        return {size.cx + 2 * side, size.cy + top + side};
    };
    component.place = [groupBox, inner](const RECT& rect) {
        SetWindowPos(groupBox, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        const int side = scaled(groupBox, 6);
        const int top = dluToPixelsY(groupBox, 8) + scaled(groupBox, 2);
        inner.place({rect.left + side, rect.top + top, rect.right - side, rect.bottom - side});
    };
    return component;
}

Component Page::splitPane(Component left, Component right) {
    splits_.push_back(std::make_unique<Split>());
    Split* split = splits_.back().get();
    split->left = std::move(left);
    split->right = std::move(right);
    Component component;
    component.preferred = [this, split]() -> SIZE {
        const SIZE l = split->left.preferred();
        const SIZE r = split->right.preferred();
        return {l.cx + scaled(hwnd_, 6) + r.cx, std::max(l.cy, r.cy)};
    };
    component.place = [this, split](const RECT& rect) { split->place(hwnd_, rect); };
    return component;
}

Page::Split* Page::splitAt(POINT point) {
    for (const auto& split : splits_)
        if (PtInRect(&split->divider, point))
            return split.get();
    return nullptr;
}

LRESULT CALLBACK Page::proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* page = static_cast<Page*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        page->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(page));
    }
    auto* page = reinterpret_cast<Page*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (page == nullptr)
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    return page->handle(msg, wParam, lParam);
}

LRESULT Page::handle(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE: relayout(); return 0;
    case WM_COMMAND:
    case WM_HSCROLL: {
        const auto control = reinterpret_cast<HWND>(lParam);
        const auto found = commands_.find(control);
        if (control != nullptr && found != commands_.end()) {
            // a copy, as the handler may replace itself
            const auto handler = found->second;
            handler(msg == WM_COMMAND ? HIWORD(wParam) : LOWORD(wParam));
            return 0;
        }
        break;
    }
    case kLater: {
        const std::unique_ptr<std::function<void()>> work(reinterpret_cast<std::function<void()>*>(lParam));
        (*work)();
        return 0;
    }
    case kComboChosen: {
        // the user picked the item that was already selected, which sends no CBN_SELCHANGE
        const auto combo = reinterpret_cast<HWND>(lParam);
        const auto found = droppedIndex_.find(combo);
        if (found != droppedIndex_.end()) {
            droppedIndex_.erase(found);
            const auto handler = commands_[combo];
            handler(CBN_SELCHANGE);
        }
        return 0;
    }
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<NMHDR*>(lParam);
        const auto found = notifies_.find(header->hwndFrom);
        if (found != notifies_.end()) {
            const auto handler = found->second;
            return handler(reinterpret_cast<NMHDR*>(lParam));
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        wchar_t name[16];
        GetClassNameW(reinterpret_cast<HWND>(lParam), name, 16);
        if (_wcsicmp(name, WC_EDITW) == 0)
            break; // read-only and disabled fields keep their grey
        SetBkColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && reinterpret_cast<HWND>(wParam) == hwnd_) {
            POINT point;
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            if (dragging_ != nullptr || splitAt(point) != nullptr) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;
    case WM_LBUTTONDOWN: {
        const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        dragging_ = splitAt(point);
        if (dragging_ != nullptr) {
            dragging_->dragStartX = point.x;
            dragging_->dragStartAdjust = dragging_->adjust;
            SetCapture(hwnd_);
            return 0;
        }
        break;
    }
    case WM_MOUSEMOVE:
        if (dragging_ != nullptr) {
            dragging_->adjust = dragging_->dragStartAdjust + GET_X_LPARAM(lParam) - dragging_->dragStartX;
            dragging_->place(hwnd_, dragging_->area);
            InvalidateRect(hwnd_, nullptr, TRUE);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (dragging_ != nullptr) {
            dragging_ = nullptr;
            if (msg == WM_LBUTTONUP)
                ReleaseCapture();
            return 0;
        }
        break;
    }
    return message(msg, wParam, lParam);
}

Frame::Frame(HWND owner, const std::wstring& title, Page& content, bool resizable)
    : owner_(owner), content_(content) {
    static const ATOM registered = [] {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = proc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kFrameClass;
        return RegisterClassExW(&windowClass);
    }();
    (void)registered;
    const DWORD style = resizable ? WS_OVERLAPPEDWINDOW : WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD exStyle = WS_EX_CONTROLPARENT | (resizable ? 0 : WS_EX_DLGMODALFRAME);
    hwnd_ = CreateWindowExW(exStyle, kFrameClass, title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, 100,
                            100, owner, nullptr, GetModuleHandleW(nullptr), this);
    ShowWindow(content_.create(hwnd_), SW_SHOW);

    // the size of the page, centred on the owner or on the screen, as pack and setLocationRelativeTo
    const SIZE preferred = content_.preferredSize();
    RECT frame{0, 0, preferred.cx, preferred.cy};
    AdjustWindowRectExForDpi(&frame, style, FALSE, exStyle, GetDpiForWindow(hwnd_));
    const int width = frame.right - frame.left;
    const int height = frame.bottom - frame.top;
    RECT around;
    if (owner != nullptr) {
        GetWindowRect(owner, &around);
    } else {
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &monitor);
        around = monitor.rcWork;
    }
    const int x = around.left + (around.right - around.left - width) / 2;
    const int y = around.top + (around.bottom - around.top - height) / 2;
    SetWindowPos(hwnd_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

Frame::~Frame() {
    if (hwnd_ != nullptr && IsWindow(hwnd_)) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
}

void Frame::runModal() {
    const bool ownerEnabled = owner_ != nullptr && IsWindowEnabled(owner_);
    if (ownerEnabled)
        EnableWindow(owner_, FALSE);
    ShowWindow(hwnd_, SW_SHOW);
    MSG msg;
    while (IsWindow(hwnd_) && IsWindowVisible(hwnd_)) {
        const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0) {
            PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (got == -1)
            break;
        if (!translateFrameMessage(msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    // enabled again before the frame goes, so that the owner becomes the active window
    if (ownerEnabled)
        EnableWindow(owner_, TRUE);
    if (IsWindow(hwnd_))
        ShowWindow(hwnd_, SW_HIDE);
    if (owner_ != nullptr)
        SetActiveWindow(owner_);
}

LRESULT CALLBACK Frame::proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* frame = static_cast<Frame*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        frame->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(frame));
    }
    auto* frame = reinterpret_cast<Frame*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (frame == nullptr)
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    return frame->handle(msg, wParam, lParam);
}

LRESULT Frame::handle(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        if (content_.handle() != nullptr)
            MoveWindow(content_.handle(), 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        return 0;
    case WM_DPICHANGED: {
        const HFONT font = messageFont(hwnd_);
        EnumChildWindows(
            hwnd_,
            [](HWND child, LPARAM fontParam) -> BOOL {
                SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(fontParam), TRUE);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(font));
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        content_.relayout();
        return 0;
    }
    case WM_COMMAND:
        if ((LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) && onDialogCommand) {
            const auto handler = onDialogCommand;
            handler(LOWORD(wParam));
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if (onSetCursor && LOWORD(lParam) == HTCLIENT && onSetCursor(reinterpret_cast<HWND>(wParam)))
            return TRUE;
        break;
    case WM_CLOSE:
        if (onClose) {
            const auto handler = onClose;
            handler();
        } else {
            if (owner_ != nullptr)
                EnableWindow(owner_, TRUE);
            ShowWindow(hwnd_, SW_HIDE);
        }
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

bool translateFrameMessage(MSG& msg) {
    const HWND root = msg.hwnd != nullptr ? GetAncestor(msg.hwnd, GA_ROOT) : nullptr;
    if (root == nullptr)
        return false;
    wchar_t name[32];
    GetClassNameW(root, name, 32);
    if (wcscmp(name, kFrameClass) != 0)
        return false;
    auto* frame = reinterpret_cast<Frame*>(GetWindowLongPtrW(root, GWLP_USERDATA));
    if (frame != nullptr && msg.message == WM_KEYDOWN && frame->onKey && frame->onKey(msg))
        return true;
    return IsDialogMessageW(root, &msg) != FALSE;
}

} // namespace pdftweak::gui
