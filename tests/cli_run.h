#pragma once

// Running the command line in process, as the Java tests call CommandLineInterface.

#include "runner.h"

#include <filesystem>
#include <string>
#include <vector>

namespace pdftweak::test {

// One argument, from a narrow (UTF-8) string, a wide string, or a path.
struct Arg {
    std::wstring text;
    Arg(const char* utf8);
    Arg(const std::string& utf8);
    Arg(const wchar_t* wide) : text(wide) {}
    Arg(const std::filesystem::path& path) : text(path.wstring()) {}
};

struct CliResult {
    int exitCode = 0;
    std::string out;
    std::string err;
};

CliResult runCliCapture(const std::vector<Arg>& args);

// A run that exited with a code other than 0. message is the last line of standard error without
// "Error: ", the message of the exception Java would have thrown.
struct CliFailure : Failure {
    std::string message;
    CliFailure(const std::string& what, std::string message) : Failure(what), message(std::move(message)) {}
};

// RegressionTests.cli: runs the arguments and throws CliFailure when the exit code is not 0.
void runCli(const std::vector<Arg>& args);

// The message runCli fails with, or "no error".
std::string cliError(const std::vector<Arg>& args);

// RegressionTests.help: the standard output of -help option.
std::string help(const std::string& option);

// OperationTests.info: the text -info, -moreinfo, or -savebookmarks writes about a PDF.
std::string info(const std::filesystem::path& pdf, const std::string& option);

} // namespace pdftweak::test
