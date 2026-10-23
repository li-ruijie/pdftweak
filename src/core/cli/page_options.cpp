// PreserveHyperlinksOption, CropOption, RotateOption, ScaleOption, and ShuffleOption.

#include "core/cli/command_option.h"

#include "core/error.h"
#include "core/formats/shuffle_rule.h"
#include "core/java_text.h"
#include "core/text.h"

#include <optional>
#include <regex>

namespace pdftweak::cli {
namespace {

class PreserveHyperlinksOption final : public CommandOption {
public:
    bool supportsOption(const std::string& option) const override { return option == "-preserveLinks"; }
    bool setOption(const std::string&, const std::string&, CliOutput&) override {
        preserveLinks_ = true;
        return preserveLinks_;
    }
    void run(pdf::Tweak& tweak, pdf::InputFile&, CliOutput&) override {
        if (preserveLinks_)
            tweak.preserveHyperlinks();
    }
    std::string summary() const override {
        return " -preserveLinks          Preserve hyperlinks (EXPERIMENTAL)\n";
    }
    std::string help(const std::string&) const override {
        return " -preserveLinks On\n"
               "    Preserve Hyperlinks (EXPERIMENTAL).";
    }

private:
    bool preserveLinks_ = false;
};

class CropOption final : public CommandOption {
public:
    bool supportsOption(const std::string& option) const override { return option == "-crop"; }
    bool setOption(const std::string&, const std::string& value, CliOutput& output) override {
        if (cropTo_) {
            output.err("Error: more than one -crop option used.\r\n");
            return false;
        }
        static const std::pair<const char*, pdf::PageBox> boxes[] = {{"MediaBox", pdf::PageBox::MediaBox},
                                                                     {"CropBox", pdf::PageBox::CropBox},
                                                                     {"BleedBox", pdf::PageBox::BleedBox},
                                                                     {"TrimBox", pdf::PageBox::TrimBox},
                                                                     {"ArtBox", pdf::PageBox::ArtBox}};
        for (const auto& [name, box] : boxes) {
            if (value == name) {
                cropTo_ = box;
                return true;
            }
        }
        output.err("Unknown crop box: " + value + "\r\n");
        return false;
    }
    void run(pdf::Tweak& tweak, pdf::InputFile&, CliOutput&) override {
        if (cropTo_)
            tweak.cropPages(*cropTo_);
    }
    std::string summary() const override {
        return " -crop                   Crop pages to the given page box.\n";
    }
    std::string help(const std::string&) const override {
        return " -crop {BOXNAME}\n"
               "    Crop all pages to the given page box.\n"
               "    Valid box names are:\n"
               "    MediaBox, CropBox, BleedBox, TrimBox, ArtBox";
    }

private:
    std::optional<pdf::PageBox> cropTo_;
};

class RotateOption final : public CommandOption {
public:
    bool supportsOption(const std::string& option) const override {
        static const std::regex pattern("-rot(ate)?(exp(licitly)?)?");
        return std::regex_match(option, pattern);
    }
    bool setOption(const std::string& option, const std::string& value, CliOutput& output) override {
        if (set_) {
            output.err("Error: more than one -rotate option used.\r\n");
            return false;
        }
        if (option.find("exp") != std::string::npos)
            explicit_ = true;
        const std::u32string chars = codePoints(value);
        if (chars.size() == 1) {
            portraitCount_ = landscapeCount_ = fromChar(chars[0], output);
        } else {
            // Java reads charAt(0) and charAt(1), failing on an empty value
            if (chars.empty())
                throw Error(Error::Kind::Document,
                            "java.lang.StringIndexOutOfBoundsException: Index 0 out of "
                            "bounds for length 0");
            portraitCount_ = fromChar(chars[0], output);
            landscapeCount_ = fromChar(chars[1], output);
        }
        set_ = true;
        return true;
    }
    void run(pdf::Tweak& tweak, pdf::InputFile&, CliOutput&) override {
        if (portraitCount_ != 0 || landscapeCount_ != 0)
            tweak.rotatePages(portraitCount_, landscapeCount_);
        if (explicit_)
            tweak.removeRotation();
    }
    std::string summary() const override {
        return " -rot[ate]               Rotate pages, optionally separately for portrait and\n"
               "                         landscape.\n"
               " -rot[ate]exp[licitly]   The same, but change real content and not the page's\n"
               "                         /Rotate attribute.\n";
    }
    std::string help(const std::string& option) const override {
        const std::string details =
            "\n"
            "    If only one value is given, it is used for both portrait and\n"
            "    landscape pages.\n"
            "    \n"
            "    Available options for {X} and {Y}:\n"
            "        K, k, or 0: Keep page (rotate 0 degrees)\n"
            "        R, r, or 1: Rotate right (rotate 90 degrees)\n"
            "        U, u, or 2: Turn upside down (rotate 180 degrees)\n"
            "        L, l, or 3: Rotate left (rotate 270 degrees)\n"
            "    \n"
            "    Example: -rotate KL\n"
            "        Rotate all landscape pages left, so that all pages will be portrait\n"
            "        afterwards.";
        if (option.find("exp") != std::string::npos) {
            return " -rot[ate]exp[licitly] {X}{Y}\n"
                   "    Rotate portrait pages by {X} and landscape pages by {Y}.\n"
                   "    This will change real content (and destroy interactive features)\n"
                   "    but will not cause problems with tools that do not understand the /Rotate\n"
                   "    attribute. Can also be used to remove the rotate attribute from a file\n"
                   "    while keeping its appearance constant (use 'K' rotation for that).\n" +
                   details;
        }
        return " -rot[ate] {X}{Y}\n"
               "    Rotate portrait pages by {X} and landscape pages by {Y}.\n"
               "    This will change the /Rotation option (and keep interactive features)\n"
               "    but might cause problems with tools that do not understand this option.\n" +
               details;
    }

private:
    static int fromChar(char32_t c, CliOutput& output) {
        switch (c) {
        case U'k':
        case U'K':
        case U'0': return 0;
        case U'r':
        case U'R':
        case U'1': return 1;
        case U'u':
        case U'U':
        case U'2': return 2;
        case U'l':
        case U'L':
        case U'3': return 3;
        default: break;
        }
        output.err("Unknown rotation \"" + fromCodePoints(std::u32string(1, c)) + "\", ignoring.\r\n");
        return 0;
    }

    int portraitCount_ = 0;
    int landscapeCount_ = 0;
    bool explicit_ = false;
    bool set_ = false;
};

class ScaleOption final : public CommandOption {
public:
    bool supportsOption(const std::string& option) const override { return option == "-scale"; }
    bool setOption(const std::string&, const std::string& value, CliOutput& output) override {
        if (enabled_) {
            output.err("Error: more than one -scale option used.\r\n");
            return false;
        }
        static const std::regex pattern(R"(([0-9.]+),([0-9.]+)(,[cr]*)?)");
        std::smatch m;
        if (!std::regex_match(value, m, pattern)) {
            output.err("Error: Could not parse -scale option: " + value + "\r\n");
            return false;
        }
        enabled_ = true;
        width_ = static_cast<float>(javaParseDouble(m[1].str()));
        height_ = static_cast<float>(javaParseDouble(m[2].str()));
        const std::string opts = m[3].matched ? m[3].str() : std::string();
        center_ = opts.find('c') != std::string::npos;
        preserveRatio_ = opts.find('r') == std::string::npos;
        return true;
    }
    void run(pdf::Tweak& tweak, pdf::InputFile&, CliOutput&) override {
        if (enabled_)
            tweak.scalePages(width_, height_, center_, preserveRatio_);
    }
    std::string summary() const override { return " -scale                  Scale pages\n"; }
    std::string help(const std::string&) const override {
        return " -scale {WIDTH},{HEIGHT}[,{OPTIONS}]\n"
               "    Scale all pages to fit to new page size.\n"
               "    If {OPTIONS} includes \"c\", center pages instead of enlarging them,\n"
               "    \"r\" in {OPTIONS} ignores aspect ratio.\n"
               "    Specify {WIDTH} and {HEIGHT} in postscript points.";
    }

private:
    bool enabled_ = false;
    bool center_ = false;
    bool preserveRatio_ = false;
    float width_ = 0;
    float height_ = 0;
};

class ShuffleOption final : public CommandOption {
public:
    bool supportsOption(const std::string& option) const override { return option == "-shuffle"; }
    bool setOption(const std::string&, const std::string& value, CliOutput& output) override {
        if (shuffleRule_) {
            output.err("Error: more than one -shuffle option used.\r\n");
            return false;
        }
        shuffleRule_ = value;
        return true;
    }
    void run(pdf::Tweak& tweak, pdf::InputFile&, CliOutput&) override {
        if (shuffleRule_)
            tweak.shufflePages(formats::parseRuleSet(*shuffleRule_));
    }
    std::string summary() const override { return " -shuffle                Shuffle pages\n"; }
    std::string help(const std::string&) const override {
        return " -shuffle {RULE}\n"
               "   shuffle all pages. {RULE} uses the same syntax as used in the GUI.";
    }

private:
    std::optional<std::string> shuffleRule_;
};

} // namespace

std::unique_ptr<CommandOption> makePreserveHyperlinksOption() {
    return std::make_unique<PreserveHyperlinksOption>();
}
std::unique_ptr<CommandOption> makeCropOption() { return std::make_unique<CropOption>(); }
std::unique_ptr<CommandOption> makeRotateOption() { return std::make_unique<RotateOption>(); }
std::unique_ptr<CommandOption> makeScaleOption() { return std::make_unique<ScaleOption>(); }
std::unique_ptr<CommandOption> makeShuffleOption() { return std::make_unique<ShuffleOption>(); }

} // namespace pdftweak::cli
