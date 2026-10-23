#pragma once

#include "core/progress.h"

#include <span>
#include <string_view>

namespace pdftweak {

// Where the command line writes, as UTF-8. Java's System.out and System.err.
class CliOutput {
public:
    virtual ~CliOutput() = default;
    virtual void out(std::string_view utf8) = 0;
    virtual void err(std::string_view utf8) = 0;
};

// CommandLineInterface: runs the arguments after the program name and returns the exit code, 0 on
// success and 1 on failure (decisions 9 and 14). The first form writes to the standard handles.
int runCommandLine(std::span<const std::wstring_view> args);
int runCommandLine(std::span<const std::wstring_view> args, CliOutput& output, Progress& progress);

} // namespace pdftweak
