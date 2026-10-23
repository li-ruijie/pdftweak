#pragma once

// The eleven tabs of the main window, in the order of MainForm. Each ports the Java class of the same
// name in jpdftweak.gui.tabs. The accessors that return controls serve the gui tests, as the Java
// tests read the fields of the tabs through reflection.

#include "core/formats/bookmark.h"
#include "core/formats/page_labels.h"
#include "core/formats/shuffle_rule.h"
#include "gui/preview.h"
#include "gui/tab.h"
#include "gui/table_editor.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace pdftweak::gui {

// A tab that only names itself, as DummyTab, until its port takes its place.
class PlaceholderTab final : public Tab {
public:
    PlaceholderTab(MainWindow& mainWindow, const wchar_t* name) : Tab(mainWindow), name_(name) {}
    const wchar_t* name() const override { return name_; }
    Operation prepare() override { return {}; }

private:
    void build() override {
        FormLayout& layout = newLayout(L"f:p:g", L"f:p:g");
        layout.add(label(L"Not implemented yet."), 1, 1);
        setLayout(layout.asComponent());
    }
    const wchar_t* name_;
};

class InputTab final : public Tab {
public:
    explicit InputTab(MainWindow& mainWindow) : Tab(mainWindow) {}
    const wchar_t* name() const override { return L"Input"; }
    void checkRun() override;
    Operation prepare() override;

    // The bookmarks of the input, for the Load button of the Bookmarks tab.
    std::vector<formats::Bookmark> loadBookmarks();
    int batchLength() const;
    void setUseTempFiles(bool useTempFiles) { useTempFiles_ = useTempFiles; }

    // What Select... does with the files chosen.
    void addFiles(const std::vector<std::filesystem::path>& files);

    HWND filename() const { return filename_; }
    HWND selectButton() const { return selectFile_; }
    HWND clearButton() const { return clear_; }
    HWND multiFiles() const { return multiFiles_; }
    HWND interleave() const { return interleave_; }
    HWND interleaveSize() const { return interleaveSize_; }
    HWND batchProcessing() const { return batchProcessing_; }
    TableEditor& fileCombination() { return *fileCombination_; }

private:
    void build() override;
    void updateEnabledState();
    void updateFileName();
    void rowChanged(int row, int column);
    std::vector<pdf::InputRange> pageRanges();

    HWND filename_ = nullptr;
    HWND selectFile_ = nullptr;
    HWND clear_ = nullptr;
    HWND multiFiles_ = nullptr;
    HWND interleave_ = nullptr;
    HWND interleaveSize_ = nullptr;
    HWND batchProcessing_ = nullptr;
    std::unique_ptr<TableEditor> fileCombination_;
    std::vector<std::shared_ptr<pdf::InputFile>> inputFiles_;
    bool useTempFiles_ = false;
};

class PageSizeTab final : public Tab {
public:
    explicit PageSizeTab(MainWindow& mainWindow) : Tab(mainWindow) {}
    const wchar_t* name() const override { return L"Page Size"; }
    Operation prepare() override;

    HWND cropPages() const { return cropPages_; }
    HWND cropTo() const { return cropTo_; }
    HWND rotatePages() const { return rotatePages_; }
    HWND rotatePortrait() const { return rotatePortrait_; }
    HWND rotateLandscape() const { return rotateLandscape_; }
    HWND fixRotation() const { return fixRotation_; }
    HWND scalePages() const { return scalePages_; }
    HWND scaleSize() const { return scaleSize_; }
    HWND scaleWidth() const { return scaleWidth_; }
    HWND scaleHeight() const { return scaleHeight_; }
    HWND scaleCenter() const { return scaleCenter_; }
    HWND scaleNoPreserve() const { return scaleNoPreserve_; }
    HWND preserveHyperlinks() const { return preserveHyperlinks_; }

private:
    void build() override;
    void updateScaleSize();

    HWND cropPages_ = nullptr;
    HWND cropTo_ = nullptr;
    HWND rotatePages_ = nullptr;
    HWND rotatePortrait_ = nullptr;
    HWND rotateLandscape_ = nullptr;
    HWND fixRotation_ = nullptr;
    HWND scalePages_ = nullptr;
    HWND scaleSize_ = nullptr;
    HWND scaleWidth_ = nullptr;
    HWND scaleHeight_ = nullptr;
    HWND scaleCenter_ = nullptr;
    HWND scaleNoPreserve_ = nullptr;
    HWND preserveHyperlinks_ = nullptr;
};

// The table of page number ranges of the Page Numbers and Watermark tabs, and the actions on it that
// PageNumberTab shares with WatermarkTab.
namespace page_numbers {

// buildPageNumberRanges
std::unique_ptr<TableEditor> buildRanges();
// updatePageNumberRanges, without the call to setPageNumbers: the label formats of the table.
// Throws Error(Io) for a start page or logical page below 1.
std::vector<formats::PageLabelFormat> formats(TableEditor& ranges);
// PageNumberLoadAction
void load(MainWindow& mainWindow, TableEditor& ranges);

} // namespace page_numbers

class WatermarkTab final : public Tab {
public:
    explicit WatermarkTab(MainWindow& mainWindow) : Tab(mainWindow) {}
    const wchar_t* name() const override { return L"Watermark"; }
    void checkRun() override;
    Operation prepare() override;

    HWND pdfWatermark() const { return pdfWatermark_; }
    HWND filename() const { return filename_; }
    HWND fileButton() const { return fileButton_; }
    HWND textWatermark() const { return textWatermark_; }
    HWND watermarkText() const { return watermarkText_; }
    HWND watermarkSize() const { return watermarkSize_; }
    HWND watermarkOpacity() const { return watermarkOpacity_; }
    HWND watermarkUseColor() const { return watermarkUseColor_; }
    HWND watermarkColor() const { return watermarkColor_; }
    COLORREF color() const { return color_; }
    void setColor(COLORREF color);
    HWND pageNumbers() const { return pageNumbers_; }
    HWND pgnoSize() const { return pgnoSize_; }
    HWND pgnoHOffset() const { return pgnoHOffset_; }
    HWND pgnoHRef() const { return pgnoHRef_; }
    HWND pgnoVOffset() const { return pgnoVOffset_; }
    HWND pgnoVRef() const { return pgnoVRef_; }
    HWND useMask() const { return useMask_; }
    HWND maskText() const { return maskText_; }
    HWND differentPageNumbers() const { return differentPageNumbers_; }
    HWND load() const { return load_; }
    TableEditor& pageNumberRanges() { return *pageNumberRanges_; }

private:
    void build() override;
    LRESULT message(UINT msg, WPARAM wParam, LPARAM lParam) override;
    void updatePdfWatermarkEnabled();
    void updateTextWatermarkEnabled();
    void updatePageNumbersEnabled();

    HWND pdfWatermark_ = nullptr;
    HWND filename_ = nullptr;
    HWND fileButton_ = nullptr;
    HWND textWatermark_ = nullptr;
    HWND watermarkText_ = nullptr;
    HWND watermarkSize_ = nullptr;
    HWND watermarkOpacity_ = nullptr;
    HWND watermarkUseColor_ = nullptr;
    HWND watermarkColor_ = nullptr;
    COLORREF color_ = RGB(0, 0, 0);
    HWND pageNumbers_ = nullptr;
    HWND pgnoSize_ = nullptr;
    HWND pgnoHOffset_ = nullptr;
    HWND pgnoHRef_ = nullptr;
    HWND pgnoVOffset_ = nullptr;
    HWND pgnoVRef_ = nullptr;
    HWND useMask_ = nullptr;
    HWND maskText_ = nullptr;
    HWND differentPageNumbers_ = nullptr;
    HWND load_ = nullptr;
    std::unique_ptr<TableEditor> pageNumberRanges_;
};

class ShuffleTab final : public Tab {
public:
    explicit ShuffleTab(MainWindow& mainWindow) : Tab(mainWindow) {}
    const wchar_t* name() const override { return L"Shuffle/N-up"; }
    void checkRun() override;
    Operation prepare() override;

    HWND shufflePages() const { return shufflePages_; }
    HWND preset() const { return preset_; }
    HWND configString() const { return configString_; }
    HWND use() const { return use_; }
    HWND pagesPerPass() const { return pagesPerPass_; }
    HWND updatePreview() const { return updatePreview_; }
    HWND blockShuffle() const { return blockShuffle_; }
    HWND blockSize() const { return blockSize_; }
    HWND previewFormat() const { return previewFormat_; }
    TableEditor& shuffleRulesTable() { return *shuffleRulesTable_; }

private:
    void build() override;
    void parseGui();
    void parseConfigString();
    void updateEnabledState();

    HWND shufflePages_ = nullptr;
    HWND preset_ = nullptr;
    HWND configString_ = nullptr;
    HWND use_ = nullptr;
    HWND pagesPerPass_ = nullptr;
    HWND updatePreview_ = nullptr;
    HWND blockShuffle_ = nullptr;
    HWND blockSize_ = nullptr;
    HWND previewFormat_ = nullptr;
    Preview preview_;
    std::unique_ptr<TableEditor> shuffleRulesTable_;
    int shufflePagesPerPass_ = 0;
    std::vector<formats::ShuffleRule> shuffleRules_;
};

class PageNumberTab final : public Tab {
public:
    explicit PageNumberTab(MainWindow& mainWindow) : Tab(mainWindow) {}
    const wchar_t* name() const override { return L"Page Numbers"; }
    void checkRun() override;
    Operation prepare() override;

    HWND changePageNumbers() const { return changePageNumbers_; }
    HWND load() const { return load_; }
    TableEditor& pageNumberRanges() { return *pageNumberRanges_; }

private:
    void build() override;
    void updateEnabledState();

    HWND changePageNumbers_ = nullptr;
    HWND load_ = nullptr;
    std::unique_ptr<TableEditor> pageNumberRanges_;
};

} // namespace pdftweak::gui
