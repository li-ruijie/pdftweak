#pragma once

#include <stdexcept>
#include <string>

namespace pdftweak {

// The one exception type of the core. The message is UTF-8 text for the user. The kind mirrors the
// Java exception class that the Java version threw, which decides how a caller reports the error.
struct Error : std::runtime_error {
    enum class Kind {
        Io,          // java.io.IOException
        Document,    // com.itextpdf.text.DocumentException and runtime failures
        BadPassword, // com.itextpdf.text.exceptions.BadPasswordException
    };

    Kind kind;

    Error(Kind kind, const std::string& utf8Message) : std::runtime_error(utf8Message), kind(kind) {}
};

} // namespace pdftweak
