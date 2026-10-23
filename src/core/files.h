#pragma once

// The file errors of Java on Windows, which jPDF Tweak reported as they came.

#include <filesystem>
#include <string>
#include <vector>

namespace pdftweak {

// The system's text for a Windows error code, without the closing full stop, as Java shows it.
std::string systemMessage(unsigned long error);

// File.getCanonicalFile on Windows: throws Error(Io, "Bad pathname") for a path with * or ?, and
// Error(Io, <system message>) when Windows refuses to look the path up, such as for a name with |.
void checkCanonical(const std::filesystem::path& path);

// new FileOutputStream(name): creates or empties the file, or throws Error(Io, "<name> (<system
// message>)") as Java's FileNotFoundException said, with the name as it was given.
void createForWriting(const std::filesystem::path& path, const std::string& name);

// BufferedReader.readLine over a file read as UTF-8: lines end at \n, \r, or \r\n, and a last line
// without an end counts when it is not empty. A missing file throws Error(Io) as FileInputStream does.
std::vector<std::string> readLines(const std::string& file);

} // namespace pdftweak
