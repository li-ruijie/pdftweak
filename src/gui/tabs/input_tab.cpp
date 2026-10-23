#include "core/error.h"
#include "core/java_text.h"
#include "core/pdf/outline.h"
#include "core/text.h"
#include "core/version.h"
#include "gui/dialogs.h"
#include "gui/main_window.h"
#include "gui/tabs.h"

namespace pdftweak::gui {

void InputTab::build() {
    FormLayout& layout = newLayout(L"f:p, f:p:g, f:p, f:p, f:p, f:p", L"f:p, f:p, f:p:g");
    layout.add(label(L"Filename"), 1, 1);
    filename_ = edit(L"", 0, ES_READONLY);
    layout.add(filename_, 2, 1, 3);
    selectFile_ = button(L"Select...", [this] {
        const bool multiple = isChecked(multiFiles_) || isChecked(batchProcessing_);
        const std::vector<std::filesystem::path> files =
            openFiles(mainWindow_.handle(), Chooser::Pdf, multiple);
        if (!files.empty())
            addFiles(files);
    });
    layout.add(selectFile_, 5, 1);
    clear_ = button(L"Clear", [this] {
        inputFiles_.clear();
        mainWindow_.setInputFile(nullptr);
        fileCombination_->clear();
        enable(selectFile_, true);
        enable(multiFiles_, !isChecked(batchProcessing_));
        enable(batchProcessing_, !isChecked(multiFiles_));
        updateFileName();
    });
    layout.add(clear_, 6, 1);
    multiFiles_ = checkBox(L"Multiple file input / Select pages", [this] { updateEnabledState(); });
    layout.add(multiFiles_, 1, 2, 2);
    interleave_ = checkBox(L"Interleave documents in blocks of", [this] { updateEnabledState(); });
    enable(interleave_, false);
    layout.add(interleave_, 3, 2);
    interleaveSize_ = edit(L"1", 10);
    enable(interleaveSize_, false);
    layout.add(interleaveSize_, 4, 2);
    batchProcessing_ = checkBox(L"Batch processing", [this] { updateEnabledState(); });
    layout.add(batchProcessing_, 5, 2, 2);
    fileCombination_ = std::make_unique<TableEditor>(
        std::vector<Column>{{L"File", ColumnType::InputFile, {}, 300},
                            {L"From Page", ColumnType::Integer},
                            {L"To Page", ColumnType::Integer},
                            {L"Include Odd", ColumnType::Boolean},
                            {L"Include Even", ColumnType::Boolean},
                            {L"Empty Before", ColumnType::IntegerList}},
        std::vector<Value>{std::monostate{}, 1, -1, true, true, std::vector<int>{0}});
    layout.add(childPage(*fileCombination_), 1, 3, 6);
    fileCombination_->setEnabled(false);
    fileCombination_->setFileChoices([this] { return inputFiles_; });
    fileCombination_->setRowListener([this](int row, int column) { rowChanged(row, column); });
    setLayout(layout.asComponent());
    updateFileName();
}

void InputTab::updateEnabledState() {
    if (isChecked(multiFiles_)) {
        enable(batchProcessing_, false);
        enable(selectFile_, true);
        fileCombination_->setEnabled(true);
        enable(interleave_, true);
        enable(interleaveSize_, isChecked(interleave_));
    } else if (isChecked(batchProcessing_)) {
        enable(multiFiles_, false);
        enable(selectFile_, true);
    } else {
        enable(batchProcessing_, true);
        enable(multiFiles_, true);
        enable(selectFile_, inputFiles_.empty());
        fileCombination_->setEnabled(false);
        enable(interleave_, false);
        enable(interleaveSize_, false);
    }
}

void InputTab::rowChanged(int rowIndex, int columnIndex) {
    std::vector<Value>& row = fileCombination_->row(rowIndex);
    if (columnIndex < 3) {
        const auto* file = std::get_if<std::shared_ptr<pdf::InputFile>>(&row[0]);
        if (std::holds_alternative<std::monostate>(row[1]))
            row[1] = 1;
        if (std::holds_alternative<std::monostate>(row[2]))
            row[2] = -1;
        const int from = std::get<int>(row[1]);
        const int to = std::get<int>(row[2]);
        const int pages = file == nullptr || !*file ? 1 : (*file)->pageCount();
        bool changed = false;
        if (from > pages) {
            row[1] = columnIndex == 0 ? 1 : pages;
            changed = true;
        }
        if (from < -pages) {
            row[1] = columnIndex == 0 ? 1 : -pages;
            changed = true;
        }
        if (from == 0) {
            row[1] = 1;
            changed = true;
        }
        if (to > pages) {
            row[2] = columnIndex == 0 ? -1 : pages;
            changed = true;
        }
        if (to < -pages) {
            row[2] = columnIndex == 0 ? -1 : -pages;
            changed = true;
        }
        if (to == 0) {
            row[2] = -1;
            changed = true;
        }
        if (changed && columnIndex != 0) {
            later([this] {
                messageBox(mainWindow_.handle(), L"Entered value out of range. Value has been adjusted.",
                           L"" PDFTWEAK_PROGRAM_NAME, MB_ICONWARNING);
            });
        }
    } else if (columnIndex == 5) {
        if (std::holds_alternative<std::monostate>(row[5]))
            row[5] = std::vector<int>{0};
    }
}

void InputTab::updateFileName() {
    std::wstring name;
    if (inputFiles_.empty())
        name = L"(No file selected)";
    else if (inputFiles_.size() == 1)
        name = inputFiles_[0]->file().filename().wstring();
    else
        name = L"(" + std::to_wstring(inputFiles_.size()) + L" files selected)";
    setWindowText(filename_, name);
}

void InputTab::addFiles(const std::vector<std::filesystem::path>& files) {
    for (const std::filesystem::path& file : files) {
        std::shared_ptr<pdf::InputFile> input;
        try {
            try {
                input = std::make_shared<pdf::InputFile>(file, "");
            } catch (const Error& error) {
                if (error.kind != Error::Kind::BadPassword)
                    throw;
                const std::optional<std::wstring> password = askForPassword(mainWindow_.handle());
                if (!password)
                    break;
                try {
                    input = std::make_shared<pdf::InputFile>(file, toUtf8(*password));
                } catch (const Error& again) {
                    if (again.kind != Error::Kind::BadPassword)
                        throw;
                    messageBox(mainWindow_.handle(), L"Bad owner password", L"Cannot open input file",
                               MB_ICONWARNING);
                    break;
                }
            }
        } catch (const Error& error) {
            messageBox(mainWindow_.handle(), fromUtf8(error.what()), L"Error reading input file",
                       MB_ICONERROR);
            return;
        }
        inputFiles_.push_back(input);
        if (inputFiles_.size() == 1)
            mainWindow_.setInputFile(input);
        fileCombination_->addRow({input, 1, input->pageCount(), true, true, std::vector<int>{0}});
    }
    if (!isChecked(multiFiles_) && !isChecked(batchProcessing_) && !inputFiles_.empty())
        enable(selectFile_, false);
    if (inputFiles_.size() > 1) {
        if (isChecked(batchProcessing_)) {
            enable(batchProcessing_, false);
        } else {
            setChecked(multiFiles_, true);
            enable(multiFiles_, false);
        }
    }
    updateFileName();
}

std::vector<pdf::InputRange> InputTab::pageRanges() {
    std::vector<pdf::InputRange> ranges;
    for (int i = 0; i < fileCombination_->rowCount(); ++i) {
        const std::vector<Value>& row = fileCombination_->row(i);
        const auto* file = std::get_if<std::shared_ptr<pdf::InputFile>>(&row[0]);
        if (file == nullptr || !*file)
            continue;
        ranges.push_back(
            {*file, formats::makePageRange(0, (*file)->pageCount(), std::get<int>(row[1]),
                                           std::get<int>(row[2]), std::get<bool>(row[3]),
                                           std::get<bool>(row[4]), std::get<std::vector<int>>(row[5]))});
    }
    return ranges;
}

void InputTab::checkRun() {
    fileCombination_->checkRun("input file");
    if (inputFiles_.empty())
        throw Error(Error::Kind::Io, "No input file selected");
    if (isChecked(multiFiles_)) {
        int pagesBefore = 0;
        const bool interleaved = isChecked(interleave_);
        for (const pdf::InputRange& range : pageRanges()) {
            const std::vector<int> pages = range.range.pages(pagesBefore);
            if (pages.empty())
                throw Error(Error::Kind::Io, "At least one input file page range contains no pages");
            if (!interleaved)
                pagesBefore += static_cast<int>(pages.size());
        }
        if (interleaved) {
            try {
                if (javaParseInt(toUtf8(windowText(interleaveSize_))) < 1)
                    throw Error(Error::Kind::Document, "");
            } catch (const Error&) {
                throw Error(Error::Kind::Io, "Invalid interleave block size");
            }
        }
    }
}

Operation InputTab::prepare() {
    const std::vector<std::shared_ptr<pdf::InputFile>> files = inputFiles_;
    const bool useTempFiles = useTempFiles_;
    if (isChecked(batchProcessing_)) {
        return [files, useTempFiles](RunContext& context) {
            const auto& file = files[static_cast<size_t>(context.batchIndex)];
            file->reopen();
            context.tweak = std::make_unique<pdf::Tweak>(file, useTempFiles, context.progress);
        };
    }
    if (isChecked(multiFiles_)) {
        const std::vector<pdf::InputRange> ranges = pageRanges();
        const int interleaveSize =
            isChecked(interleave_) ? javaParseInt(toUtf8(windowText(interleaveSize_))) : 0;
        return [files, ranges, useTempFiles, interleaveSize](RunContext& context) {
            for (const auto& file : files)
                file->reopen();
            context.tweak = std::make_unique<pdf::Tweak>(files[0], ranges, useTempFiles, interleaveSize,
                                                         context.progress);
        };
    }
    return [files, useTempFiles](RunContext& context) {
        files[0]->reopen();
        context.tweak = std::make_unique<pdf::Tweak>(files[0], useTempFiles, context.progress);
    };
}

std::vector<formats::Bookmark> InputTab::loadBookmarks() {
    if (inputFiles_.empty())
        return {};
    if (isChecked(multiFiles_))
        return pdf::buildBookmarks(pageRanges());
    return pdf::readBookmarks(*inputFiles_[0]->qpdf(), 1);
}

int InputTab::batchLength() const {
    return isChecked(batchProcessing_) ? static_cast<int>(inputFiles_.size()) : 1;
}

} // namespace pdftweak::gui
