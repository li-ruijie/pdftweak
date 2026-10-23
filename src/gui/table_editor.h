#pragma once

// TableComponent and TableComponentModel: a table of typed cells over the Add, Up, Down, and Delete
// buttons. The table is a report list view with one in-place editor at a time. A text, number, or
// list cell is edited in a field after a double click, F2, or typing, a choice or file cell in a
// drop-down list after one click, and a check box cell changes with a click or the space bar.

#include "core/pdf/input_file.h"
#include "gui/window.h"

#include <commctrl.h>

#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace pdftweak::gui {

// The column classes of the Java tables.
enum class ColumnType {
    Text,        // String
    Integer,     // Integer
    Number,      // Double
    Boolean,     // Boolean
    Choice,      // String, edited with a JComboBox of fixed items
    InputFile,   // PdfInputFile, edited with the combo box of the input files
    IntegerList, // IntegerList
};

// A cell: null (monostate), or a value of its column's type.
using Value = std::variant<std::monostate, std::wstring, int, double, bool, std::shared_ptr<pdf::InputFile>,
                           std::vector<int>>;

struct Column {
    std::wstring caption;
    ColumnType type = ColumnType::Text;
    std::vector<std::wstring> choices; // the items of a Choice column
    int preferredWidth = 75;           // TableColumn's default width, in pixels at 96 DPI
};

class TableEditor final : public Page {
public:
    // The columns, and the sample row that Add appends.
    TableEditor(std::vector<Column> columns, std::vector<Value> sample);

    // This table as a component of the page that holds it, which must have created it.
    Component component();

    // JScrollPane.setPreferredSize, in pixels at 96 DPI. The default is 750 by 400.
    void setPreferredSize(SIZE size) { preferred_ = size; }

    // The input files offered by an InputFile column.
    void setFileChoices(std::function<std::vector<std::shared_ptr<pdf::InputFile>>()> files) {
        fileChoices_ = std::move(files);
    }

    // TableComponentModel.RowListener: called after the user changed a cell. The listener may
    // change the row.
    void setRowListener(std::function<void(int row, int column)> listener) {
        listener_ = std::move(listener);
    }

    void setEnabled(bool enabled);

    int rowCount() const { return static_cast<int>(rows_.size()); }
    std::vector<Value>& row(int index) { return rows_[static_cast<size_t>(index)]; }

    // Both stop the editing first and do nothing when the editor holds a value that does not parse.
    void clear();
    void addRow(std::vector<Value> values);

    // TableComponent.checkRun: stops the editing, or throws Error(Io, "Please finish editing the
    // <name> table!") when the editor holds a value that does not parse.
    void checkRun(const std::string& name);

    // For the tests, which drive the table as a user would.
    HWND listView() const { return list_; }
    HWND addButton() const { return add_; }
    HWND upButton() const { return up_; }
    HWND downButton() const { return down_; }
    HWND deleteButton() const { return delete_; }
    // Starts editing a cell as a click or double click does, and returns the editor (null for a
    // check box cell, which changes at once).
    HWND startEditing(int row, int column);
    HWND editor() const { return editor_; }
    // JTable.getCellEditor().stopCellEditing(): returns false when the value does not parse.
    bool stopEditing();
    void cancelEditing();
    // The selected rows, as JTable.getSelectedRows.
    std::vector<int> selectedRows() const;
    void selectRows(int first, int last);
    // The text a cell shows.
    std::wstring cellText(int row, int column) const;

private:
    void build() override;
    LRESULT message(UINT msg, WPARAM wParam, LPARAM lParam) override;
    LRESULT notify(NMHDR* header);
    LRESULT customDraw(NMLVCUSTOMDRAW* draw);
    void refresh();
    void fitColumns();
    void placeEditor();
    void setCurrent(int row, int column);
    void toggle(int row, int column);
    void moveSelection(int offset);
    static LRESULT CALLBACK listProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                                     DWORD_PTR data);
    static LRESULT CALLBACK editorProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                                       DWORD_PTR data);

    std::vector<Column> columns_;
    std::vector<Value> sample_;
    std::vector<std::vector<Value>> rows_;
    std::function<std::vector<std::shared_ptr<pdf::InputFile>>()> fileChoices_;
    std::function<void(int, int)> listener_;
    SIZE preferred_{750, 400};
    HWND list_ = nullptr;
    HWND add_ = nullptr;
    HWND up_ = nullptr;
    HWND down_ = nullptr;
    HWND delete_ = nullptr;
    HWND editor_ = nullptr;
    int editRow_ = -1;
    int editColumn_ = -1;
    bool editorInvalid_ = false;
    std::vector<std::shared_ptr<pdf::InputFile>> editorFiles_;
    int currentColumn_ = 0;
    std::wstring display_;
};

} // namespace pdftweak::gui
