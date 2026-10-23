#include "core/libraries.h"

#include <mupdf/fitz.h>
#include <qpdf/QPDF.hh>
#include <tiffio.h>

#include <stdexcept>

namespace pdftweak {

LibraryVersions libraryVersions() {
    LibraryVersions v;
    v.qpdf = QPDF::QPDFVersion();

    // fz_new_context returns null when the headers and the library differ in version.
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (ctx == nullptr)
        throw std::runtime_error("MuPDF headers and library differ in version");
    fz_drop_context(ctx);
    v.mupdf = FZ_VERSION;

    // TIFFGetVersion returns "LIBTIFF, Version 4.7.2" followed by a copyright notice.
    const std::string tiff = TIFFGetVersion();
    const std::string marker = "Version ";
    const size_t start = tiff.find(marker);
    if (start == std::string::npos)
        throw std::runtime_error("Unexpected libtiff version text: " + tiff);
    const size_t end = tiff.find_first_of("\r\n", start);
    v.libtiff = tiff.substr(start + marker.size(), end - start - marker.size());
    return v;
}

} // namespace pdftweak
