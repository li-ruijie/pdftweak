#pragma once

// Helpers over qpdf shared by the document code: opening and writing documents, page geometry as
// iText's PdfReader reports it, and the translation of library exceptions into pdftweak::Error.

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace pdftweak::pdf {

// A rectangle with its lower left corner first, as iText's Rectangle.
struct Rect {
    float llx = 0;
    float lly = 0;
    float urx = 0;
    float ury = 0;

    float width() const { return urx - llx; }
    float height() const { return ury - lly; }
};

// The UTF-8 form of a path, which qpdf takes for file names on Windows.
std::string utf8Path(const std::filesystem::path& path);

// Opens a PDF file with recovery and with warnings suppressed. The password is given as the bytes
// Java's getBytes("ISO-8859-1") produced. A missing file gives iText's message, "<absolute path>
// not found as file or resource.".
std::shared_ptr<QPDF> openFile(const std::filesystem::path& path, const std::string& password);

// Opens a PDF held in memory, which the returned object owns.
std::shared_ptr<QPDF> openBytes(std::string bytes, const std::string& description);

// Writes a document unencrypted with qpdf's defaults, as every intermediate document is written.
std::string writeBytes(QPDF& pdf);

std::vector<QPDFPageObjectHelper> pages(QPDF& pdf);

// PdfReader.getNormalizedRectangle: the rectangle of a four-number array, corners ordered.
Rect rectangle(QPDFObjectHandle array);

// PdfReader.getPageSize: the MediaBox of a page, inherited if need be.
Rect pageSize(QPDFObjectHandle page);

// PdfReader.getPageRotation: the inherited /Rotate reduced to 0 to 359.
int pageRotation(QPDFObjectHandle page);

// PdfReader.getPageSizeWithRotation: the page size turned by the rotation of the page.
Rect pageSizeWithRotation(QPDFObjectHandle page);

// Copies an object of another document into to: indirect objects through copyForeignObject, which
// maps each one once per destination, and direct dictionaries and arrays element by element.
QPDFObjectHandle copyFrom(QPDF& to, QPDFObjectHandle object);

// A number as iText writes it (ByteBuffer.formatDouble), as a qpdf object.
QPDFObjectHandle itextNumberObject(double value);

// Inside a catch block, rethrows the current exception as pdftweak::Error. Errors and Cancelled pass
// unchanged, qpdf's password failure becomes iText's "Bad user password", and other exceptions keep
// their message.
[[noreturn]] void rethrowAsError();

} // namespace pdftweak::pdf
