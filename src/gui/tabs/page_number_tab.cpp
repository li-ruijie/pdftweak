#include "core/error.h"
#include "core/text.h"
#include "gui/main_window.h"
#include "gui/tabs.h"

#include <algorithm>

namespace pdftweak::gui {
namespace page_numbers {
namespace {

// NUMBER_STYLES, in the order of formats::LabelStyle.
const std::vector<std::wstring>& numberStyles() {
    static const std::vector<std::wstring> styles{L"1, 2, 3", L"I, II, III", L"i, ii, iii",
                                                  L"A, B, C", L"a, b, c",    L"Empty"};
    return styles;
}

} // namespace

std::unique_ptr<TableEditor> buildRanges() {
    auto result =
        std::make_unique<TableEditor>(std::vector<Column>{{L"Start Page", ColumnType::Integer},
                                                          {L"Style", ColumnType::Choice, numberStyles()},
                                                          {L"Prefix", ColumnType::Text},
                                                          {L"Logical Page", ColumnType::Integer}},
                                      std::vector<Value>{1, numberStyles()[0], std::wstring(), 1});
    TableEditor* table = result.get();
    result->setRowListener([table](int rowIndex, int columnIndex) {
        if (columnIndex == 0 || columnIndex == 3) {
            Value& value = table->row(rowIndex)[static_cast<size_t>(columnIndex)];
            if (std::holds_alternative<std::monostate>(value))
                value = 1;
        }
    });
    return result;
}

std::vector<formats::PageLabelFormat> formats(TableEditor& ranges) {
    std::vector<formats::PageLabelFormat> result;
    for (int i = 0; i < ranges.rowCount(); ++i) {
        const std::vector<Value>& row = ranges.row(i);
        const auto* style = std::get_if<std::wstring>(&row[1]);
        const auto found = style == nullptr
                               ? numberStyles().end()
                               : std::find(numberStyles().begin(), numberStyles().end(), *style);
        const int nstyle =
            found == numberStyles().end() ? 0 : static_cast<int>(found - numberStyles().begin());
        if (std::get<int>(row[3]) < 1)
            throw Error(Error::Kind::Io, "Logical page numbers must be positive");
        if (std::get<int>(row[0]) < 1)
            throw Error(Error::Kind::Io, "Start page numbers must be positive");
        const auto* prefix = std::get_if<std::wstring>(&row[2]);
        result.push_back({std::get<int>(row[0]), static_cast<formats::LabelStyle>(nstyle),
                          prefix == nullptr ? std::string() : toUtf8(*prefix), std::get<int>(row[3])});
    }
    return result;
}

void load(MainWindow& mainWindow, TableEditor& ranges) {
    ranges.clear();
    if (!mainWindow.inputFile())
        return;
    if (const auto labels = mainWindow.inputFile()->pageLabels()) {
        for (const formats::PageLabelFormat& label : *labels)
            ranges.addRow({label.physicalPage, numberStyles()[static_cast<size_t>(label.style)],
                           fromUtf8(label.prefix), label.logicalPage});
    }
}

} // namespace page_numbers

void PageNumberTab::build() {
    FormLayout& layout = newLayout(L"f:p:g, f:p", L"f:p, f:p, f:p:g");
    changePageNumbers_ = checkBox(L"Change page numbers", [this] { updateEnabledState(); });
    layout.add(changePageNumbers_, 1, 1);
    load_ = button(L"Load from document", [this] { page_numbers::load(mainWindow_, *pageNumberRanges_); });
    layout.add(load_, 2, 1);
    pageNumberRanges_ = page_numbers::buildRanges();
    layout.add(childPage(*pageNumberRanges_), 1, 3, 2);
    setLayout(layout.asComponent());
    updateEnabledState();
}

void PageNumberTab::updateEnabledState() {
    enable(load_, isChecked(changePageNumbers_));
    pageNumberRanges_->setEnabled(isChecked(changePageNumbers_));
}

void PageNumberTab::checkRun() { pageNumberRanges_->checkRun("page number"); }

Operation PageNumberTab::prepare() {
    if (!isChecked(changePageNumbers_))
        return {};
    const std::vector<formats::PageLabelFormat> labels = page_numbers::formats(*pageNumberRanges_);
    return [labels](RunContext& context) { context.tweak->setPageNumbers(labels); };
}

} // namespace pdftweak::gui
