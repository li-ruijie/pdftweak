#include "gui/preview.h"

#include "core/text.h"
#include "gui/window.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace pdftweak::gui {
namespace {

const wchar_t* const kPreviewClass = L"PdfTweakPreview";

XFORM translation(double x, double y) { return {1, 0, 0, 1, static_cast<FLOAT>(x), static_cast<FLOAT>(y)}; }

} // namespace

HWND Preview::create(HWND parent) {
    static const ATOM registered = [] {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = proc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = kPreviewClass;
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        return RegisterClassExW(&windowClass);
    }();
    (void)registered;
    hwnd_ = CreateWindowExW(0, kPreviewClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent, nullptr,
                            GetModuleHandleW(nullptr), this);
    return hwnd_;
}

void Preview::setConfig(std::vector<formats::ShuffleRule> rules) {
    rules_ = std::move(rules);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Preview::setPageFormat(float width, float height) {
    pageWidth_ = width;
    pageHeight_ = height;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

LRESULT CALLBACK Preview::proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(
            hwnd, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    auto* preview = reinterpret_cast<Preview*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (preview != nullptr) {
        if (msg == WM_ERASEBKGND)
            return 1;
        if (msg == WM_PAINT) {
            PAINTSTRUCT paint;
            const HDC dc = BeginPaint(hwnd, &paint);
            RECT client;
            GetClientRect(hwnd, &client);
            // drawn off screen, then copied, so that a resize does not flicker
            const HDC memory = CreateCompatibleDC(dc);
            const HBITMAP bitmap =
                CreateCompatibleBitmap(dc, std::max(1L, client.right), std::max(1L, client.bottom));
            const HGDIOBJ old = SelectObject(memory, bitmap);
            guardedProc(
                hwnd,
                [&] {
                    preview->paint(memory);
                    return LRESULT{0};
                },
                [] { return LRESULT{0}; });
            BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
            SelectObject(memory, old);
            DeleteObject(bitmap);
            DeleteDC(memory);
            EndPaint(hwnd, &paint);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void Preview::paint(HDC dc) {
    RECT client;
    GetClientRect(hwnd_, &client);
    FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));
    SetGraphicsMode(dc, GM_ADVANCED);
    // Java draws in units of 96 DPI that its graphics scale to the screen
    const double scale = GetDpiForWindow(hwnd_) / 96.0;
    const XFORM base{static_cast<FLOAT>(scale), 0, 0, static_cast<FLOAT>(scale), 0, 0};
    const int pw = static_cast<int>(client.right / scale) - 10;
    const int ph = static_cast<int>(static_cast<float>(pw) * pageHeight_ / pageWidth_);
    int y = -ph;

    const HFONT font =
        CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
    const HGDIOBJ oldFont = SelectObject(dc, font);
    const HPEN black = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
    const HPEN blue = CreatePen(PS_SOLID, 1, RGB(0, 0, 255));
    const HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    const HBRUSH yellow = CreateSolidBrush(RGB(255, 255, 0));
    const HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    const HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    SetBkMode(dc, TRANSPARENT);
    SetTextAlign(dc, TA_BASELINE | TA_LEFT);
    SetTextColor(dc, RGB(0, 0, 255));

    for (const formats::ShuffleRule& rule : rules_) {
        SetWorldTransform(dc, &base);
        if (rule.newPageBefore) {
            y += ph + 5;
            SelectObject(dc, black);
            SelectObject(dc, white);
            Rectangle(dc, 4, y - 1, 4 + pw + 2, y - 1 + ph + 2);
        }
        // translate, rotate, scale, and translate again, each applied before the ones before it
        XFORM step = translation(5, y + ph);
        ModifyWorldTransform(dc, &step, MWT_LEFTMULTIPLY);
        const double angle = rule.rotateAngle() * std::numbers::pi / 180;
        const auto c = static_cast<FLOAT>(std::cos(angle));
        const auto s = static_cast<FLOAT>(std::sin(angle));
        step = {c, s, -s, c, 0, 0};
        ModifyWorldTransform(dc, &step, MWT_LEFTMULTIPLY);
        const double ox = rule.offsetXPercent ? rule.offsetX * pw / 100 : rule.offsetX * pw / pageWidth_;
        const double oy = rule.offsetYPercent ? rule.offsetY * ph / 100 : rule.offsetY * ph / pageHeight_;
        step = {static_cast<FLOAT>(rule.scale), 0, 0, static_cast<FLOAT>(rule.scale), 0, 0};
        ModifyWorldTransform(dc, &step, MWT_LEFTMULTIPLY);
        step = translation(ox, -oy);
        ModifyWorldTransform(dc, &step, MWT_LEFTMULTIPLY);
        SelectObject(dc, blue);
        SelectObject(dc, yellow);
        Rectangle(dc, 0, -ph, pw, 0);
        const std::wstring page = fromUtf8(rule.pageString());
        TextOutW(dc, 5, -ph + 20, page.c_str(), static_cast<int>(page.size()));
    }

    const XFORM identity{1, 0, 0, 1, 0, 0};
    SetWorldTransform(dc, &identity);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldFont);
    DeleteObject(black);
    DeleteObject(blue);
    DeleteObject(white);
    DeleteObject(yellow);
    DeleteObject(font);
}

} // namespace pdftweak::gui
