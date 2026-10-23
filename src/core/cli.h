#pragma once

#include <span>
#include <string_view>

namespace pdftweak {

// Run the command-line interface on the arguments after the program name and return the process
// exit code. Milestone 1 handles -v and -version only, and milestone 2 ports the full Java CLI.
int runCommandLine(std::span<const std::wstring_view> args);

} // namespace pdftweak
