#include "gui/table_editor.h"

#include "core/error.h"
#include "core/formats/integer_list.h"
#include "core/java_text.h"
#include "core/text.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <vsstyle.h>
#include <windowsx.h>

#include <algorithm>
#include <charconv>
#include <cmath>

#pragma comment(lib, "uxtheme.lib")

namespace pdftweak::gui {
namespace {

// Posted to finish the editing after the editor's own notification has returned.
const UINT kStopEditing = WM_APP + 10;
// Posted by the editor for Enter, Escape, and Tab.
const UINT kEditorKey = WM_APP + 11;

bool isTextLike(ColumnType type) {
    return type == ColumnType::Text || type == ColumnType::Integer || type == ColumnType::Number ||
           type == ColumnType::IntegerList;
}

std::wstring localeInfo(LCTYPE type, const wchar_t* fallback) {
    wchar_t buffer[8];
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, type, buffer, 8) > 0)
        return buffer;
    return fallback;
}

// JTable's DoubleRenderer: NumberFormat.getInstance().format, which is the pattern #,##0.### of the
// user's locale, rounding the exact binary value half to even.
std::wstring numberText(double value) {
    if (std::isnan(value))
        return L"NaN";
    if (std::isinf(value))
        return value < 0 ? L"-∞" : L"∞";
    char digits[400];
    const auto result =
        std::to_chars(digits, digits + sizeof(digits), std::fabs(value), std::chars_format::fixed, 40);
    std::string text(digits, result.ptr);
    const size_t point = text.find('.');
    std::string whole = text.substr(0, point);
    std::string fraction = text.substr(point + 1);
    std::string kept = fraction.substr(0, 3);
    const std::string rest = fraction.substr(3);
    // half to even at the third decimal
    bool up = false;
    if (rest[0] > '5') {
        up = true;
    } else if (rest[0] == '5') {
        const bool exactHalf = rest.find_first_not_of('0', 1) == std::string::npos;
        const char last = kept.back();
        up = !exactHalf || (last - '0') % 2 == 1;
    }
    std::string number = whole + kept;
    if (up) {
        int i = static_cast<int>(number.size()) - 1;
        while (i >= 0 && number[static_cast<size_t>(i)] == '9')
            number[static_cast<size_t>(i--)] = '0';
        if (i < 0)
            number.insert(number.begin(), '1');
        else
            ++number[static_cast<size_t>(i)];
    }
    whole = number.substr(0, number.size() - 3);
    kept = number.substr(number.size() - 3);
    while (!kept.empty() && kept.back() == '0')
        kept.pop_back();
    const std::wstring grouping = localeInfo(LOCALE_STHOUSAND, L",");
    std::wstring out;
    for (size_t i = 0; i < whole.size(); ++i) {
        if (i > 0 && (whole.size() - i) % 3 == 0)
            out += grouping;
        out += static_cast<wchar_t>(whole[i]);
    }
    if (!kept.empty())
        out += localeInfo(LOCALE_SDECIMAL, L".") + fromUtf8(kept);
    // DecimalFormat keeps the sign of a negative number that rounds to zero, and of -0.0
    if (std::signbit(value))
        out.insert(out.begin(), L'-');
    return out;
}

// The text an editor starts with: the value's toString, as DefaultCellEditor sets it.
std::wstring editText(const Value& value) {
    if (const auto* text = std::get_if<std::wstring>(&value))
        return *text;
    if (const auto* number = std::get_if<int>(&value))
        return std::to_wstring(*number);
    if (const auto* number = std::get_if<double>(&value))
        return fromUtf8(javaDouble(*number));
    if (const auto* list = std::get_if<std::vector<int>>(&value))
        return fromUtf8(formats::formatIntegerList(*list));
    return {};
}

} // namespace

TableEditor::TableEditor(std::vector<Column> columns, std::vector<Value> sample)
    : columns_(std::move(columns)), sample_(std::move(sample)) {
    setMargin(0);
}

Component TableEditor::component() {
    Component component;
    component.preferred = [this]() { return preferredSize(); };
    component.place = [this](const RECT& rect) {
        SetWindowPos(handle(), nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    };
    return component;
}

void TableEditor::build() {
    list_ = createChild(handle(), WC_LISTVIEWW, L"",
                        WS_TABSTOP | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS,
                        WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    // column 0 stays empty and zero wide, as the first column of a list view cannot be right-aligned
    LVCOLUMNW column{};
    column.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_FMT;
    column.pszText = const_cast<wchar_t*>(L"");
    ListView_InsertColumn(list_, 0, &column);
    for (size_t i = 0; i < columns_.size(); ++i) {
        const ColumnType type = columns_[i].type;
        column.fmt = type == ColumnType::Integer || type == ColumnType::Number ? LVCFMT_RIGHT
                     : type == ColumnType::Boolean                             ? LVCFMT_CENTER
                                                                               : LVCFMT_LEFT;
        column.pszText = const_cast<wchar_t*>(columns_[i].caption.c_str());
        column.cx = scaled(handle(), columns_[i].preferredWidth);
        ListView_InsertColumn(list_, static_cast<int>(i) + 1, &column);
    }
    SetWindowSubclass(list_, listProc, 1, reinterpret_cast<DWORD_PTR>(this));
    onNotify(list_, [this](NMHDR* header) { return notify(header); });
    onNotify(ListView_GetHeader(list_), [this](NMHDR* header) { return notify(header); });

    add_ = button(L"Add", [this] {
        if (editor_ != nullptr && !stopEditing())
            return;
        rows_.push_back(sample_);
        refresh();
    });
    up_ = button(L"Up", [this] {
        const std::vector<int> selected = selectedRows();
        if (selected.empty() || selected[0] == 0)
            return;
        if (editor_ != nullptr && !stopEditing())
            return;
        const int row = selected[0];
        std::swap(rows_[static_cast<size_t>(row)], rows_[static_cast<size_t>(row) - 1]);
        selectRows(row - 1, row - 1);
        refresh();
    });
    down_ = button(L"Down", [this] {
        const std::vector<int> selected = selectedRows();
        if (selected.empty() || selected[0] == rowCount() - 1)
            return;
        if (editor_ != nullptr && !stopEditing())
            return;
        const int row = selected[0];
        std::swap(rows_[static_cast<size_t>(row)], rows_[static_cast<size_t>(row) + 1]);
        selectRows(row + 1, row + 1);
        refresh();
    });
    delete_ = button(L"Delete", [this] {
        const std::vector<int> selected = selectedRows();
        for (auto it = selected.rbegin(); it != selected.rend(); ++it) {
            if (editor_ != nullptr && !stopEditing())
                return;
            rows_.erase(rows_.begin() + *it);
            ListView_SetItemState(list_, -1, 0, LVIS_SELECTED);
            refresh();
        }
    });

    Component table;
    table.preferred = [this]() -> SIZE {
        return {scaled(list_, preferred_.cx), scaled(list_, preferred_.cy)};
    };
    table.place = [this](const RECT& rect) {
        SetWindowPos(list_, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        fitColumns();
        placeEditor();
    };
    FormLayout& layout = newLayout(L"f:p:g,f:p:g,f:p:g,f:p:g", L"f:p:g, f:p");
    layout.add(table, 1, 1, 4);
    layout.add(add_, 1, 2);
    layout.add(up_, 2, 2);
    layout.add(down_, 3, 2);
    layout.add(delete_, 4, 2);
    setLayout(layout.asComponent());
}

void TableEditor::setEnabled(bool enabled) {
    for (HWND control : {add_, up_, down_, delete_, list_})
        enable(control, enabled);
}

void TableEditor::clear() {
    if (editor_ != nullptr && !stopEditing())
        return;
    rows_.clear();
    ListView_SetItemState(list_, -1, 0, LVIS_SELECTED);
    refresh();
}

void TableEditor::addRow(std::vector<Value> values) {
    if (editor_ != nullptr && !stopEditing())
        return;
    values.resize(columns_.size());
    rows_.push_back(std::move(values));
    refresh();
}

void TableEditor::checkRun(const std::string& name) {
    if (editor_ != nullptr && !stopEditing())
        throw Error(Error::Kind::Io, "Please finish editing the " + name + " table!");
}

std::vector<int> TableEditor::selectedRows() const {
    std::vector<int> rows;
    for (int i = ListView_GetNextItem(list_, -1, LVNI_SELECTED); i != -1;
         i = ListView_GetNextItem(list_, i, LVNI_SELECTED))
        rows.push_back(i);
    return rows;
}

void TableEditor::selectRows(int first, int last) {
    ListView_SetItemState(list_, -1, 0, LVIS_SELECTED);
    for (int i = first; i <= last; ++i)
        ListView_SetItemState(list_, i, LVIS_SELECTED, LVIS_SELECTED);
    ListView_SetItemState(list_, first, LVIS_FOCUSED, LVIS_FOCUSED);
    ListView_EnsureVisible(list_, first, FALSE);
}

std::wstring TableEditor::cellText(int row, int column) const {
    const Value& value = rows_[static_cast<size_t>(row)][static_cast<size_t>(column)];
    if (const auto* number = std::get_if<double>(&value))
        return numberText(*number);
    if (const auto* file = std::get_if<std::shared_ptr<pdf::InputFile>>(&value))
        return *file ? (*file)->file().filename().wstring() : std::wstring();
    if (const auto* flag = std::get_if<bool>(&value))
        return *flag ? L"true" : L"false";
    return editText(value);
}

void TableEditor::refresh() {
    ListView_SetItemCountEx(list_, rows_.size(), LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    InvalidateRect(list_, nullptr, TRUE);
    placeEditor();
}

void TableEditor::fitColumns() {
    // JTable's AUTO_RESIZE_SUBSEQUENT_COLUMNS: the columns share the width of the table in the
    // proportions they have
    RECT client;
    GetClientRect(list_, &client);
    const int available = client.right - client.left;
    if (available <= 0)
        return;
    std::vector<int> widths;
    int total = 0;
    for (size_t i = 0; i < columns_.size(); ++i) {
        widths.push_back(std::max(1, ListView_GetColumnWidth(list_, static_cast<int>(i) + 1)));
        total += widths.back();
    }
    int used = 0;
    for (size_t i = 0; i < widths.size(); ++i) {
        const int width =
            i + 1 == widths.size() ? available - used : MulDiv(widths[i], available, std::max(1, total));
        ListView_SetColumnWidth(list_, static_cast<int>(i) + 1, width);
        used += width;
    }
}

void TableEditor::placeEditor() {
    if (editor_ == nullptr)
        return;
    RECT cell;
    ListView_GetSubItemRect(list_, editRow_, editColumn_ + 1, LVIR_BOUNDS, &cell);
    RECT visible;
    GetClientRect(list_, &visible);
    RECT header;
    GetWindowRect(ListView_GetHeader(list_), &header);
    visible.top += header.bottom - header.top;
    RECT shown;
    if (!IntersectRect(&shown, &cell, &visible) || !EqualRect(&shown, &cell)) {
        ShowWindow(editor_, SW_HIDE);
        return;
    }
    MapWindowPoints(list_, handle(), reinterpret_cast<POINT*>(&cell), 2);
    const bool combo = columns_[static_cast<size_t>(editColumn_)].type == ColumnType::Choice ||
                       columns_[static_cast<size_t>(editColumn_)].type == ColumnType::InputFile;
    const int height = combo ? scaled(list_, 300) : cell.bottom - cell.top;
    SetWindowPos(editor_, HWND_TOP, cell.left, cell.top, cell.right - cell.left, height, SWP_SHOWWINDOW);
}

void TableEditor::setCurrent(int row, int column) {
    currentColumn_ = column;
    if (row >= 0) {
        ListView_SetItemState(list_, row, LVIS_FOCUSED, LVIS_FOCUSED);
        ListView_RedrawItems(list_, row, row);
    }
}

void TableEditor::toggle(int row, int column) {
    if (!isEnabled(list_))
        return;
    if (editor_ != nullptr && !stopEditing())
        return;
    Value& value = rows_[static_cast<size_t>(row)][static_cast<size_t>(column)];
    const bool* flag = std::get_if<bool>(&value);
    value = !(flag != nullptr && *flag);
    if (listener_)
        listener_(row, column);
    refresh();
}

void TableEditor::moveSelection(int offset) {
    if (rows_.empty())
        return;
    int row = ListView_GetNextItem(list_, -1, LVNI_FOCUSED);
    if (row < 0)
        row = 0;
    row = (row + offset + rowCount()) % rowCount();
    selectRows(row, row);
}

HWND TableEditor::startEditing(int row, int column) {
    if (editor_ != nullptr) {
        if (row == editRow_ && column == editColumn_)
            return editor_;
        if (!stopEditing())
            return nullptr;
    }
    if (!isEnabled(list_) || row < 0 || row >= rowCount() || column < 0 ||
        column >= static_cast<int>(columns_.size()))
        return nullptr;
    const Column& spec = columns_[static_cast<size_t>(column)];
    if (spec.type == ColumnType::Boolean) {
        toggle(row, column);
        return nullptr;
    }
    if (selectedRows() != std::vector<int>{row})
        selectRows(row, row);
    currentColumn_ = column;
    ListView_EnsureVisible(list_, row, FALSE);
    const Value& value = rows_[static_cast<size_t>(row)][static_cast<size_t>(column)];
    if (spec.type == ColumnType::Choice || spec.type == ColumnType::InputFile) {
        editor_ = createChild(handle(), WC_COMBOBOXW, L"", WS_VSCROLL | CBS_DROPDOWNLIST);
        int selected = -1;
        if (spec.type == ColumnType::Choice) {
            setItems(editor_, spec.choices);
            const auto* text = std::get_if<std::wstring>(&value);
            for (size_t i = 0; i < spec.choices.size(); ++i)
                if (text != nullptr && spec.choices[i] == *text)
                    selected = static_cast<int>(i);
        } else {
            editorFiles_ = fileChoices_ ? fileChoices_() : std::vector<std::shared_ptr<pdf::InputFile>>{};
            std::vector<std::wstring> names;
            const auto* file = std::get_if<std::shared_ptr<pdf::InputFile>>(&value);
            for (size_t i = 0; i < editorFiles_.size(); ++i) {
                names.push_back(editorFiles_[i]->file().filename().wstring());
                if (file != nullptr && *file == editorFiles_[i])
                    selected = static_cast<int>(i);
            }
            setItems(editor_, names);
        }
        selectIndex(editor_, selected);
    } else {
        editor_ = createChild(handle(), WC_EDITW, editText(value), ES_AUTOHSCROLL | WS_BORDER);
        const auto length = static_cast<WPARAM>(GetWindowTextLengthW(editor_));
        SendMessageW(editor_, EM_SETSEL, length, static_cast<LPARAM>(length));
    }
    editRow_ = row;
    editColumn_ = column;
    editorInvalid_ = false;
    SetWindowSubclass(editor_, editorProc, 2, reinterpret_cast<DWORD_PTR>(this));
    const HWND editor = editor_;
    onCommand(editor_, [this, editor](int code) {
        if (code == EN_KILLFOCUS || code == CBN_KILLFOCUS || code == CBN_SELENDOK)
            PostMessageW(handle(), kStopEditing, code == CBN_SELENDOK, reinterpret_cast<LPARAM>(editor));
    });
    placeEditor();
    SetFocus(editor_);
    if (spec.type == ColumnType::Choice || spec.type == ColumnType::InputFile)
        SendMessageW(editor_, CB_SHOWDROPDOWN, TRUE, 0);
    return editor_;
}

bool TableEditor::stopEditing() {
    if (editor_ == nullptr)
        return true;
    const Column& spec = columns_[static_cast<size_t>(editColumn_)];
    Value value;
    try {
        if (spec.type == ColumnType::Choice) {
            const int index = selectedIndex(editor_);
            if (index >= 0)
                value = spec.choices[static_cast<size_t>(index)];
        } else if (spec.type == ColumnType::InputFile) {
            const int index = selectedIndex(editor_);
            if (index >= 0)
                value = editorFiles_[static_cast<size_t>(index)];
        } else {
            // JTable's GenericEditor: an empty field is null except in a String column, and anything
            // the column's constructor refuses keeps the editor open with a red border
            const std::wstring text = windowText(editor_);
            if (spec.type == ColumnType::Text)
                value = text;
            else if (text.empty())
                value = std::monostate{};
            else if (spec.type == ColumnType::Integer)
                value = javaParseInt(toUtf8(text));
            else if (spec.type == ColumnType::Number)
                value = javaParseDouble(toUtf8(text));
            else
                value = formats::parseIntegerList(toUtf8(text));
        }
    } catch (const Error&) {
        editorInvalid_ = true;
        InvalidateRect(editor_, nullptr, TRUE);
        return false;
    }
    const int row = editRow_;
    const int column = editColumn_;
    rows_[static_cast<size_t>(row)][static_cast<size_t>(column)] = std::move(value);
    cancelEditing();
    if (listener_)
        listener_(row, column);
    refresh();
    return true;
}

void TableEditor::cancelEditing() {
    if (editor_ == nullptr)
        return;
    const HWND editor = editor_;
    editor_ = nullptr;
    editRow_ = -1;
    editColumn_ = -1;
    editorFiles_.clear();
    forget(editor);
    const HWND focus = GetFocus();
    if (focus == editor || IsChild(editor, focus))
        SetFocus(list_);
    DestroyWindow(editor);
}

LRESULT TableEditor::message(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case kStopEditing: {
        // after the focus left the editor, or after a choice in its list
        const auto editor = reinterpret_cast<HWND>(lParam);
        if (editor != editor_)
            return 0;
        const HWND focus = GetFocus();
        if (wParam != 0 || (focus != editor && !IsChild(editor, focus)))
            stopEditing();
        return 0;
    }
    case kEditorKey:
        if (reinterpret_cast<HWND>(lParam) != editor_)
            return 0;
        if (wParam == VK_ESCAPE) {
            cancelEditing();
        } else if (stopEditing()) {
            SetFocus(list_);
            if (wParam == VK_RETURN)
                moveSelection(1);
        }
        return 0;
    case WM_CTLCOLOREDIT:
        if (reinterpret_cast<HWND>(lParam) == editor_ && editorInvalid_) {
            static const HBRUSH invalid = CreateSolidBrush(RGB(255, 220, 220));
            SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 220, 220));
            SetTextColor(reinterpret_cast<HDC>(wParam), RGB(192, 0, 0));
            return reinterpret_cast<LRESULT>(invalid);
        }
        break;
    }
    return Page::message(msg, wParam, lParam);
}

LRESULT TableEditor::notify(NMHDR* header) {
    switch (header->code) {
    case LVN_GETDISPINFOW: {
        LVITEMW& item = reinterpret_cast<NMLVDISPINFOW*>(header)->item;
        if ((item.mask & LVIF_TEXT) != 0 && item.cchTextMax > 0) {
            const int column = item.iSubItem - 1;
            display_.clear();
            if (column >= 0 && item.iItem < rowCount() &&
                columns_[static_cast<size_t>(column)].type != ColumnType::Boolean)
                display_ = cellText(item.iItem, column);
            wcsncpy_s(item.pszText, static_cast<size_t>(item.cchTextMax), display_.c_str(), _TRUNCATE);
        }
        return 0;
    }
    case NM_CUSTOMDRAW:
        if (header->hwndFrom == list_)
            return customDraw(reinterpret_cast<NMLVCUSTOMDRAW*>(header));
        return CDRF_DODEFAULT;
    case NM_CLICK:
    case NM_DBLCLK: {
        LVHITTESTINFO hit{};
        hit.pt = reinterpret_cast<NMITEMACTIVATE*>(header)->ptAction;
        ListView_SubItemHitTest(list_, &hit);
        const int column = hit.iSubItem - 1;
        if (hit.iItem < 0 || column < 0)
            return 0;
        setCurrent(hit.iItem, column);
        const ColumnType type = columns_[static_cast<size_t>(column)].type;
        if (header->code == NM_CLICK &&
            (type == ColumnType::Boolean || type == ColumnType::Choice || type == ColumnType::InputFile))
            startEditing(hit.iItem, column);
        else if (header->code == NM_DBLCLK && isTextLike(type))
            startEditing(hit.iItem, column);
        return 0;
    }
    case LVN_ENDSCROLL: placeEditor(); return 0;
    case HDN_BEGINTRACKW:
    case HDN_DIVIDERDBLCLICKW:
        // the empty column 0 keeps its zero width
        return reinterpret_cast<NMHEADERW*>(header)->iItem == 0 ? TRUE : FALSE;
    case HDN_ITEMCHANGEDW: placeEditor(); return 0;
    }
    return 0;
}

LRESULT TableEditor::customDraw(NMLVCUSTOMDRAW* draw) {
    switch (draw->nmcd.dwDrawStage) {
    case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: return CDRF_NOTIFYSUBITEMDRAW | CDRF_NOTIFYPOSTPAINT;
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
        const int column = draw->iSubItem - 1;
        const auto row = static_cast<int>(draw->nmcd.dwItemSpec);
        if (column < 0 || row >= rowCount() ||
            columns_[static_cast<size_t>(column)].type != ColumnType::Boolean)
            return CDRF_DODEFAULT;
        RECT cell;
        ListView_GetSubItemRect(list_, row, draw->iSubItem, LVIR_BOUNDS, &cell);
        cell.right -= 1;
        cell.bottom -= 1;
        const bool selected = ListView_GetItemState(list_, row, LVIS_SELECTED) != 0;
        const bool enabled = isEnabled(list_);
        // the colours the list view gives the other cells of the row
        const int background = !enabled              ? COLOR_BTNFACE
                               : !selected           ? COLOR_WINDOW
                               : GetFocus() == list_ ? COLOR_HIGHLIGHT
                                                     : COLOR_BTNFACE;
        FillRect(draw->nmcd.hdc, &cell, GetSysColorBrush(background));
        const Value& value = rows_[static_cast<size_t>(row)][static_cast<size_t>(column)];
        const bool checked = std::holds_alternative<bool>(value) && std::get<bool>(value);
        const HTHEME theme = OpenThemeData(list_, L"BUTTON");
        SIZE box{scaled(list_, 13), scaled(list_, 13)};
        if (theme != nullptr)
            GetThemePartSize(theme, draw->nmcd.hdc, BP_CHECKBOX, CBS_UNCHECKEDNORMAL, nullptr, TS_TRUE,
                             &box);
        RECT mark;
        mark.left = cell.left + (cell.right - cell.left - box.cx) / 2;
        mark.top = cell.top + (cell.bottom - cell.top - box.cy) / 2;
        mark.right = mark.left + box.cx;
        mark.bottom = mark.top + box.cy;
        if (theme != nullptr) {
            const int state = checked ? (enabled ? CBS_CHECKEDNORMAL : CBS_CHECKEDDISABLED)
                                      : (enabled ? CBS_UNCHECKEDNORMAL : CBS_UNCHECKEDDISABLED);
            DrawThemeBackground(theme, draw->nmcd.hdc, BP_CHECKBOX, state, &mark, nullptr);
            CloseThemeData(theme);
        } else {
            DrawFrameControl(draw->nmcd.hdc, &mark, DFC_BUTTON,
                             DFCS_BUTTONCHECK | (checked ? DFCS_CHECKED : 0) |
                                 (enabled ? 0 : DFCS_INACTIVE));
        }
        return CDRF_SKIPDEFAULT;
    }
    case CDDS_ITEMPOSTPAINT: {
        // the current cell of the focused row, as JTable outlines its focused cell
        const auto row = static_cast<int>(draw->nmcd.dwItemSpec);
        if (GetFocus() == list_ && ListView_GetNextItem(list_, -1, LVNI_FOCUSED) == row) {
            RECT cell;
            ListView_GetSubItemRect(list_, row, currentColumn_ + 1, LVIR_BOUNDS, &cell);
            InflateRect(&cell, -1, -1);
            DrawFocusRect(draw->nmcd.hdc, &cell);
        }
        return CDRF_DODEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

LRESULT CALLBACK TableEditor::listProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR,
                                       DWORD_PTR data) {
    auto* table = reinterpret_cast<TableEditor*>(data);
    switch (msg) {
    case WM_GETDLGCODE:
        // Enter moves to the next row, as in JTable, instead of pressing the default button
        if (lParam != 0 && reinterpret_cast<MSG*>(lParam)->message == WM_KEYDOWN &&
            reinterpret_cast<MSG*>(lParam)->wParam == VK_RETURN)
            return DLGC_WANTALLKEYS;
        break;
    case WM_KEYDOWN: {
        const int row = ListView_GetNextItem(hwnd, -1, LVNI_FOCUSED);
        const int columns = static_cast<int>(table->columns_.size());
        switch (wParam) {
        case VK_F2:
            if (row >= 0)
                table->startEditing(row, table->currentColumn_);
            return 0;
        case VK_LEFT:
        case VK_RIGHT:
            table->setCurrent(
                row, std::clamp(table->currentColumn_ + (wParam == VK_LEFT ? -1 : 1), 0, columns - 1));
            return 0;
        case VK_SPACE:
            if (row >= 0 &&
                table->columns_[static_cast<size_t>(table->currentColumn_)].type == ColumnType::Boolean)
                table->toggle(row, table->currentColumn_);
            return 0;
        case VK_RETURN: table->moveSelection(1); return 0;
        }
        break;
    }
    case WM_CHAR: {
        const int row = ListView_GetNextItem(hwnd, -1, LVNI_FOCUSED);
        if (wParam < 32 || row < 0)
            break;
        const ColumnType type = table->columns_[static_cast<size_t>(table->currentColumn_)].type;
        if (type == ColumnType::Boolean)
            return 0;
        // typing starts editing the current cell, and the key goes to the editor, as in JTable
        const HWND editor = table->startEditing(row, table->currentColumn_);
        if (editor != nullptr && isTextLike(type))
            SendMessageW(editor, WM_CHAR, wParam, lParam);
        return 0;
    }
    case WM_NCDESTROY: RemoveWindowSubclass(hwnd, listProc, 1); break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK TableEditor::editorProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR,
                                         DWORD_PTR data) {
    auto* table = reinterpret_cast<TableEditor*>(data);
    switch (msg) {
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DefSubclassProc(hwnd, msg, wParam, lParam);
    case WM_KEYDOWN:
        if (wParam == VK_RETURN || wParam == VK_ESCAPE || wParam == VK_TAB) {
            // a drop-down list that is open takes Enter and Escape itself
            if (wParam != VK_TAB && SendMessageW(hwnd, CB_GETDROPPEDSTATE, 0, 0) != 0)
                break;
            PostMessageW(table->handle(), kEditorKey, wParam, reinterpret_cast<LPARAM>(hwnd));
            return 0;
        }
        break;
    case WM_CHAR:
        if (wParam == L'\r' || wParam == 27 || wParam == L'\t')
            return 0;
        break;
    case WM_NCDESTROY: RemoveWindowSubclass(hwnd, editorProc, 2); break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

} // namespace pdftweak::gui
