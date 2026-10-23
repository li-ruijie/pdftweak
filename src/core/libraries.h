#pragma once

#include <string>

namespace pdftweak {

// The versions of the libraries linked into the program, as each library reports them at run time.
struct LibraryVersions {
    std::string qpdf;
    std::string mupdf;
    std::string libtiff;
};

LibraryVersions libraryVersions();

} // namespace pdftweak
