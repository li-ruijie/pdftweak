#pragma once

// Reading back what an operation produced, as the Java tests do with iText's PdfReader and text
// extraction. Pages are numbered from 1. Every function throws Failure when the file cannot be read.

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

class QPDF;

namespace pdftweak::test {

// The document opened with qpdf, for checks no helper below covers.
std::shared_ptr<QPDF> openPdf(const std::filesystem::path& pdf, const std::string& password = "");

int pageCount(const std::filesystem::path& pdf, const std::string& password = "");

// PdfReader.getPageSize: the MediaBox, inherited if need be, as llx, lly, urx, ury with the lower
// left corner first.
std::array<float, 4> mediaBox(const std::filesystem::path& pdf, int page);

// PdfReader.getPageRotation: the inherited /Rotate, reduced to 0, 90, 180, or 270.
int rotation(const std::filesystem::path& pdf, int page);

// PdfReader.getPageContent: the decoded content streams of the page, joined by newlines.
std::string content(const std::filesystem::path& pdf, int page);

// The matrices of all "cm" operators in the content of a page, in order.
std::vector<std::array<float, 6>> placements(const std::filesystem::path& pdf, int page);

// OperationTests.textPositions: each line of text on a page, with the x of the centre of its
// baseline, the y where the baseline starts when it is horizontal and of its centre otherwise, and
// the angle of the baseline in degrees, all in the user space of the page. MuPDF extracts the text.
struct TextRun {
    std::string text;
    float x = 0;
    float y = 0;
    float angle = 0;
};
std::vector<TextRun> textPositions(const std::filesystem::path& pdf, int page);

// The text of a page, its lines joined by newlines.
std::string text(const std::filesystem::path& pdf, int page);

// OperationTests.tiffTags: the tags of the first image file directory of a TIFF. SHORT and LONG
// values are read as numbers, and other types have no values.
std::map<int, std::vector<long long>> tiffTags(const std::string& tiff);

// RegressionTests.tiffPageCount: the number of image file directories of a TIFF.
int tiffPageCount(const std::string& tiff);

// OperationTests.assertValidSignature: checks that the file has one signature, that its byte ranges
// cover the whole file except the signature itself, and that the CMS signature verifies over them.
// Returns the /SubFilter of the signature, such as "/adbe.pkcs7.detached".
std::string verifySignature(const std::filesystem::path& pdf, const std::string& password = "");

} // namespace pdftweak::test
