#pragma once

// Fixture PDFs, written with qpdf to look as the iText fixtures of the Java tests do: each page shows
// its text in Helvetica 12 with the first baseline 54 points below the top, at x = 36.

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace pdftweak::test {

struct FixturePage {
    float width = 595;
    float height = 842;
    std::string text; // UTF-8, shown in WinAnsiEncoding
};

struct FixtureOptions {
    int linkPage = 0; // a page with an internal link to the top-left corner of linkTarget, or 0
    int linkTarget = 0;
    // Document information as (key without slash, UTF-8 value), added to the Producer, CreationDate,
    // and ModDate that iText writes.
    std::vector<std::pair<std::string, std::string>> info;
    bool xmp = false;       // an XMP metadata stream, as PdfWriter.createXmpMetadata adds
    std::string xmpPacket;  // the packet of that stream, or empty for a plain one
    bool textField = false; // a text field "name" with the value "value" at [100 700 300 720] on page 1
};

std::filesystem::path writePdf(const std::filesystem::path& dir, const std::string& name,
                               const std::vector<FixturePage>& pages, const FixtureOptions& options = {});

// RegressionTests.fixture: A4 pages saying "Page n", with an optional link from linkPage whose /Dest
// is /XYZ 0 842 0 on linkTarget.
std::filesystem::path fixture(const std::filesystem::path& dir, const std::string& name, int pages,
                              int linkPage = 0, int linkTarget = 0);

// OperationTests.pdf: one page of each size, each saying "Page n".
std::filesystem::path pdfOfSizes(const std::filesystem::path& dir, const std::string& name,
                                 const std::vector<std::pair<float, float>>& sizes);

// OperationTests.pdfWithText: one A4 page showing text.
std::filesystem::path pdfWithText(const std::filesystem::path& dir, const std::string& name,
                                  const std::string& text);

// OperationTests.pdfWithPages: A4 pages saying prefix1, prefix2, and so on.
std::filesystem::path pdfWithPages(const std::filesystem::path& dir, const std::string& name,
                                   const std::string& prefix, int pages);

// OperationTests.withPageEntries: copies in to out with each (key, value in PDF syntax) set in the
// dictionary of one page, such as {"/Rotate", "90"}.
std::filesystem::path setPageEntries(const std::filesystem::path& in, const std::filesystem::path& out,
                                     int page,
                                     const std::vector<std::pair<std::string, std::string>>& entries);

// OperationTests.keystore, as PKCS#12: an RSA 2048 key with a self-signed certificate under the
// friendly name "mykey", protected by the password "secret".
std::filesystem::path pkcs12(const std::filesystem::path& dir);

std::string readFile(const std::filesystem::path& path);
void writeFile(const std::filesystem::path& path, const std::string& bytes);

} // namespace pdftweak::test
