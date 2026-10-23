#pragma once

// Tweak mirrors PdfTweak: it holds the current document, which each operation replaces, and the
// settings that apply only when the output is written. With temporary files on, each new document
// goes to one of two alternating files and is read back from it, otherwise it stays in memory.

#include "core/formats/bookmark.h"
#include "core/formats/page_labels.h"
#include "core/formats/page_range.h"
#include "core/formats/shuffle_rule.h"
#include "core/pdf/input_file.h"
#include "core/progress.h"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pdftweak::pdf {

// PdfTweak.PageBox: the page boxes a crop can use, each falling back to another when a page lacks it.
enum class PageBox { MediaBox, CropBox, BleedBox, TrimBox, ArtBox };

// PdfPageRange: pages of one input file.
struct InputRange {
    std::shared_ptr<InputFile> file;
    formats::PageRange range;
};

class Tweak {
public:
    // The single-file constructor: the input document itself is the current document.
    Tweak(std::shared_ptr<InputFile> single, bool useTempFiles, Progress& progress);

    // The multi-file constructor: a new document of the given page ranges, interleaved in blocks of
    // interleaveSize pages when that is not 0, with the Info and XMP of the first file.
    Tweak(std::shared_ptr<InputFile> first, const std::vector<InputRange>& ranges, bool useTempFiles,
          int interleaveSize, Progress& progress);

    ~Tweak();
    Tweak(const Tweak&) = delete;
    Tweak& operator=(const Tweak&) = delete;

    int pageCount() const;
    QPDF& current() { return *current_; }

    // cropPages: each page cut to the given box (or its fallback), keeping its /Rotate.
    void cropPages(PageBox cropTo);

    // rotatePages: /Rotate turned by 90 degrees count times, separately for portrait and landscape.
    void rotatePages(int portraitCount, int landscapeCount);

    // removeRotation: each rotated page drawn upright on a page of its rotated size.
    void removeRotation();

    // scalePages: each page drawn on a page of the new size, scaled to fit (not above 1 with
    // noEnlarge, and with equal factors with preserveAspectRatio) and centred.
    void scalePages(float newWidth, float newHeight, bool noEnlarge, bool preserveAspectRatio);

    // shufflePages: pages placed by the rules of a rule set, in passes over blocks.
    void shufflePages(const formats::RuleSet& rules);

    // preserveHyperlinks: the page operations copy link annotations from now on.
    void preserveHyperlinks() { preserveHyperlinks_ = true; }

    // addWatermark: the first page of wmFile under each page, wmText at wmSize points across each
    // page diagonally with the fill opacity and colour, and page numbers at position pnPosition (0
    // to 8, the bottom row first, left to right, or -1 for none) at pnSize points, pnHOff and pnVOff
    // points from the edges, swapped left and right on even pages with pnFlipEven. A mask that is
    // not empty formats each number with String.format from the page, the page count, the logical
    // page, and the page label. Throws Error(Io, "Page number X is not within page bounding box")
    // when a number would cross the edge it is placed at.
    void addWatermark(const InputFile* wmFile, const std::optional<std::string>& wmText, int wmSize,
                      float wmOpacity, const std::optional<std::array<int, 3>>& wmColor, int pnPosition,
                      bool pnFlipEven, int pnSize, float pnHOff, float pnVOff, const std::string& mask);

    // updateBookmarks: the outline replaced by the bookmarks, depth first with the top level at
    // depth 1, or removed when there are none. Throws Error(Io, "Invalid depth value") when a
    // bookmark is not at its parent's depth plus 1.
    void updateBookmarks(const std::vector<formats::Bookmark>& bookmarks);

    // setPageNumbers: the /PageLabels of the document, decimal from page 1 unless a format starts at
    // page 1. Throws Error(Document) for a physical or logical page below 1.
    void setPageNumbers(const std::vector<formats::PageLabelFormat>& labelFormats);

    // setTransition: for page (1-based), the transition type (an index of formats::transitionNames,
    // 0 for none) of tduration seconds, and the display duration pduration (-1 for none). Applied
    // when the output is written.
    void setTransition(int page, int type, int tduration, int pduration);

    // setViewerPreferences: the page layout and page mode bits of iText's PdfWriter, and the
    // optional preferences as (key, value) in the spelling of formats::viewerPreferences. Applied
    // when the output is written, replacing the document's own.
    void setViewerPreferences(int simplePrefs,
                              const std::vector<std::pair<std::string, std::string>>& optionalPrefs);

    // addFile: a file embedded when the output is written.
    void addFile(const std::filesystem::path& file) { attachments_.push_back(file); }

    // updateInfoDictionary: sets each (key, value) in the Info dictionary, in the given order, where
    // an empty value removes the key, and removes the XMP metadata.
    void updateInfoDictionary(const std::vector<std::pair<std::string, std::string>>& newInfo);

    // setEncryption: the mode of EncryptOptions (0 to 3, plus 8 for unencrypted metadata), the
    // permission bits, and the passwords as Latin-1 bytes. An empty owner password is refused.
    void setEncryption(int mode, int permissions, const std::string& ownerPassword,
                       const std::string& userPassword);

    // addPageMarks and removePageMarks: the /pdftk_PageNum entry of each page.
    void addPageMarks();
    void removePageMarks();

    // writeOutput: resolves <F>, <FX>, <P>, and <#> in the name, then writes the output, one file
    // per page when burst is set, and optimised for size, uncompressed, or with object streams.
    void writeOutput(std::string outputFile, bool multipageTiff, bool burst, bool uncompressed,
                     bool sizeOptimize, bool fullyCompressed);

    // cleanup: deletes the temporary files.
    void cleanup();

private:
    // Makes next the current document, through memory or the next temporary file.
    void replaceCurrent(QPDF& next);
    void checkCancelled() const;
    void configureWriter(class QPDFWriter& writer, bool uncompressed, bool fullyCompressed) const;
    void finishInformation(QPDF& pdf) const;
    void applyOutputSettings(QPDF& pdf) const;

    Progress& progress_;
    std::shared_ptr<QPDF> current_;
    // the order of the Info keys of the current document, shared with the input file it came from
    std::shared_ptr<std::vector<std::string>> infoOrder_;
    std::filesystem::path tempfile1_;
    std::filesystem::path tempfile2_;
    std::vector<std::filesystem::path> inputFiles_;
    std::wstring inputFilePath_;
    std::wstring inputFileName_;
    std::wstring inputFileFullName_;
    int encryptionMode_ = -1;
    int encryptionPermissions_ = -1;
    std::string userPassword_;
    std::string ownerPassword_;
    bool preserveHyperlinks_ = false;
    // type, transition duration, and page duration of each page, empty until a transition is set
    std::vector<std::array<int, 3>> transitionValues_;
    int simpleViewerPreferences_ = 0;
    std::optional<std::vector<std::pair<std::string, std::string>>> optionalViewerPreferences_;
    std::vector<std::filesystem::path> attachments_;
};

} // namespace pdftweak::pdf
