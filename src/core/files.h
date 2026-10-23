#pragma once

// The file errors of Java on Windows, which jPDF Tweak reported as they came.

#include <filesystem>
#include <string>

namespace pdftweak {

// The system's text for a Windows error code, without the closing full stop, as Java shows it.
std::string systemMessage(unsigned long error);

// File.getCanonicalFile on Windows: throws Error(Io, "Bad pathname") for a path with * or ?, and
// Error(Io, <system message>) when Windows refuses to look the path up, such as for a name with |.
void checkCanonical(const std::filesystem::path& path);

// new FileOutputStream(name): creates or empties the file, or throws Error(Io, "<name> (<system
// message>)") as Java's FileNotFoundException said, with the name as it was given.
void createForWriting(const std::filesystem::path& path, const std::string& name);

} // namespace pdftweak
