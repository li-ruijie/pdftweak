#pragma once

// A port of the part of JGoodies Forms that jPDF Tweak's tabs use: FormLayout with column and row
// specifications such as "f:p, f:p:g, 80dlu, 10dlu" and CellConstraints xy, xyw, and xywh. Every
// alignment is fill. A size is p (the preferred size of the cell's components), Ndlu, or Npx, and g
// makes the column or row take an equal share of the extra space.

#include <windows.h>

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace pdftweak::gui {

// Something a layout places: a control, or a nested panel or custom control that measures and places
// itself.
struct Component {
    std::function<SIZE()> preferred;
    std::function<void(const RECT&)> place;
};

// A control with the preferred size Swing would give it, measured from its class, style, and text.
Component control(HWND hwnd);

// A control with a fixed preferred size in pixels at 96 DPI.
Component sized(HWND hwnd, SIZE preferred);

// The window property that holds the column count of a text field, which sets its preferred width.
extern const wchar_t* const kColumnsProperty;

// Components in one row of equal widths, as a GridLayout(1, n).
Component equalRow(std::vector<Component> items);

class FormLayout {
public:
    FormLayout(HWND window, std::wstring_view columns, std::wstring_view rows);

    // FormLayout.appendRow(RowSpec.decode(spec))
    void appendRow(std::wstring_view spec);

    // add(component, cc.xywh(x, y, w, h)), with 1-based columns and rows.
    void add(Component component, int x, int y, int w = 1, int h = 1);
    void add(HWND hwnd, int x, int y, int w = 1, int h = 1) { add(control(hwnd), x, y, w, h); }

    SIZE preferredSize() const;

    // Places every component in the rectangle.
    void layout(const RECT& area);

    // This layout as a component of an enclosing one.
    Component asComponent();

private:
    struct Spec {
        bool preferred = true;
        int dlu = 0;
        int px = 0;
        bool grow = false;
    };
    struct Item {
        Component component;
        int x, y, w, h;
    };
    static std::vector<Spec> parse(std::wstring_view text);
    std::vector<int> sizes(const std::vector<Spec>& specs, bool horizontal, int available) const;
    int fixedPixels(const Spec& spec, bool horizontal) const;

    HWND window_;
    std::vector<Spec> columns_;
    std::vector<Spec> rows_;
    std::vector<Item> items_;
};

// Dialog units of the window's font, as Swing's dlu: horizontal units are a quarter of the average
// character width and vertical ones an eighth of the font height.
int dluToPixelsX(HWND window, int dlu);
int dluToPixelsY(HWND window, int dlu);

} // namespace pdftweak::gui
