#include "cli_run.h"

#include "fixtures.h"

#include "core/cli.h"
#include "core/text.h"

#include <string_view>

namespace pdftweak::test {
namespace {

class CaptureOutput final : public CliOutput {
public:
    void out(std::string_view utf8) override { out_ += utf8; }
    void err(std::string_view utf8) override { err_ += utf8; }
    std::string out_;
    std::string err_;
};

} // namespace

Arg::Arg(const char* utf8) : text(fromUtf8(utf8)) {}
Arg::Arg(const std::string& utf8) : text(fromUtf8(utf8)) {}

CliResult runCliCapture(const std::vector<Arg>& args) {
    std::vector<std::wstring_view> views;
    for (const Arg& arg : args)
        views.push_back(arg.text);
    CaptureOutput output;
    NullProgress progress;
    CliResult result;
    result.exitCode = runCommandLine(views, output, progress);
    result.out = output.out_;
    result.err = output.err_;
    return result;
}

void runCli(const std::vector<Arg>& args) {
    const CliResult result = runCliCapture(args);
    if (result.exitCode == 0)
        return;
    std::string text = result.err;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.pop_back();
    const size_t lineStart = text.find_last_of('\n');
    std::string message = lineStart == std::string::npos ? text : text.substr(lineStart + 1);
    if (message.rfind("Error: ", 0) == 0)
        message = message.substr(7);
    throw CliFailure("command line failed with exit code " + std::to_string(result.exitCode) + ": " +
                         result.err,
                     message);
}

std::string cliError(const std::vector<Arg>& args) {
    try {
        runCli(args);
    } catch (const CliFailure& e) {
        return e.message;
    }
    return "no error";
}

std::string help(const std::string& option) { return runCliCapture({"-help", option}).out; }

std::string info(const std::filesystem::path& pdf, const std::string& option) {
    static int count = 0;
    const std::filesystem::path file = "info" + std::to_string(++count) + ".txt";
    runCli({pdf, option, file});
    return readFile(file);
}

} // namespace pdftweak::test
