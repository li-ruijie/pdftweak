#pragma once

// Every MuPDF call of the core lives in mupdf_bridge.cpp (spec section 4.4). MuPDF reports errors
// with setjmp and longjmp, so each call sequence sits in a function whose fz_try block holds no
// object with a destructor, and the error becomes a pdftweak::Error after fz_catch.

#include <string>

namespace pdftweak::mupdf {

// "Optimise size": writes the PDF again with MuPDF's garbage collection at level 3, which renumbers
// objects and merges duplicates, as PdfSmartCopy did in Java.
std::string optimize(const std::string& pdf);

} // namespace pdftweak::mupdf
