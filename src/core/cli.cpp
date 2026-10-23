// CommandLineInterface: parses the Java command-line syntax, opens the inputs, runs the option
// blocks in order, and writes the output.

#include "core/cli.h"

#include "core/cli/command_option.h"
#include "core/console.h"
#include "core/error.h"
#include "core/formats/integer_list.h"
#include "core/java_text.h"
#include "core/pdf/tweak.h"
#include "core/text.h"
#include "core/version.h"

#include <map>
#include <regex>

namespace pdftweak {
namespace {

using Block = std::vector<std::unique_ptr<cli::CommandOption>>;

class ConsoleOutput final : public CliOutput {
public:
    void out(std::string_view utf8) override { writeOut(utf8); }
    void err(std::string_view utf8) override { writeErr(utf8); }
};

void showHelp(CliOutput& output) {
    std::string text = PDFTWEAK_PROGRAM_NAME
        " " PDFTWEAK_VERSION_STRING "\n\n"
        "Usage: pdftweak {inputfile} [-o[opt]] {outputfile}\n"
        "       pdftweak -i[opt] {inputfile} [...] [-o[opt]] {outputfile}\n"
        "       pdftweak -o[opt] {outputfile} [-i[opt]] {inputfile} [...]\n"
        "\n"
        "The first syntax handles a single input file, the second and third one use\n"
        "multiple file input. The third syntax is especially useful if you want to use\n"
        "wildcards (like *.pdf) for the input file.\n"
        "You may add options and transformation and their parameter everywhere in the\n"
        "commandline, except between a -i/-o switch and the filename.\n"
        "The order of tranformations is the one printed below. To change the order\n"
        "or to perform a transformation more than once, separate transformation\n"
        "block by two dashes (--).\n"
        "Parameters are case sensitive.\n\n"
        "Parameters\n"
        "~~~~~~~~~~\n"
        " -help                   Show this help.\n"
        " -help {transformation}  Show help for a transformation\n"
        " -v[ersion]              Show version\n"
        " -password {password}    Use password for opening next input file\n"
        " -interleave {pages}     Interleave documents in blocks of {pages}\n"
        " -i[{options}]           next parameter is input file, see '-help -i'\n"
        " -i={ALIAS}              use ALIAS for next input file name, '-help -i'\n"
        " -o[{options}]           next parameter is output file, see '-help -o'\n"
        "\n"
        "Transformations\n"
        "~~~~~~~~~~~~~~~\r\n";
    for (const auto& option : cli::buildOptions())
        text += option->summary();
    output.out(text);
}

void showHelpFor(std::string option, CliOutput& output) {
    if (option.rfind('-', 0) != 0)
        option = "-" + option;
    if (option == "-i") {
        output.out("\n"
                   "Input Files\n"
                   "~~~~~~~~~~~\n"
                   "If you use -i without options, or no -i parameters at all, all pages of the\n"
                   "given PDF file will be loaded. You can also use the following terms:\n"
                   " -i3                       Load page 3 only\n"
                   " -i~2                      Load the page before the last page only\n"
                   " -i3-9                     Load pages 3-9\n"
                   " -i5-                      Load pages 5-end\n"
                   " -ie                       Load even pages only\n"
                   " -i+3                      Add 3 empty pages to the beginning\n"
                   " -i+5,6                    Add 5 or 6 empty pages depending whether the current\n"
                   "                           position is odd or even\n"
                   " -i1-~3o+1                 Load all but the last two pages, only odd pages, \n"
                   "                           and add one empty page\n"
                   "You can load the file to an alias name using -i=ALIAS and use that alias name\n"
                   "later; this is useful if you want to reference a file more than once and it has\n"
                   "a long path/filename, or it requires a password. If you know 'pdftk', you know\n"
                   "aliases as well, only that they are optional in PDF Tweak and not limited to a\n"
                   "single character.\n\n"
                   "Example:\n"
                   "  pdftweak -o mixedmode.pdf -password secret -i=A secret.pdf \\\n"
                   "            -password moresecret -i=Ox knox.pdf \\\n"
                   "            -i Ox -i1-3 A -i Ox -i4- A -i Ox\n"
                   "This will take knox.pdf, then page 1-3 of secret.pdf, then knox.pdf again,\n"
                   "then the rest of secret.pdf and finally knox.pdf again.\r\n");
    } else if (option == "-o") {
        output.out("\n"
                   "Output files\n"
                   "~~~~~~~~~~~~\n"
                   "Use -o to specify the output filename; you can add flags:\n"
                   "  -ou  save uncompressed, -ob burst pages,\n"
                   "  -om  add PdfTk page marks with -ou, remove them without,\n"
                   "  -oi  export as multipage TIFF image,\n"
                   "  -ot  use temp files,  -os optimize for size,\n"
                   "  -oc use better compression (Acrobat 6.0). \n"
                   "Flags can be combined like -oub.\r\n");
    } else {
        output.out("\r\n");
        for (const auto& o : cli::buildOptions()) {
            if (o->supportsOption(option)) {
                output.out(o->help(option) + "\r\n");
                return;
            }
        }
        output.out("No help found for option " + option +
                   ".\n"
                   "Use -help without option to get a list of options.\r\n");
    }
}

// The body of CommandLineInterface's constructor. Returns the exit code.
int run(const std::vector<std::string>& args, CliOutput& output, Progress& progress) {
    std::shared_ptr<pdf::InputFile> input;
    std::vector<pdf::InputRange> pageRanges;
    std::map<std::string, std::shared_ptr<pdf::InputFile>> aliases;
    std::optional<std::string> outputFile;
    bool burstOutput = false, multipageTiff = false, uncompressedOutput = false, markedOutput = false,
         sizeOptimize = false, fullyCompress = false, useTempFiles = false;
    std::string password;
    int interleaveSize = 0;
    std::vector<Block> options;

    static const std::regex inputOption(R"(-i((~?[0-9]+)(-(~?[0-9]+)?)?)?([eo]?)(\+[0-9,]+)?)");
    static const std::regex outputOption(R"(-o[mubistc]*)");
    const auto open = [&](const std::string& name) {
        return std::make_shared<pdf::InputFile>(std::filesystem::path(fromUtf8(name)), password);
    };
    const auto range = [](const std::shared_ptr<pdf::InputFile>& file, int from, int to, bool odd,
                          bool even, std::vector<int> emptyBefore) {
        return pdf::InputRange{file, formats::makePageRange(0, file->pageCount(), from, to, odd, even,
                                                            std::move(emptyBefore))};
    };
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "-help" || arg == "-?" || arg == "/?") {
            if (i == args.size() - 1)
                showHelp(output);
            else
                showHelpFor(args[i + 1], output);
            return 0;
        } else if (arg == "-v" || arg == "-version") {
            output.out(PDFTWEAK_PROGRAM_NAME " " PDFTWEAK_VERSION_STRING "\r\n");
            return 0;
        } else if (arg == "--") {
            options.push_back(cli::buildOptions());
        } else if (arg.rfind('-', 0) == 0) {
            if (i == args.size() - 1) {
                output.err("Option without parameters: " + arg + "\r\n");
                return 1;
            }
            const std::string& value = args[i + 1];
            std::smatch m;
            if (arg == "-password") {
                password = value;
            } else if (arg == "-interleave") {
                interleaveSize = javaParseInt(value);
            } else if (std::regex_match(arg, m, inputOption)) {
                int from = 1, to = -1;
                if (m[2].matched) {
                    std::string first = m[2].str();
                    std::replace(first.begin(), first.end(), '~', '-');
                    from = javaParseInt(first);
                    if (!m[3].matched) {
                        to = from;
                    } else if (m[4].matched) {
                        std::string last = m[4].str();
                        std::replace(last.begin(), last.end(), '~', '-');
                        to = javaParseInt(last);
                    }
                }
                if (!aliases.contains(value))
                    aliases[value] = open(value);
                const std::vector<int> emptyBefore =
                    formats::parseIntegerList(m[6].matched ? m[6].str().substr(1) : std::string("0"));
                pageRanges.push_back(
                    range(aliases[value], from, to, m[5].str() != "e", m[5].str() != "o", emptyBefore));
            } else if (arg.rfind("-i=", 0) == 0) {
                aliases[arg.substr(3)] = open(value);
            } else if (std::regex_match(arg, outputOption)) {
                outputFile = value;
                markedOutput = arg.find('m') != std::string::npos;
                uncompressedOutput = arg.find('u') != std::string::npos;
                burstOutput = arg.find('b') != std::string::npos;
                multipageTiff = arg.find('i') != std::string::npos;
                useTempFiles = arg.find('t') != std::string::npos;
                sizeOptimize = arg.find('s') != std::string::npos;
                fullyCompress = arg.find('c') != std::string::npos;
            } else {
                if (options.empty())
                    options.push_back(cli::buildOptions());
                bool handled = false;
                for (const auto& option : options.back()) {
                    if (option->supportsOption(arg)) {
                        if (!option->setOption(arg, value, output))
                            return 1;
                        handled = true;
                        break;
                    }
                }
                if (!handled) {
                    output.err("Unknown option: " + arg + "\r\n");
                    return 1;
                }
            }
            ++i;
        } else {
            if (outputFile) {
                if (!aliases.contains(arg))
                    aliases[arg] = open(arg);
                pageRanges.push_back(range(aliases[arg], 1, -1, true, true, {0}));
            } else if (input || !pageRanges.empty() || !aliases.empty()) {
                outputFile = arg;
            } else {
                input = open(arg);
            }
        }
    }
    if (input && !pageRanges.empty()) {
        output.err("Invalid combination of input files.\r\n");
        return 1;
    }
    if (!input && pageRanges.empty()) {
        output.err("No input files.\r\n");
        return 1;
    }
    std::unique_ptr<pdf::Tweak> tweak;
    if (input) {
        tweak = std::make_unique<pdf::Tweak>(input, useTempFiles, progress);
    } else if (aliases.contains("master")) {
        input = aliases["master"];
        tweak = std::make_unique<pdf::Tweak>(input, pageRanges, useTempFiles, interleaveSize, progress);
    } else {
        input = pageRanges.front().file;
        tweak = std::make_unique<pdf::Tweak>(input, pageRanges, useTempFiles, interleaveSize, progress);
    }
    bool queried = false;
    for (const Block& block : options) {
        for (const auto& option : block) {
            option->run(*tweak, *input, output);
            queried = queried || option->queried();
        }
    }
    if (!outputFile) {
        output.err("Cannot write PDF: No output file.\r\n");
        return queried ? 0 : 1;
    }
    if (markedOutput) {
        if (uncompressedOutput)
            tweak->addPageMarks();
        else
            tweak->removePageMarks();
    }
    tweak->writeOutput(*outputFile, multipageTiff, burstOutput, uncompressedOutput, sizeOptimize,
                       fullyCompress);
    output.err("Output file written successfully.\r\n");
    return 0;
}

} // namespace

int runCommandLine(std::span<const std::wstring_view> args) {
    ConsoleOutput output;
    NullProgress progress;
    return runCommandLine(args, output, progress);
}

int runCommandLine(std::span<const std::wstring_view> args, CliOutput& output, Progress& progress) {
    std::vector<std::string> utf8;
    for (const std::wstring_view arg : args)
        utf8.push_back(toUtf8(arg));
    try {
        return run(utf8, output, progress);
    } catch (const Error& e) {
        output.err(std::string("Error: ") + e.what() + "\r\n");
    } catch (const Cancelled&) {
        output.err("Cancelled\r\n");
    } catch (const std::bad_alloc&) {
        output.err(
            "Error: PDF Tweak has run out of memory. You can enable the Tempfile option on the output "
            "tab.\r\n");
    } catch (const std::exception& e) {
        output.err(std::string("Error: ") + e.what() + "\r\n");
    }
    return 1;
}

namespace cli {

std::vector<std::unique_ptr<CommandOption>> buildOptions() {
    std::vector<std::unique_ptr<CommandOption>> options;
    options.push_back(makePreserveHyperlinksOption());
    options.push_back(makeCropOption());
    options.push_back(makeRotateOption());
    options.push_back(makeScaleOption());
    options.push_back(makeShuffleOption());
    options.push_back(makeDocInfoOption());
    options.push_back(makeEncryptOptions());
    options.push_back(makeInfoOption());
    return options;
}

} // namespace cli
} // namespace pdftweak
