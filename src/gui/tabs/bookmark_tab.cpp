#include "core/error.h"
#include "core/files.h"
#include "core/pdf/outline.h"
#include "core/text.h"
#include "gui/dialogs.h"
#include "gui/main_window.h"
#include "gui/tabs.h"

#include <fstream>

namespace pdftweak::gui {

void BookmarkTab::build() {
    FormLayout& layout = newLayout(L"f:p:g, f:p", L"f:p, f:p, f:p:g");
    changeBookmarks_ = checkBox(L"Change chapter bookmarks", [this] { updateEnabledState(); });
    layout.add(changeBookmarks_, 1, 1);
    load_ = button(L"Load from document", [this] {
        const std::vector<formats::Bookmark> loaded = mainWindow_.inputTab().loadBookmarks();
        bookmarks_->clear();
        appendBookmarks(loaded);
    });
    layout.add(load_, 2, 1);
    importPdf_ = button(L"Import from PDF", [this] {
        const auto files = openFiles(mainWindow_.handle(), Chooser::Pdf, false);
        if (!files.empty())
            importPdf(files[0]);
    });
    importCsv_ = button(L"Import from CSV", [this] {
        const auto files = openFiles(mainWindow_.handle(), Chooser::Other, false);
        if (!files.empty())
            importCsv(files[0]);
    });
    exportCsv_ = button(L"Export to CSV", [this] {
        const auto file = saveFile(mainWindow_.handle(), Chooser::Other);
        if (!file)
            return;
        if (std::filesystem::exists(*file) && !confirmOverwrite(mainWindow_.handle()))
            return;
        exportCsv(*file);
    });
    layout.add(equalRow({control(importPdf_), control(importCsv_), control(exportCsv_)}), 1, 2, 2);
    bookmarks_ = std::make_unique<TableEditor>(
        std::vector<Column>{{L"Depth", ColumnType::Integer},
                            {L"Open", ColumnType::Boolean},
                            {L"Title", ColumnType::Text},
                            {L"Page", ColumnType::Integer},
                            {L"Position", ColumnType::Text},
                            {L"Bold", ColumnType::Boolean},
                            {L"Italic", ColumnType::Boolean},
                            {L"Options", ColumnType::Text}},
        std::vector<Value>{1, false, std::wstring(), 1, std::wstring(), false, false, std::wstring()});
    layout.add(childPage(*bookmarks_), 1, 3, 2);
    setLayout(layout.asComponent());
    updateEnabledState();
}

void BookmarkTab::importPdf(const std::filesystem::path& file) {
    try {
        pdf::InputFile input(file, "");
        const std::vector<formats::Bookmark> imported = pdf::readBookmarks(*input.qpdf(), 1);
        bookmarks_->clear();
        appendBookmarks(imported);
    } catch (const Error& error) {
        messageBox(mainWindow_.handle(), fromUtf8(error.what()), L"Error reading file", MB_ICONERROR);
    }
}

void BookmarkTab::importCsv(const std::filesystem::path& file) {
    try {
        std::vector<formats::Bookmark> imported;
        for (const std::string& line : readLines(toUtf8(file.wstring()))) {
            if (line.starts_with("#"))
                continue;
            imported.push_back(formats::parseBookmark(line));
        }
        bookmarks_->clear();
        appendBookmarks(imported);
    } catch (const Error& error) {
        messageBox(mainWindow_.handle(), fromUtf8(error.what()), L"Error reading file", MB_ICONERROR);
    }
}

void BookmarkTab::exportCsv(const std::filesystem::path& file) {
    try {
        // every line first, so that a bookmark that fails leaves no file behind, where Java left an
        // empty one
        std::string text;
        for (int i = 0; i < bookmarks_->rowCount(); ++i)
            text += bookmark(bookmarks_->row(i)).toString() + "\r\n";
        createForWriting(file, toUtf8(file.wstring()));
        std::ofstream out(file, std::ios::binary);
        out << text;
    } catch (const Error& error) {
        messageBox(mainWindow_.handle(), fromUtf8(error.what()), L"Error reading file", MB_ICONERROR);
    }
}

void BookmarkTab::appendBookmarks(const std::vector<formats::Bookmark>& list) {
    for (const formats::Bookmark& b : list)
        bookmarks_->addRow({b.depth, b.open, fromUtf8(b.title), b.page, fromUtf8(b.pagePosition), b.bold,
                            b.italic, fromUtf8(formats::makeOptionString(b.more))});
}

formats::Bookmark BookmarkTab::bookmark(const std::vector<Value>& row) const {
    const auto text = [&row](size_t column) {
        const auto* value = std::get_if<std::wstring>(&row[column]);
        return value == nullptr ? std::string() : toUtf8(*value);
    };
    const auto flag = [&row](size_t column) {
        const auto* value = std::get_if<bool>(&row[column]);
        return value != nullptr && *value;
    };
    const int depth = std::holds_alternative<int>(row[0]) ? std::get<int>(row[0]) : 1;
    const int page = std::holds_alternative<int>(row[3]) ? std::get<int>(row[3]) : 1;
    return formats::makeBookmark(depth, text(2), flag(1), page, text(4), flag(5), flag(6), text(7));
}

void BookmarkTab::updateEnabledState() {
    const bool change = isChecked(changeBookmarks_);
    for (HWND control : {load_, importPdf_, importCsv_, exportCsv_})
        enable(control, change);
    bookmarks_->setEnabled(change);
}

void BookmarkTab::checkRun() { bookmarks_->checkRun("chapter bookmarks"); }

Operation BookmarkTab::prepare() {
    if (!isChecked(changeBookmarks_))
        return {};
    std::vector<formats::Bookmark> list;
    for (int i = 0; i < bookmarks_->rowCount(); ++i) {
        try {
            list.push_back(bookmark(bookmarks_->row(i)));
        } catch (const Error& error) {
            throw Error(Error::Kind::Io, error.what());
        }
    }
    return [list](RunContext& context) { context.tweak->updateBookmarks(list); };
}

} // namespace pdftweak::gui
