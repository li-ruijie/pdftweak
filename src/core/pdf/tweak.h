#pragma once

// Tweak mirrors PdfTweak: it holds the current document, which each operation replaces, and the
// settings that apply only when the output is written. With temporary files on, each new document
// goes to one of two alternating files and is read back from it, otherwise it stays in memory.

#include "core/formats/page_range.h"
#include "core/pdf/input_file.h"
#include "core/progress.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace pdftweak::pdf {

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
};

} // namespace pdftweak::pdf
