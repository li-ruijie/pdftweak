#include "gui/form_layout.h"

#include "gui/window.h"

#include <commctrl.h>

#include <algorithm>
#include <cwctype>
#include <string>

namespace pdftweak::gui {
namespace {

struct FontDc {
    explicit FontDc(HWND window) : window_(window), dc_(GetDC(window)) {
        const auto font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
        old_ = SelectObject(dc_, font != nullptr ? font : GetStockObject(DEFAULT_GUI_FONT));
    }
    ~FontDc() {
        SelectObject(dc_, old_);
        ReleaseDC(window_, dc_);
    }
    FontDc(const FontDc&) = delete;
    FontDc& operator=(const FontDc&) = delete;
    HDC dc() const { return dc_; }

private:
    HWND window_;
    HDC dc_;
    HGDIOBJ old_;
};

SIZE textSize(HWND hwnd, const std::wstring& text) {
    FontDc font(hwnd);
    SIZE size{0, 0};
    if (text.empty()) {
        TEXTMETRICW metrics;
        GetTextMetricsW(font.dc(), &metrics);
        size.cy = metrics.tmHeight;
        return size;
    }
    RECT rect{0, 0, 0, 0};
    DrawTextW(font.dc(), text.c_str(), static_cast<int>(text.size()), &rect, DT_CALCRECT | DT_NOPREFIX);
    return {rect.right - rect.left, rect.bottom - rect.top};
}

std::wstring className(HWND hwnd) {
    wchar_t name[64];
    GetClassNameW(hwnd, name, 64);
    std::wstring result = name;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return result;
}

} // namespace

int dluToPixelsX(HWND window, int dlu) {
    FontDc font(window);
    SIZE size;
    GetTextExtentPoint32W(font.dc(), L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", 52, &size);
    const int average = (size.cx / 26 + 1) / 2;
    return MulDiv(dlu, average, 4);
}

int dluToPixelsY(HWND window, int dlu) {
    FontDc font(window);
    TEXTMETRICW metrics;
    GetTextMetricsW(font.dc(), &metrics);
    return MulDiv(dlu, metrics.tmHeight, 8);
}

const wchar_t* const kColumnsProperty = L"PdfTweakColumns";

Component control(HWND hwnd) {
    Component component;
    component.preferred = [hwnd]() -> SIZE {
        const std::wstring cls = className(hwnd);
        const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
        const SIZE text = textSize(hwnd, windowText(hwnd));
        const LONG line = textSize(hwnd, L"").cy;
        if (cls == L"button") {
            const LONG type = style & BS_TYPEMASK;
            if (type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_RADIOBUTTON ||
                type == BS_AUTORADIOBUTTON)
                return {text.cx + GetSystemMetricsForDpi(SM_CXMENUCHECK, GetDpiForWindow(hwnd)) +
                            scaled(hwnd, 8),
                        std::max(text.cy, line) + scaled(hwnd, 6)};
            return {text.cx + scaled(hwnd, 24), std::max(text.cy, line) + scaled(hwnd, 10)};
        }
        if (cls == L"static") {
            if ((style & SS_TYPEMASK) == SS_ETCHEDHORZ)
                return {0, scaled(hwnd, 2)};
            return {text.cx + scaled(hwnd, 2), std::max(text.cy, line) + scaled(hwnd, 6)};
        }
        if (cls == L"edit") {
            // a JTextField with columns asks for that many widths of "m", one without for its text
            const auto columns = reinterpret_cast<INT_PTR>(GetPropW(hwnd, kColumnsProperty));
            const int width = columns > 0 ? static_cast<int>(columns) * textSize(hwnd, L"m").cx : text.cx;
            return {width + scaled(hwnd, 8), line + scaled(hwnd, 8)};
        }
        if (cls == L"combobox") {
            int widest = 0;
            const LRESULT count = SendMessageW(hwnd, CB_GETCOUNT, 0, 0);
            for (LRESULT i = 0; i < count; ++i) {
                const LRESULT length = SendMessageW(hwnd, CB_GETLBTEXTLEN, static_cast<WPARAM>(i), 0);
                std::wstring item(static_cast<size_t>(length) + 1, L'\0');
                SendMessageW(hwnd, CB_GETLBTEXT, static_cast<WPARAM>(i),
                             reinterpret_cast<LPARAM>(item.data()));
                item.resize(static_cast<size_t>(length));
                widest = std::max<int>(widest, textSize(hwnd, item).cx);
            }
            // the window of a drop-down list is as high as its closed box
            RECT box;
            GetWindowRect(hwnd, &box);
            return {widest + GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(hwnd)) + scaled(hwnd, 12),
                    box.bottom - box.top};
        }
        if (cls == TRACKBAR_CLASSW)
            return {scaled(hwnd, 200), scaled(hwnd, 30)};
        return {scaled(hwnd, 100), line + scaled(hwnd, 8)};
    };
    component.place = [hwnd](const RECT& rect) {
        int height = rect.bottom - rect.top;
        if (className(hwnd) == L"combobox")
            height = scaled(hwnd, 300); // the closed box keeps its own height, the rest is the list
        SetWindowPos(hwnd, nullptr, rect.left, rect.top, rect.right - rect.left, height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    };
    return component;
}

Component sized(HWND hwnd, SIZE preferred) {
    Component component = control(hwnd);
    component.preferred = [hwnd, preferred]() -> SIZE {
        return {scaled(hwnd, preferred.cx), scaled(hwnd, preferred.cy)};
    };
    return component;
}

Component equalRow(std::vector<Component> items) {
    auto shared = std::make_shared<std::vector<Component>>(std::move(items));
    Component component;
    component.preferred = [shared]() -> SIZE {
        SIZE size{0, 0};
        for (const Component& item : *shared) {
            const SIZE one = item.preferred();
            size.cx = std::max(size.cx, one.cx);
            size.cy = std::max(size.cy, one.cy);
        }
        size.cx *= static_cast<LONG>(shared->size());
        return size;
    };
    component.place = [shared](const RECT& rect) {
        const int count = static_cast<int>(shared->size());
        for (int i = 0; i < count; ++i) {
            RECT cell = rect;
            cell.left = rect.left + MulDiv(rect.right - rect.left, i, count);
            cell.right = rect.left + MulDiv(rect.right - rect.left, i + 1, count);
            (*shared)[static_cast<size_t>(i)].place(cell);
        }
    };
    return component;
}

FormLayout::FormLayout(HWND window, std::wstring_view columns, std::wstring_view rows)
    : window_(window), columns_(parse(columns)), rows_(parse(rows)) {}

std::vector<FormLayout::Spec> FormLayout::parse(std::wstring_view text) {
    std::vector<Spec> specs;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L',', start);
        if (end == std::wstring_view::npos)
            end = text.size();
        std::wstring part(text.substr(start, end - start));
        part.erase(std::remove_if(part.begin(), part.end(), [](wchar_t c) { return std::iswspace(c); }),
                   part.end());
        if (!part.empty()) {
            Spec spec;
            std::vector<std::wstring> pieces;
            size_t from = 0;
            for (size_t colon = part.find(L':'); colon != std::wstring::npos;
                 colon = part.find(L':', from)) {
                pieces.push_back(part.substr(from, colon - from));
                from = colon + 1;
            }
            pieces.push_back(part.substr(from));
            // [alignment:]size[:resize]
            size_t sizeIndex =
                pieces.size() >= 2 && (pieces[0] == L"f" || pieces[0] == L"l" || pieces[0] == L"c" ||
                                       pieces[0] == L"r" || pieces[0] == L"t" || pieces[0] == L"b")
                    ? 1
                    : 0;
            const std::wstring& size = pieces[sizeIndex];
            if (size == L"p" || size == L"pref" || size == L"m" || size == L"min") {
                spec.preferred = true;
            } else if (size.size() > 3 && size.compare(size.size() - 3, 3, L"dlu") == 0) {
                spec.preferred = false;
                spec.dlu = std::stoi(size.substr(0, size.size() - 3));
            } else if (size.size() > 2 && size.compare(size.size() - 2, 2, L"px") == 0) {
                spec.preferred = false;
                spec.px = std::stoi(size.substr(0, size.size() - 2));
            }
            spec.grow = sizeIndex + 1 < pieces.size() && pieces[sizeIndex + 1].rfind(L"g", 0) == 0;
            specs.push_back(spec);
        }
        start = end + 1;
    }
    return specs;
}

void FormLayout::appendRow(std::wstring_view spec) {
    for (const Spec& s : parse(spec))
        rows_.push_back(s);
}

void FormLayout::add(Component component, int x, int y, int w, int h) {
    items_.push_back({std::move(component), x, y, w, h});
}

int FormLayout::fixedPixels(const Spec& spec, bool horizontal) const {
    if (spec.px != 0)
        return scaled(window_, spec.px);
    return horizontal ? dluToPixelsX(window_, spec.dlu) : dluToPixelsY(window_, spec.dlu);
}

std::vector<int> FormLayout::sizes(const std::vector<Spec>& specs, bool horizontal, int available) const {
    std::vector<int> result(specs.size(), 0);
    std::vector<SIZE> preferred;
    for (const Item& item : items_) {
        // each cell keeps a gap of two pixels on every side
        SIZE size = item.component.preferred();
        size.cx += 2 * scaled(window_, 2);
        size.cy += 2 * scaled(window_, 2);
        preferred.push_back(size);
    }
    for (size_t i = 0; i < specs.size(); ++i)
        if (!specs[i].preferred)
            result[i] = fixedPixels(specs[i], horizontal);
    // components in one column or row set its preferred size
    for (size_t k = 0; k < items_.size(); ++k) {
        const Item& item = items_[k];
        const int start = (horizontal ? item.x : item.y) - 1;
        const int span = horizontal ? item.w : item.h;
        const int want = horizontal ? preferred[k].cx : preferred[k].cy;
        if (span == 1 && start >= 0 && static_cast<size_t>(start) < specs.size() && specs[start].preferred)
            result[start] = std::max(result[start], want);
    }
    // a spanning component that does not fit gives the excess to the growing columns it spans, or to
    // its last column when none grows
    for (size_t k = 0; k < items_.size(); ++k) {
        const Item& item = items_[k];
        const int start = (horizontal ? item.x : item.y) - 1;
        const int span = horizontal ? item.w : item.h;
        if (span < 2 || start < 0)
            continue;
        const int end = std::min(start + span, static_cast<int>(specs.size()));
        int have = 0;
        std::vector<int> growing;
        for (int i = start; i < end; ++i) {
            have += result[static_cast<size_t>(i)];
            if (specs[static_cast<size_t>(i)].grow)
                growing.push_back(i);
        }
        const int want = horizontal ? preferred[k].cx : preferred[k].cy;
        if (want <= have)
            continue;
        if (growing.empty())
            growing.push_back(end - 1);
        const int excess = want - have;
        for (size_t g = 0; g < growing.size(); ++g)
            result[static_cast<size_t>(growing[g])] +=
                excess / static_cast<int>(growing.size()) +
                (g == 0 ? excess % static_cast<int>(growing.size()) : 0);
    }
    if (available >= 0) {
        int total = 0;
        for (const int size : result)
            total += size;
        std::vector<size_t> growing;
        for (size_t i = 0; i < specs.size(); ++i)
            if (specs[i].grow)
                growing.push_back(i);
        const int extra = available - total;
        if (extra > 0 && !growing.empty()) {
            for (size_t g = 0; g < growing.size(); ++g)
                result[growing[g]] +=
                    extra / static_cast<int>(growing.size()) +
                    (g + 1 == growing.size() ? extra % static_cast<int>(growing.size()) : 0);
        }
    }
    return result;
}

SIZE FormLayout::preferredSize() const {
    const std::vector<int> widths = sizes(columns_, true, -1);
    const std::vector<int> heights = sizes(rows_, false, -1);
    SIZE size{0, 0};
    for (const int w : widths)
        size.cx += w;
    for (const int h : heights)
        size.cy += h;
    return size;
}

void FormLayout::layout(const RECT& area) {
    const std::vector<int> widths = sizes(columns_, true, area.right - area.left);
    const std::vector<int> heights = sizes(rows_, false, area.bottom - area.top);
    std::vector<int> xs(widths.size() + 1, area.left);
    std::vector<int> ys(heights.size() + 1, area.top);
    for (size_t i = 0; i < widths.size(); ++i)
        xs[i + 1] = xs[i] + widths[i];
    for (size_t i = 0; i < heights.size(); ++i)
        ys[i + 1] = ys[i] + heights[i];
    const int inset = scaled(window_, 2);
    for (const Item& item : items_) {
        const size_t x0 = static_cast<size_t>(std::clamp(item.x - 1, 0, static_cast<int>(widths.size())));
        const size_t x1 =
            static_cast<size_t>(std::clamp(item.x - 1 + item.w, 0, static_cast<int>(widths.size())));
        const size_t y0 = static_cast<size_t>(std::clamp(item.y - 1, 0, static_cast<int>(heights.size())));
        const size_t y1 =
            static_cast<size_t>(std::clamp(item.y - 1 + item.h, 0, static_cast<int>(heights.size())));
        RECT cell{xs[x0] + inset, ys[y0] + inset, xs[x1] - inset, ys[y1] - inset};
        if (cell.right < cell.left)
            cell.right = cell.left;
        if (cell.bottom < cell.top)
            cell.bottom = cell.top;
        item.component.place(cell);
    }
}

Component FormLayout::asComponent() {
    Component component;
    component.preferred = [this]() { return preferredSize(); };
    component.place = [this](const RECT& rect) { layout(rect); };
    return component;
}

} // namespace pdftweak::gui
