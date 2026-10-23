// The gui group: the main window, created hidden and driven through window messages, as the gui_
// tests of the Java project drove the Swing components.

#include "runner.h"

#include "cli_run.h"
#include "describe.h"
#include "fixtures.h"
#include "inspect.h"

#include "core/error.h"
#include "core/formats/page_dimension.h"
#include "core/formats/shuffle_rule.h"
#include "core/text.h"
#include "gui/dialogs.h"
#include "gui/main_window.h"
#include "gui/table_editor.h"
#include "gui/tabs.h"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>

#include <filesystem>
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

namespace {

namespace fs = std::filesystem;

const std::pair<float, float> kA4{595.0f, 842.0f};
const std::pair<float, float> kLandscape{842.0f, 595.0f};

// Answers every message box with OK, or with the answer given, and keeps "title: text" of each.
struct Messages {
    std::vector<std::string> seen;
    int answer = IDOK;
    decltype(gui::DialogHooks::messageBox) previous;
    Messages() : previous(gui::dialogHooks().messageBox) {
        gui::dialogHooks().messageBox = [this](HWND, const std::wstring& text, const std::wstring& title,
                                               UINT) {
            seen.push_back(toUtf8(title) + ": " + toUtf8(text));
            return answer;
        };
    }
    ~Messages() { gui::dialogHooks().messageBox = previous; }
};

// Runs what the window has posted to itself, such as the work of invokeLater.
void pump() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void type(HWND field, const std::wstring& text) { gui::setWindowText(field, text); }

// Picks an item as a user does, which runs the combo box's handler.
void choose(gui::Tab& tab, HWND combo, int index) { tab.chooseItem(combo, index); }

// Runs the tabs as the Run button does, with the output file named in the Output tab. Throws
// Error with the message of the error box when the run fails.
void runTabs(gui::MainWindow& window, const fs::path& output) {
    gui::setWindowText(window.tab<gui::OutputTab>().outputFile(), output.wstring());
    Messages messages;
    window.runTweaks();
    if (messages.seen.empty())
        throw Failure("the run ended without a message");
    const std::string& last = messages.seen.back();
    if (last.starts_with("Error: "))
        throw Error(Error::Kind::Io, last.substr(7));
    if (last != "PDF Tweak: Finished")
        throw Failure("the run ended with " + last);
}

std::string runError(gui::MainWindow& window, const fs::path& output) {
    try {
        runTabs(window, output);
    } catch (const Error& error) {
        return error.what();
    }
    return "no error";
}

int presetIndex(const std::string& name) {
    const auto& presets = formats::presetRuleSets();
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].first == name)
            return static_cast<int>(i);
    throw Failure("no preset " + name);
}

} // namespace

// InputTab's listener: multiple input and batch processing exclude each other, and the table and
// interleaving follow multiple input.
TEST(gui_inputTabEnablesAsJava) {
    gui::MainWindow window(false);
    gui::InputTab& tab = window.inputTab();
    CHECK(gui::isEnabled(tab.selectButton()) && gui::isEnabled(tab.multiFiles()) &&
          gui::isEnabled(tab.batchProcessing()));
    CHECK(!gui::isEnabled(tab.interleave()) && !gui::isEnabled(tab.interleaveSize()));
    CHECK(!gui::isEnabled(tab.fileCombination().listView()));
    CHECK_EQUAL(toUtf8(gui::windowText(tab.filename())), std::string("(No file selected)"));
    click(tab.multiFiles());
    CHECK(!gui::isEnabled(tab.batchProcessing()) && gui::isEnabled(tab.fileCombination().listView()));
    CHECK(gui::isEnabled(tab.interleave()) && !gui::isEnabled(tab.interleaveSize()));
    click(tab.interleave());
    CHECK(gui::isEnabled(tab.interleaveSize()));
    click(tab.multiFiles());
    CHECK(gui::isEnabled(tab.batchProcessing()) && !gui::isEnabled(tab.fileCombination().listView()));
    CHECK(!gui::isEnabled(tab.interleave()) && !gui::isEnabled(tab.interleaveSize()));
    click(tab.batchProcessing());
    CHECK(!gui::isEnabled(tab.multiFiles()) && gui::isEnabled(tab.selectButton()));
}

// Select... with one file disables itself, Clear starts again, and several files with multiple
// input fill the table, whose listener keeps page numbers in range with Java's warning.
TEST(gui_inputTabAddsFilesAndChecksRanges) {
    Messages messages;
    pdfOfSizes(".", "a.pdf", {kA4, kA4, kA4});
    pdfOfSizes(".", "b.pdf", {kA4, kA4});
    gui::MainWindow window(false);
    gui::InputTab& tab = window.inputTab();
    CHECK_EQUAL(runError(window, "out.pdf"), std::string("No input file selected"));
    tab.addFiles({"a.pdf"});
    CHECK_EQUAL(toUtf8(gui::windowText(tab.filename())), std::string("a.pdf"));
    CHECK(!gui::isEnabled(tab.selectButton()));
    CHECK(window.inputFile() != nullptr);
    click(tab.clearButton());
    CHECK_EQUAL(toUtf8(gui::windowText(tab.filename())), std::string("(No file selected)"));
    CHECK(gui::isEnabled(tab.selectButton()) && window.inputFile() == nullptr);

    click(tab.multiFiles());
    tab.addFiles({"a.pdf", "b.pdf"});
    CHECK_EQUAL(toUtf8(gui::windowText(tab.filename())), std::string("(2 files selected)"));
    CHECK(gui::isChecked(tab.multiFiles()) && !gui::isEnabled(tab.multiFiles()));
    gui::TableEditor& table = tab.fileCombination();
    CHECK_EQUAL(table.rowCount(), 2);
    CHECK_EQUAL(toUtf8(table.cellText(1, 0)), std::string("b.pdf"));
    CHECK(table.row(1)[2] == gui::Value(2));
    type(table.startEditing(1, 1), L"5");
    CHECK(table.stopEditing());
    CHECK(table.row(1)[1] == gui::Value(2));
    pump();
    CHECK(messages.seen ==
          std::vector<std::string>{"PDF Tweak: Entered value out of range. Value has been adjusted."});
    type(table.startEditing(0, 2), L"");
    CHECK(table.stopEditing());
    CHECK(table.row(0)[2] == gui::Value(-1));
    table.startEditing(0, 3);
    table.startEditing(0, 4);
    CHECK_EQUAL(runError(window, "out.pdf"),
                std::string("At least one input file page range contains no pages"));
    table.startEditing(0, 4);
    click(tab.interleave());
    type(tab.interleaveSize(), L"0");
    CHECK_EQUAL(runError(window, "out.pdf"), std::string("Invalid interleave block size"));
}

// A run with multiple input files gives the document of the matching -i options, interleaved as
// -interleave interleaves.
TEST(gui_inputTabRunsMatchTheCommandLine) {
    Messages messages;
    pdfWithPages(".", "a.pdf", "A", 3);
    pdfWithPages(".", "b.pdf", "B", 2);
    gui::MainWindow window(false);
    gui::InputTab& tab = window.inputTab();
    click(tab.multiFiles());
    tab.addFiles({"a.pdf", "b.pdf"});
    gui::TableEditor& table = tab.fileCombination();
    type(table.startEditing(0, 2), L"2");
    type(table.startEditing(1, 1), L"-1");
    type(table.startEditing(1, 2), L"1");
    table.startEditing(1, 4);
    type(table.startEditing(1, 5), L"1");
    CHECK(table.stopEditing());
    runTabs(window, "gui.pdf");
    runCli({"-i1-2", "a.pdf", "-i~1-1o+1", "b.pdf", "cli.pdf"});
    CHECK_EQUAL(describe("gui.pdf"), describe("cli.pdf"));

    click(tab.interleave());
    type(tab.interleaveSize(), L"1");
    runTabs(window, "gui-interleaved.pdf");
    runCli({"-interleave", "1", "-i1-2", "a.pdf", "-i~1-1o+1", "b.pdf", "cli-interleaved.pdf"});
    CHECK_EQUAL(describe("gui-interleaved.pdf"), describe("cli-interleaved.pdf"));
}

// PageSizeTab: each check box enables its fields, the page size fills width and height, and a run
// gives the document of -preserveLinks, -crop, -rotate, and -scale.
TEST(gui_pageSizeTabRunMatchesTheCommandLine) {
    pdfOfSizes(".", "in.pdf", {kA4, kLandscape});
    gui::MainWindow window(false);
    window.inputTab().addFiles({"in.pdf"});
    auto& tab = window.tab<gui::PageSizeTab>();
    CHECK(!gui::isEnabled(tab.cropTo()) && !gui::isEnabled(tab.rotatePortrait()) &&
          !gui::isEnabled(tab.scaleSize()));
    CHECK_EQUAL(toUtf8(gui::windowText(tab.scaleWidth())), std::string("595.0"));
    click(tab.cropPages());
    CHECK(gui::isEnabled(tab.cropTo()));
    click(tab.rotatePages());
    choose(tab, tab.rotatePortrait(), 1);
    click(tab.rotatePages());
    CHECK_EQUAL(gui::selectedIndex(tab.rotatePortrait()), 0);
    click(tab.rotatePages());
    choose(tab, tab.rotatePortrait(), 1);
    click(tab.scalePages());
    int a4Landscape = 0;
    while (formats::commonSizes()[static_cast<size_t>(a4Landscape)].name != "A4 Landscape")
        ++a4Landscape;
    choose(tab, tab.scaleSize(), a4Landscape);
    CHECK_EQUAL(toUtf8(gui::windowText(tab.scaleWidth())), std::string("842.0"));
    type(tab.scaleWidth(), L"420");
    type(tab.scaleHeight(), L"595");
    click(tab.scaleCenter());
    click(tab.preserveHyperlinks());
    runTabs(window, "gui.pdf");
    runCli({"in.pdf", "-preserveLinks", "On", "-crop", "CropBox", "-rotate", "RK", "-scale", "420,595,c",
            "cli.pdf"});
    CHECK_EQUAL(describe("gui.pdf"), describe("cli.pdf"));
    type(tab.scaleWidth(), L"wide");
    CHECK_EQUAL(runError(window, "bad.pdf"), std::string("Invalid scale size"));
}

// WatermarkTab: a run with a background, a coloured text watermark, and masked page numbers on the
// outer bottom corner gives the document of -background, -watermark, and -numbers, and the fields
// report Java's messages.
TEST(gui_watermarkTabRunMatchesTheCommandLine) {
    pdfOfSizes(".", "in.pdf", {kA4, kA4});
    pdfWithText(".", "bg.pdf", "Background");
    gui::MainWindow window(false);
    window.inputTab().addFiles({"in.pdf"});
    auto& tab = window.tab<gui::WatermarkTab>();
    CHECK(!gui::isEnabled(tab.filename()) && !gui::isEnabled(tab.watermarkText()) &&
          !gui::isEnabled(tab.pgnoSize()));
    click(tab.pdfWatermark());
    type(tab.filename(), fs::absolute("bg.pdf").wstring());
    click(tab.textWatermark());
    type(tab.watermarkText(), L"Draft");
    type(tab.watermarkSize(), L"50");
    type(tab.watermarkOpacity(), L"0.5");
    click(tab.watermarkUseColor());
    tab.setColor(RGB(255, 0, 0));
    click(tab.pageNumbers());
    CHECK(!gui::isEnabled(tab.maskText()) && !gui::isEnabled(tab.load()));
    type(tab.pgnoSize(), L"12");
    type(tab.pgnoHOffset(), L"20");
    choose(tab, tab.pgnoHRef(), 4);
    type(tab.pgnoVOffset(), L"30");
    click(tab.useMask());
    CHECK(gui::isEnabled(tab.maskText()));
    type(tab.maskText(), L"p %1$d");
    runTabs(window, "gui.pdf");
    runCli({"in.pdf", "-background", "bg.pdf", "-watermark", "Draft,50,0.5,#FF0000", "-numbers",
            "OB,12,20,30,p %1$d", "cli.pdf"});
    CHECK_EQUAL(describe("gui.pdf"), describe("cli.pdf"));
    type(tab.watermarkOpacity(), L"half");
    CHECK_EQUAL(runError(window, "bad.pdf"), std::string("Unparsable value: For input string: \"half\""));
    type(tab.watermarkOpacity(), L"0.5");
    type(tab.watermarkSize(), L"0");
    CHECK_EQUAL(runError(window, "bad.pdf"), std::string("Font size may not be zero"));
}

// ShuffleTab: a preset fills the config string and the table, Update writes the table back as a
// config string, Use reports what does not parse as Java did, and a run gives -shuffle's document.
TEST(gui_shuffleTabParsesRulesAndRuns) {
    Messages messages;
    pdfWithPages(".", "in.pdf", "P", 4);
    gui::MainWindow window(false);
    window.inputTab().addFiles({"in.pdf"});
    auto& tab = window.tab<gui::ShuffleTab>();
    CHECK_EQUAL(toUtf8(gui::windowText(tab.configString())), std::string("1:!+1N1+0%+0%"));
    CHECK(!gui::isEnabled(tab.preset()));
    click(tab.shufflePages());
    CHECK(gui::isEnabled(tab.preset()) && !gui::isEnabled(tab.blockSize()));
    choose(tab, tab.preset(), presetIndex("2-up Portrait"));
    const std::string twoUp = "2:!+1L0.707+0%-100%,+2L.707+100%-100%";
    CHECK_EQUAL(toUtf8(gui::windowText(tab.configString())), twoUp);
    CHECK_EQUAL(tab.shuffleRulesTable().rowCount(), 2);
    CHECK_EQUAL(toUtf8(gui::windowText(tab.pagesPerPass())), std::string("2"));
    runTabs(window, "gui.pdf");
    runCli({"in.pdf", "-shuffle", twoUp, "cli.pdf"});
    CHECK_EQUAL(describe("gui.pdf"), describe("cli.pdf"));

    gui::TableEditor& table = tab.shuffleRulesTable();
    type(table.startEditing(1, 3), L"0.5");
    CHECK(table.stopEditing());
    click(tab.updatePreview());
    CHECK_EQUAL(toUtf8(gui::windowText(tab.configString())),
                std::string("2:!+1L0.707+0.0%-100.0%,+2L0.5+100.0%-100.0%"));
    type(table.startEditing(1, 3), L"");
    CHECK(table.stopEditing());
    click(tab.updatePreview());
    type(table.startEditing(1, 0), L"+0");
    CHECK(table.stopEditing());
    click(tab.updatePreview());
    type(tab.configString(), L"x");
    click(tab.use());
    type(tab.configString(), L"1:!+0N1+0%+0%");
    click(tab.use());
    CHECK(messages.seen == (std::vector<std::string>{"Message: Please fill in all the fields.",
                                                     "Message: Unparsable option: +0",
                                                     "Message: Unparsable config string: No colon found",
                                                     "Message: Unparsable config string: 1:!+0N1+0%+0%"}));
    click(tab.blockShuffle());
    type(tab.blockSize(), L"1");
    CHECK_EQUAL(runError(window, "bad.pdf"), std::string("Invalid shuffle block size"));
}

// PageNumberTab: Load reads the labels of the input file, and a run gives -pagenumbers' document.
TEST(gui_pageNumberTabLoadsAndRuns) {
    pdfOfSizes(".", "in.pdf", {kA4, kA4, kA4, kA4});
    runCli({"in.pdf", "-pagenumbers", "1=1i", "-pagenumbers", "3=P-:1", "labelled.pdf"});
    gui::MainWindow window(false);
    window.inputTab().addFiles({"labelled.pdf"});
    auto& tab = window.tab<gui::PageNumberTab>();
    CHECK(!gui::isEnabled(tab.load()));
    click(tab.changePageNumbers());
    click(tab.load());
    gui::TableEditor& table = tab.pageNumberRanges();
    CHECK_EQUAL(table.rowCount(), 2);
    CHECK(table.row(0) == (std::vector<gui::Value>{1, std::wstring(L"i, ii, iii"), std::wstring(), 1}));
    CHECK(table.row(1) == (std::vector<gui::Value>{3, std::wstring(L"1, 2, 3"), std::wstring(L"P-"), 1}));
    type(table.startEditing(1, 3), L"5");
    CHECK(table.stopEditing());
    runTabs(window, "gui.pdf");
    runCli({"in.pdf", "-pagenumbers", "1=1i", "-pagenumbers", "3=P-:5", "cli.pdf"});
    CHECK_EQUAL(describe("gui.pdf"), describe("cli.pdf"));
    type(table.startEditing(1, 3), L"0");
    CHECK(table.stopEditing());
    CHECK_EQUAL(runError(window, "bad.pdf"), std::string("Logical page numbers must be positive"));
}

namespace {

// The index of a combo box item by its text.
int itemIndex(HWND combo, const std::wstring& text) {
    for (int i = 0; i < gui::itemCount(combo); ++i)
        if (gui::itemText(combo, i) == text)
            return i;
    throw Failure("no item " + toUtf8(text));
}

} // namespace

// RegressionTests.gui_outputTabDisablesTransparencyForTiffJpeg: the Output tab offers transparency
// for a TIFF only while it is not JPEG-compressed.
TEST(gui_outputTabDisablesTransparencyForTiffJpeg) {
    gui::MainWindow window(false);
    auto& tab = window.tab<gui::OutputTab>();
    bool enabled[6];
    click(tab.multipageTiff());
    enabled[0] = gui::isEnabled(tab.transparent());
    choose(tab, tab.compressionType(), 2); // JPEG
    enabled[1] = gui::isEnabled(tab.transparent());
    choose(tab, tab.colorMode(), 1); // GRAY
    choose(tab, tab.colorMode(), 0); // RGB
    enabled[2] = gui::isEnabled(tab.transparent());
    click(tab.multipageTiff());
    click(tab.burst());
    choose(tab, tab.fileType(), 7); // TIFF
    enabled[3] = gui::isEnabled(tab.transparent());
    choose(tab, tab.fileType(), 2); // PNG
    enabled[4] = gui::isEnabled(tab.transparent());
    choose(tab, tab.fileType(), 7);
    choose(tab, tab.compressionType(), 1); // LZW
    enabled[5] = gui::isEnabled(tab.transparent());
    CHECK(enabled[0]);
    CHECK(!enabled[1]);
    CHECK(!enabled[2]);
    CHECK(!enabled[3]);
    CHECK(enabled[4]);
    CHECK(enabled[5]);
}

// RegressionTests.gui_outputTabKeepsQualityForTiffJpeg: the quality slider stays for a TIFF
// compression that uses it when the file type changes.
TEST(gui_outputTabKeepsQualityForTiffJpeg) {
    gui::MainWindow window(false);
    auto& tab = window.tab<gui::OutputTab>();
    bool enabled[5];
    click(tab.multipageTiff());
    enabled[0] = gui::isEnabled(tab.qualitySlider());
    choose(tab, tab.compressionType(), 2); // JPEG
    enabled[1] = gui::isEnabled(tab.qualitySlider());
    click(tab.multipageTiff());
    click(tab.multipageTiff());
    enabled[2] = gui::isEnabled(tab.qualitySlider());
    click(tab.multipageTiff());
    click(tab.burst());
    choose(tab, tab.fileType(), 7); // TIFF
    enabled[3] = gui::isEnabled(tab.qualitySlider());
    choose(tab, tab.compressionType(), 1); // LZW
    enabled[4] = gui::isEnabled(tab.qualitySlider());
    CHECK(!enabled[0]);
    CHECK(enabled[1]);
    CHECK(enabled[2]);
    CHECK(enabled[3]);
    CHECK(!enabled[4]);
}

// RegressionTests.gui_outputTabOffersQualityForTiffDeflate: DEFLATE, like ZLIB, takes the quality
// as its compression level.
TEST(gui_outputTabOffersQualityForTiffDeflate) {
    gui::MainWindow window(false);
    auto& tab = window.tab<gui::OutputTab>();
    click(tab.multipageTiff());
    choose(tab, tab.compressionType(), 5); // DEFLATE
    CHECK(gui::isEnabled(tab.qualitySlider()));
    click(tab.multipageTiff());
    click(tab.multipageTiff());
    CHECK(gui::isEnabled(tab.qualitySlider()));
}

// OutputTab: the chosen name gets the extension of the output type, which follows burst, the file
// type, and the multipage TIFF, an existing file is overwritten only after a yes, the info icon
// shows for images, and checkRun asks for a name and for variables in batch mode.
TEST(gui_outputTabNamesTheOutputAsJava) {
    Messages messages;
    gui::MainWindow window(false);
    auto& tab = window.tab<gui::OutputTab>();
    const std::wstring base = fs::absolute("out").wstring();
    const auto output = [&tab] { return gui::windowText(tab.outputFile()); };
    tab.chooseOutput("out");
    CHECK(output() == base + L".pdf");
    click(tab.burst());
    CHECK(gui::isEnabled(tab.fileType()) && tab.warningText().empty());
    choose(tab, tab.fileType(), 2); // PNG
    CHECK(output() == base + L".png");
    CHECK(tab.warningText() == L"Images will be exported in 72 dpi");
    CHECK(gui::itemCount(tab.colorMode()) == 2 && gui::itemText(tab.colorMode(), 1) == L"Grayscale");
    click(tab.multipageTiff());
    CHECK(!gui::isChecked(tab.burst()) && !gui::isEnabled(tab.fileType()));
    CHECK(output() == base + L".tiff");
    CHECK(gui::itemCount(tab.colorMode()) == 4);
    click(tab.multipageTiff());
    CHECK(output() == base + L".pdf");
    CHECK(gui::isEnabled(tab.optimizeSize()) && !gui::isEnabled(tab.colorMode()));
    click(tab.uncompressed());
    CHECK(gui::windowText(tab.pageMarks()) == L"Add PdfTk page marks");
    click(tab.uncompressed());
    CHECK(gui::windowText(tab.pageMarks()) == L"Remove PdfTk page marks");

    writeFile("taken.pdf", "x");
    messages.answer = IDNO;
    tab.chooseOutput("taken.pdf");
    CHECK(output() == base + L".pdf");
    messages.answer = IDYES;
    tab.chooseOutput("taken.pdf");
    CHECK(output() == fs::absolute("taken.pdf").wstring());
    CHECK(messages.seen == (std::vector<std::string>{"Confirm Overwrite: Overwrite existing file?",
                                                     "Confirm Overwrite: Overwrite existing file?"}));

    pdfOfSizes(".", "a.pdf", {kA4});
    pdfOfSizes(".", "b.pdf", {kA4});
    gui::setWindowText(tab.outputFile(), L"");
    window.inputTab().addFiles({"a.pdf"});
    CHECK_EQUAL(runError(window, ""), std::string("No output file selected"));
    click(window.inputTab().clearButton());
    click(window.inputTab().batchProcessing());
    window.inputTab().addFiles({"a.pdf", "b.pdf"});
    CHECK_EQUAL(runError(window, "out.pdf"),
                std::string("Variables in output file name required for batch mode"));
    runTabs(window, "<F>-batch.pdf");
    CHECK(fs::exists("a-batch.pdf") && fs::exists("b-batch.pdf"));
}

// OutputTab: a multipage TIFF and burst images are the files of -oi and -ob with the image options.
TEST(gui_outputTabExportsImagesAsTheCommandLine) {
    pdfWithPages(".", "in.pdf", "Page ", 2);
    gui::MainWindow window(false);
    window.inputTab().addFiles({"in.pdf"});
    auto& tab = window.tab<gui::OutputTab>();
    click(tab.multipageTiff());
    choose(tab, tab.colorMode(), 1);       // GRAY
    choose(tab, tab.compressionType(), 1); // LZW
    runTabs(window, "gui.tiff");
    runCli({"in.pdf", "-colormode", "GRAY", "-tiffcompression", "LZW", "-oi", "cli.tiff"});
    CHECK(readFile("gui.tiff") == readFile("cli.tiff"));

    click(tab.burst());
    choose(tab, tab.fileType(), 2); // PNG
    runTabs(window, "gui-*.png");
    runCli({"in.pdf", "-burstfiletype", "PNG", "-ob", "cli-*.png"});
    for (const char* page : {"1", "2"})
        CHECK(readFile(std::string("gui-") + page + ".png") ==
              readFile(std::string("cli-") + page + ".png"));
}

// BookmarkTab: Export to CSV writes the lines of -savebookmarks, Import from CSV and Import from PDF
// read them back, Load reads the input's outline, and a run gives -bookmark's document.
TEST(gui_bookmarkTabImportsExportsAndRuns) {
    Messages messages;
    pdfOfSizes(".", "in.pdf", {kA4, kA4});
    gui::MainWindow window(false);
    window.inputTab().addFiles({"in.pdf"});
    auto& tab = window.tab<gui::BookmarkTab>();
    CHECK(!gui::isEnabled(tab.load()) && !gui::isEnabled(tab.exportCsvButton()));
    click(tab.changeBookmarks());
    gui::TableEditor& table = tab.bookmarks();
    table.addRow({1, false, std::wstring(L"Stale"), 1, std::wstring(), false, false, std::wstring()});
    click(tab.load());
    CHECK_EQUAL(table.rowCount(), 0);
    table.addRow({1, true, std::wstring(L"Part"), 1, std::wstring(L"Fit"), false, false, std::wstring()});
    table.addRow({2, false, std::wstring(L"Chapter; one"), 2, std::wstring(), true, true,
                  std::wstring(L"Color=\"1 0 0\"")});
    tab.exportCsv("bookmarks.csv");
    const std::string lines = "1;O;Part;1 Fit\r\n2;BI;Chapter\\3B one;2;Color=\\221 0 0\\22\r\n";
    CHECK_EQUAL(readFile("bookmarks.csv"), lines);
    runTabs(window, "gui.pdf");
    runCli({"in.pdf", "-loadbookmarks", "bookmarks.csv", "cli.pdf"});
    CHECK_EQUAL(describe("gui.pdf"), describe("cli.pdf"));

    table.clear();
    writeFile("import.csv", "# a comment\n" + lines);
    tab.importCsv("import.csv");
    CHECK_EQUAL(table.rowCount(), 2);
    CHECK(table.row(1)[2] == gui::Value(std::wstring(L"Chapter; one")));
    tab.importPdf("cli.pdf");
    CHECK_EQUAL(table.rowCount(), 2);
    CHECK(table.row(0)[2] == gui::Value(std::wstring(L"Part")));
    writeFile("bad.csv", "not a bookmark\n");
    tab.importCsv("bad.csv");
    tab.importCsv("missing.csv");
    CHECK(messages.seen ==
          (std::vector<std::string>{"Error reading file: Cannot parse bookmark",
                                    "Error reading file: missing.csv (The system cannot find the "
                                    "file specified)"}));
    table.addRow(
        {1, false, std::wstring(L"x"), 1, std::wstring(), false, false, std::wstring(L" A=\"1\"")});
    CHECK_EQUAL(runError(window, "bad.pdf"), std::string("More options incorrect"));
    tab.exportCsv("failed.csv");
    CHECK(!fs::exists("failed.csv"));
    CHECK_EQUAL(messages.seen.back(), std::string("Error reading file: More options incorrect"));
}

// AttachmentTab: Remove takes the selected files out of the list, and a run embeds the others as
// -attach does.
TEST(gui_attachmentTabEmbedsTheListedFiles) {
    pdfOfSizes(".", "in.pdf", {kA4});
    writeFile("a.txt", "first");
    writeFile("b.txt", "second");
    gui::MainWindow window(false);
    window.inputTab().addFiles({"in.pdf"});
    auto& tab = window.tab<gui::AttachmentTab>();
    tab.addAttachment(fs::absolute("a.txt"));
    tab.addAttachment(fs::absolute("b.txt"));
    SendMessageW(tab.list(), LB_SETSEL, TRUE, 0);
    click(tab.removeButton());
    CHECK(tab.attachments() == std::vector<fs::path>{fs::absolute("b.txt")});
    CHECK_EQUAL(SendMessageW(tab.list(), LB_GETCOUNT, 0, 0), 1);
    runTabs(window, "gui.pdf");
    runCli({"in.pdf", "-attach", fs::absolute("b.txt"), "cli.pdf"});
    CHECK_EQUAL(describe("gui.pdf"), describe("cli.pdf"));
}

// InteractionTab: a run with transitions and viewer preferences gives the document of -transition,
// -pagemode, -pagelayout, and -viewpref, and an empty cell fails as Java's unboxing did.
TEST(gui_interactionTabRunMatchesTheCommandLine) {
    pdfOfSizes(".", "in.pdf", {kA4, kA4, kA4});
    gui::MainWindow window(false);
    window.inputTab().addFiles({"in.pdf"});
    auto& tab = window.tab<gui::InteractionTab>();
    click(tab.addTransitions());
    gui::TableEditor& table = tab.transitions();
    table.addRow({2, -1, std::wstring(L"Dissolve"), 3, 5});
    click(tab.addPrefs());
    CHECK(gui::isEnabled(tab.pageMode()) && !gui::isEnabled(tab.optionalPrefValue(0)));
    choose(tab, tab.pageMode(), itemIndex(tab.pageMode(), L"Outline"));
    choose(tab, tab.pageLayout(), itemIndex(tab.pageLayout(), L"One Column"));
    int title = 0;
    while (gui::windowText(tab.optionalPrefCheck(title)) != L"Display document title: ")
        ++title;
    click(tab.optionalPrefCheck(title));
    CHECK(gui::isEnabled(tab.optionalPrefValue(title)));
    choose(tab, tab.optionalPrefValue(title), itemIndex(tab.optionalPrefValue(title), L"true"));
    runTabs(window, "gui.pdf");
    runCli({"in.pdf", "-transition", "2-3:Dissolve,3,5", "-pagemode", "Outline", "-pagelayout",
            "One_Column", "-viewpref", "Display_document_title=true", "cli.pdf"});
    CHECK_EQUAL(describe("gui.pdf"), describe("cli.pdf"));
    table.row(0)[3] = std::monostate{};
    CHECK_EQUAL(runError(window, "bad.pdf"),
                std::string("Please fill in every cell of the transitions table."));
    table.row(0)[3] = 3;
    table.row(0)[0] = -5;
    CHECK_EQUAL(runError(window, "bad.pdf"), std::string("Page -1 does not exist."));
}

// DocumentInfoTab: Load lists the input's information, Add predefined adds a known name, and a run
// gives -docinfo's document.
TEST(gui_documentInfoTabLoadsAndRuns) {
    FixtureOptions options;
    options.info = {{"Title", "Old title"}, {"Author", "Someone"}};
    writePdf(".", "in.pdf", {{595, 842, "Page 1"}}, options);
    gui::MainWindow window(false);
    window.inputTab().addFiles({"in.pdf"});
    auto& tab = window.tab<gui::DocumentInfoTab>();
    CHECK(!gui::isEnabled(tab.infoLoad()));
    click(tab.infoChange());
    click(tab.infoLoad());
    gui::TableEditor& table = tab.infoEntries();
    std::vector<std::string> loaded;
    for (int i = 0; i < table.rowCount(); ++i)
        loaded.push_back(toUtf8(table.cellText(i, 0)) + "=" + toUtf8(table.cellText(i, 1)));
    std::vector<std::string> expected;
    for (const auto& [key, value] : window.inputFile()->infoDictionary())
        expected.push_back(key + "=" + value);
    CHECK(loaded == expected);
    table.clear();
    tab.addPredefined(0);
    CHECK(table.row(0)[0] == gui::Value(std::wstring(L"Title")));
    table.row(0)[1] = std::wstring(L"New title");
    table.addRow({std::wstring(L"Author"), std::wstring()});
    table.addRow({std::wstring(L"Custom"), std::wstring(L"Value")});
    runTabs(window, "gui.pdf");
    runCli({"in.pdf", "-docinfo", "Title=New title", "-docinfo", "Author=", "-docinfo", "Custom=Value",
            "cli.pdf"});
    CHECK_EQUAL(describe("gui.pdf"), describe("cli.pdf"));
}

// EncryptSignTab: 40-bit RC4 leaves out the permissions it cannot express, Load reads the
// encryption of the input, and a run encrypts and signs as the command line does.
TEST(gui_encryptSignTabLoadsAndRuns) {
    pdfOfSizes(".", "in.pdf", {kA4});
    runCli({"in.pdf", "-ownerpw", "owner", "-userpw", "user", "-encmode", "1", "-permissions",
            "Printing,Copy", "locked.pdf"});
    gui::MainWindow window(false);
    auto& tab = window.tab<gui::EncryptSignTab>();
    CHECK(!gui::isEnabled(tab.encryptMode()) && !gui::isEnabled(tab.keystore()));
    CHECK_EQUAL(gui::selectedIndex(tab.encryptMode()), 1);
    click(tab.encryptDocument());
    click(tab.permission(5));
    choose(tab, tab.encryptMode(), 0);
    CHECK(!gui::isEnabled(tab.permission(5)) && !gui::isChecked(tab.permission(5)) &&
          gui::isEnabled(tab.permission(3)));

    const auto previous = gui::dialogHooks().askForPassword;
    gui::dialogHooks().askForPassword = [](HWND) { return std::optional<std::wstring>(L"owner"); };
    window.inputTab().addFiles({"locked.pdf"});
    gui::dialogHooks().askForPassword = previous;
    click(tab.encryptLoad());
    CHECK_EQUAL(gui::selectedIndex(tab.encryptMode()), 1);
    CHECK(gui::windowText(tab.ownerPassword()) == L"owner" &&
          gui::windowText(tab.userPassword()) == L"user");
    CHECK(gui::isChecked(tab.permission(0)) && gui::isChecked(tab.permission(2)) &&
          !gui::isChecked(tab.permission(1)));
    runTabs(window, "gui.pdf");
    runCli({"-password", "owner", "locked.pdf", "-ownerpw", "owner", "-userpw", "user", "-encmode", "1",
            "-permissions", "Printing,Copy", "cli.pdf"});
    DescribeOptions locked;
    locked.password = "owner";
    CHECK_EQUAL(describe("gui.pdf", locked), describe("cli.pdf", locked));

    click(tab.encryptDocument());
    const fs::path keystore = pkcs12(".");
    click(tab.signDocument());
    gui::setWindowText(tab.keystore(), fs::absolute(keystore).wstring());
    gui::setWindowText(tab.keyPassword(), L"secret");
    choose(tab, tab.certLevel(), 1);
    runTabs(window, "signed.pdf");
    CHECK_EQUAL(verifySignature("signed.pdf"), std::string("/adbe.pkcs7.detached"));
}
