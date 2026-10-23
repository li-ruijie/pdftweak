// The gui group: the main window, created hidden and driven through window messages, as the gui_
// tests of the Java project drove the Swing components.

#include "runner.h"

#include "core/error.h"
#include "core/text.h"
#include "gui/main_window.h"
#include "gui/table_editor.h"
#include "gui/tabs.h"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>

#include <string>
#include <vector>

using namespace pdftweak;
using namespace pdftweak::test;

namespace {

// The common controls and COM, as pdftweak.exe starts them before it opens the window.
const struct GuiSetup {
    GuiSetup() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES |
                                                            ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES |
                                                            ICC_PROGRESS_CLASS};
        InitCommonControlsEx(&controls);
    }
} guiSetup;

HWND findChild(HWND parent, const wchar_t* className) {
    struct Search {
        const wchar_t* className;
        HWND found;
    } search{className, nullptr};
    EnumChildWindows(
        parent,
        [](HWND child, LPARAM param) -> BOOL {
            auto* s = reinterpret_cast<Search*>(param);
            wchar_t name[64];
            GetClassNameW(child, name, 64);
            if (_wcsicmp(name, s->className) == 0) {
                s->found = child;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.found;
}

std::string tabText(HWND tabControl, int index) {
    wchar_t text[64] = {};
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = text;
    item.cchTextMax = 64;
    TabCtrl_GetItem(tabControl, index, &item);
    return toUtf8(text);
}

} // namespace

// OperationTests.gui_mainFormBuildsEveryTab: the window has the eleven tabs, in Java's order.
TEST(gui_mainFormBuildsEveryTab) {
    gui::MainWindow window(false);
    const HWND tabControl = findChild(window.handle(), WC_TABCONTROLW);
    CHECK(tabControl != nullptr);
    const std::vector<std::string> names{"Input",         "Page Size",    "Watermark",   "Shuffle/N-up",
                                         "Page Numbers",  "Bookmarks",    "Attachments", "Interaction",
                                         "Document Info", "Encrypt/Sign", "Output"};
    CHECK_EQUAL(TabCtrl_GetItemCount(tabControl), static_cast<int>(names.size()));
    for (size_t i = 0; i < names.size(); ++i)
        CHECK_EQUAL(tabText(tabControl, static_cast<int>(i)), names[i]);
    CHECK_EQUAL(window.tabCount(), 11);
}

namespace {

// AbstractButton.doClick: a check box changes its state, then the button reports the click, unless it
// is disabled.
void click(HWND button) {
    if (!IsWindowEnabled(button))
        return;
    if ((GetWindowLongW(button, GWL_STYLE) & BS_TYPEMASK) == BS_AUTOCHECKBOX)
        gui::setChecked(button, !gui::isChecked(button));
    SendMessageW(GetParent(button), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(button), BN_CLICKED),
                 reinterpret_cast<LPARAM>(button));
}

// A hidden frame that holds one table.
struct TableHost final : gui::Page {
    TableHost(std::vector<gui::Column> columns, std::vector<gui::Value> sample)
        : table(std::move(columns), std::move(sample)) {}
    gui::TableEditor table;

private:
    void build() override {
        gui::FormLayout& layout = newLayout(L"f:p:g", L"f:p:g");
        layout.add(childPage(table), 1, 1);
        setLayout(layout.asComponent());
    }
};

std::vector<int> integers(gui::TableEditor& table, int column) {
    std::vector<int> values;
    for (int i = 0; i < table.rowCount(); ++i)
        values.push_back(std::get<int>(table.row(i)[static_cast<size_t>(column)]));
    return values;
}

} // namespace

// TableComponent: Add appends the sample row, Up and Down move the first selected row, and Delete
// removes every selected row.
TEST(gui_tableAddsMovesAndDeletesRows) {
    TableHost host({{L"A", gui::ColumnType::Integer}, {L"B", gui::ColumnType::Text}},
                   {7, std::wstring(L"x")});
    gui::Frame frame(nullptr, L"Table", host, true);
    gui::TableEditor& table = host.table;
    for (int i = 0; i < 3; ++i)
        click(table.addButton());
    CHECK_EQUAL(table.rowCount(), 3);
    CHECK(table.row(2)[1] == gui::Value(std::wstring(L"x")));
    for (int i = 0; i < 3; ++i)
        table.row(i)[0] = i;
    table.selectRows(2, 2);
    click(table.upButton());
    CHECK(integers(table, 0) == std::vector<int>({0, 2, 1}));
    CHECK(table.selectedRows() == std::vector<int>{1});
    click(table.downButton());
    CHECK(integers(table, 0) == std::vector<int>({0, 1, 2}));
    table.selectRows(0, 0);
    click(table.upButton());
    CHECK(integers(table, 0) == std::vector<int>({0, 1, 2}));
    table.selectRows(0, 1);
    click(table.deleteButton());
    CHECK(integers(table, 0) == std::vector<int>{2});
    table.setEnabled(false);
    CHECK(!IsWindowEnabled(table.addButton()) && !IsWindowEnabled(table.listView()));
    CHECK(table.startEditing(0, 0) == nullptr);
}

// JTable's GenericEditor: an empty field gives null, and a value the column's class does not parse
// keeps the editor open, so that checkRun asks to finish the editing.
TEST(gui_tableEditorParsesAsGenericEditor) {
    TableHost host({{L"Integer", gui::ColumnType::Integer},
                    {L"Number", gui::ColumnType::Number},
                    {L"List", gui::ColumnType::IntegerList},
                    {L"Text", gui::ColumnType::Text}},
                   {1, 1.0, std::vector<int>{0}, std::wstring(L"")});
    gui::Frame frame(nullptr, L"Table", host, true);
    gui::TableEditor& table = host.table;
    std::vector<std::pair<int, int>> changes;
    table.setRowListener([&](int row, int column) { changes.emplace_back(row, column); });
    click(table.addButton());

    HWND editor = table.startEditing(0, 0);
    CHECK(editor != nullptr);
    CHECK_EQUAL(toUtf8(gui::windowText(editor)), std::string("1"));
    gui::setWindowText(editor, L"x");
    CHECK(!table.stopEditing());
    try {
        table.checkRun("input file");
        CHECK(false);
    } catch (const Error& error) {
        CHECK_EQUAL(std::string(error.what()), std::string("Please finish editing the input file table!"));
    }
    click(table.addButton());
    CHECK_EQUAL(table.rowCount(), 1);
    CHECK(changes.empty());
    gui::setWindowText(editor, L"");
    CHECK(table.stopEditing());
    CHECK(std::holds_alternative<std::monostate>(table.row(0)[0]));
    CHECK(changes == (std::vector<std::pair<int, int>>{{0, 0}}));

    editor = table.startEditing(0, 1);
    CHECK_EQUAL(toUtf8(gui::windowText(editor)), std::string("1.0"));
    gui::setWindowText(editor, L" 1e3 ");
    CHECK(table.stopEditing());
    CHECK(table.row(0)[1] == gui::Value(1000.0));

    editor = table.startEditing(0, 2);
    gui::setWindowText(editor, L"1,-2");
    CHECK(!table.stopEditing());
    gui::setWindowText(editor, L"1,2");
    CHECK(table.stopEditing());
    CHECK(table.row(0)[2] == gui::Value(std::vector<int>{1, 2}));

    editor = table.startEditing(0, 3);
    gui::setWindowText(editor, L"");
    table.checkRun("input file");
    CHECK(table.row(0)[3] == gui::Value(std::wstring()));
    CHECK(table.editor() == nullptr);
}

// A check box cell changes with one click, and a choice cell takes the item picked in its list.
TEST(gui_tableTogglesCheckBoxesAndPicksChoices) {
    TableHost host({{L"Flag", gui::ColumnType::Boolean},
                    {L"Style", gui::ColumnType::Choice, {L"1, 2, 3", L"I, II, III", L"Empty"}}},
                   {true, std::wstring(L"1, 2, 3")});
    gui::Frame frame(nullptr, L"Table", host, true);
    gui::TableEditor& table = host.table;
    int changes = 0;
    table.setRowListener([&](int, int) { ++changes; });
    click(table.addButton());
    CHECK(table.startEditing(0, 0) == nullptr);
    CHECK(table.row(0)[0] == gui::Value(false));
    table.startEditing(0, 0);
    CHECK(table.row(0)[0] == gui::Value(true));
    const HWND combo = table.startEditing(0, 1);
    CHECK(combo != nullptr);
    CHECK_EQUAL(gui::selectedIndex(combo), 0);
    gui::selectIndex(combo, 2);
    CHECK(table.stopEditing());
    CHECK(table.row(0)[1] == gui::Value(std::wstring(L"Empty")));
    CHECK_EQUAL(changes, 3);
}

// JTable shows a Double as NumberFormat.getInstance() does: up to three decimals, rounded half to
// even from the exact binary value, with the separators of the locale.
TEST(gui_tableShowsNumbersAsNumberFormat) {
    TableHost host({{L"Number", gui::ColumnType::Number}}, {0.0});
    gui::Frame frame(nullptr, L"Table", host, true);
    gui::TableEditor& table = host.table;
    wchar_t point[8], group[8];
    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SDECIMAL, point, 8);
    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STHOUSAND, group, 8);
    const std::wstring p(point), g(group);
    const std::vector<std::pair<double, std::wstring>> cases{{1.0, L"1"},
                                                             {0.3535, L"0" + p + L"353"},
                                                             {0.0625, L"0" + p + L"062"},
                                                             {0.0005, L"0" + p + L"001"},
                                                             {1234.5, L"1" + g + L"234" + p + L"5"},
                                                             {-0.0001, L"-0"},
                                                             {0.9999, L"1"}};
    for (const auto& [value, text] : cases) {
        table.clear();
        table.addRow({value});
        CHECK_EQUAL(toUtf8(table.cellText(0, 0)), toUtf8(text));
    }
}
