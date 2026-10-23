#pragma once

#include <filesystem>
#include <string>

namespace pdftweak::test {

struct ProcessResult {
    unsigned long exitCode = 0;
    std::string out;
    std::string err;
};

// Run program with the given argument string (already quoted as a command line) and capture its
// standard output and standard error through pipes.
ProcessResult runProcess(const std::filesystem::path& program, const std::wstring& arguments);

} // namespace pdftweak::test
