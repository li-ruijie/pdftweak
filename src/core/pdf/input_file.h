#pragma once

// PdfInputFile: one input PDF, opened with the owner password, as the command line and the Input
// tab give it.

#include "core/formats/page_labels.h"
#include "core/pdf/qpdf_util.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pdftweak::pdf {

class InputFile {
public:
    // Opens the file with the password (UTF-8, converted to Latin-1 as Java did). Throws
    // Error(BadPassword, "PdfReader not opened with owner password") for an encrypted file that the
    // password does not open with full permissions, and "Bad user password" when it opens nothing.
    InputFile(std::filesystem::path file, const std::string& password);

    const std::filesystem::path& file() const { return file_; }
    const std::shared_ptr<QPDF>& qpdf() const { return pdf_; }

    int pageCount() const;

    // PdfInputFile.getPageSize: the size of a page (1-based) with its rotation applied.
    Rect pageSize(int page) const;

    // PdfInputFile.getInfoDictionary: the document information as (key, value) in UTF-8, in the
    // order Java's HashMap iterates them, with the Producer marked as modified by PDF Tweak.
    std::vector<std::pair<std::string, std::string>> infoDictionary() const;

    // PdfReader.getCryptoMode: -1 when not encrypted, otherwise 0 (RC4 40-bit), 1 (RC4 128-bit),
    // 2 (AES 128-bit), or 3 (AES 256-bit), plus 8 when revision 4 or later leaves metadata
    // unencrypted.
    int cryptoMode() const;
    bool metadataEncrypted() const;

    // PdfInputFile.getPermissions: the /P value, with the bits that 40-bit encryption cannot express
    // cleared.
    int permissions() const;

    std::string ownerPassword() const { return ownerPassword_; }
    std::string userPassword() const;

    // PdfPageLabels.getPageLabelFormats: the page label number tree in page order, or nothing when
    // the document has no /PageLabels.
    std::optional<std::vector<formats::PageLabelFormat>> pageLabels() const;

private:
    std::filesystem::path file_;
    std::string ownerPassword_;
    std::shared_ptr<QPDF> pdf_;
    std::shared_ptr<std::vector<std::string>> infoOrder_;

    friend class Tweak;
};

// The document information as getInfoDictionary reports it, with the keys of the Info dictionary
// first taken in the given order (names with their slash), as iText's dictionary keeps the order
// of the file and of later additions.
std::vector<std::pair<std::string, std::string>> readInfoDictionary(QPDF& pdf,
                                                                    const std::vector<std::string>& order);

// The page label formats of a document (see InputFile::pageLabels).
std::optional<std::vector<formats::PageLabelFormat>> readPageLabels(QPDF& pdf);

} // namespace pdftweak::pdf
