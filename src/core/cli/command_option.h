#pragma once

// CommandOption and the option classes of the Java command line, created in the order of
// CommandLineInterface.buildOptions.

#include "core/cli.h"
#include "core/pdf/input_file.h"
#include "core/pdf/tweak.h"

#include <memory>
#include <string>
#include <vector>

namespace pdftweak::cli {

class CommandOption {
public:
    virtual ~CommandOption() = default;
    virtual bool supportsOption(const std::string& option) const = 0;
    // Returns false after writing the reason to output, which ends the run.
    virtual bool setOption(const std::string& option, const std::string& value, CliOutput& output) = 0;
    virtual void run(pdf::Tweak& tweak, pdf::InputFile& masterFile, CliOutput& output) = 0;
    virtual std::string summary() const = 0;
    virtual std::string help(const std::string& option) const = 0;
    // Whether the option asked for information about the input (-info, -moreinfo, -savebookmarks).
    virtual bool queried() const { return false; }
};

std::unique_ptr<CommandOption> makePreserveHyperlinksOption();
std::unique_ptr<CommandOption> makeCropOption();
std::unique_ptr<CommandOption> makeRotateOption();
std::unique_ptr<CommandOption> makeScaleOption();
std::unique_ptr<CommandOption> makeShuffleOption();
std::unique_ptr<CommandOption> makeWatermarkOptions();
std::unique_ptr<CommandOption> makePageNumberOption();
std::unique_ptr<CommandOption> makeBookmarksOption();
std::unique_ptr<CommandOption> makeAttachOption();
std::unique_ptr<CommandOption> makeTransitionOption();
std::unique_ptr<CommandOption> makeViewerOptions();
std::unique_ptr<CommandOption> makeDocInfoOption();
std::unique_ptr<CommandOption> makeEncryptOptions();
std::unique_ptr<CommandOption> makeInfoOption();

std::vector<std::unique_ptr<CommandOption>> buildOptions();

} // namespace pdftweak::cli
