// WatermarkOptions, PageNumberOption, BookmarksOption, AttachOption, TransitionOption, and
// ViewerOptions.

#include "core/cli/command_option.h"

#include "core/error.h"
#include "core/files.h"
#include "core/formats/bookmark.h"
#include "core/formats/page_labels.h"
#include "core/formats/tables.h"
#include "core/formats/viewer_preferences.h"
#include "core/java_text.h"
#include "core/text.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>

namespace pdftweak::cli {
namespace {

std::string underscores(std::string text) {
    std::replace(text.begin(), text.end(), ' ', '_');
    return text;
}

class WatermarkOptions final : public CommandOption {
public:
    bool supportsOption(const std::string& option) const override {
        return option == "-background" || option == "-watermark" || option == "-numbers";
    }

    bool setOption(const std::string& option, const std::string& value, CliOutput& output) override {
        if (option == "-background")
            background_ = value;
        if (option == "-watermark") {
            static const std::regex pattern(R"((.*),([0-9]+),([0-9.]+)(,#[0-9a-fA-F]{6})?)");
            std::smatch m;
            if (!std::regex_match(value, m, pattern)) {
                output.err("Error: Invalid parameter for -watermark option\r\n");
                return false;
            }
            text_ = m[1].str();
            textSize_ = javaParseInt(m[2].str());
            textOpacity_ = javaParseFloat(m[3].str());
            if (m[4].matched) {
                const std::string color = m[4].str();
                textColor_ = std::array<int, 3>{std::stoi(color.substr(2, 2), nullptr, 16),
                                                std::stoi(color.substr(4, 2), nullptr, 16),
                                                std::stoi(color.substr(6, 2), nullptr, 16)};
            }
        }
        if (option == "-numbers") {
            static const std::regex pattern(
                R"(([LRIOTBC]|[LRIO][TB]),([0-9]+),(-?[0-9]+),(-?[0-9]+)(,.+)?)");
            std::smatch m;
            if (!std::regex_match(value, m, pattern)) {
                output.err("Error: Invalid parameter for -numbers option\r\n");
                return false;
            }
            static const std::vector<std::string> plain = {"LB", "B", "RB", "L", "C", "R", "LT", "T", "RT"};
            static const std::vector<std::string> flipped = {"IB", "B",  "OB", "I", "C",
                                                             "O",  "IT", "T",  "OT"};
            const std::string where = m[1].str();
            auto found = std::find(plain.begin(), plain.end(), where);
            position_ = found == plain.end() ? -1 : static_cast<int>(found - plain.begin());
            if (position_ == -1) {
                found = std::find(flipped.begin(), flipped.end(), where);
                position_ = found == flipped.end() ? -1 : static_cast<int>(found - flipped.begin());
                flipEven_ = true;
            }
            numberSize_ = javaParseInt(m[2].str());
            xoff_ = static_cast<float>(javaParseInt(m[3].str()));
            yoff_ = static_cast<float>(javaParseInt(m[4].str()));
            mask_ = m[5].matched ? m[5].str().substr(1) : std::string();
        }
        return true;
    }

    void run(pdf::Tweak& tweak, pdf::InputFile&, CliOutput&) override {
        if (!background_ && !text_ && position_ == -1)
            return;
        std::optional<pdf::InputFile> wmFile;
        if (background_)
            wmFile.emplace(std::filesystem::path(fromUtf8(*background_)), "");
        tweak.addWatermark(wmFile ? &*wmFile : nullptr, text_, textSize_, textOpacity_, textColor_,
                           position_, flipEven_, numberSize_, xoff_, yoff_, mask_);
    }

    std::string summary() const override {
        return " -background             Add file as background watermark\n"
               " -watermark              Add text watermark\n"
               " -numbers                Add page numbers\n";
    }

    std::string help(const std::string& option) const override {
        if (option == "-background")
            return " -background {FILENAME}\n"
                   "    Add first page of file {FILENAME} as background watermark.\n"
                   "    This watermark file may not be password protected.";
        if (option == "-watermark")
            return " -watermark {TEXT},{FONTSIZE},{OPACITY}[,#{RRGGBB}]\n"
                   "    Add a text watermark. {OPACITY} is a float between 0 and 1,\n"
                   "    for example 0.25. {RRGGBB} is a HTML color.";
        return " -numbers {POSITION},{FONTSIZE},{XOFFSET},{YOFFSET}{,MASK}\n"
               "    Add page numbers. {XOFFSET} and {YOFFSET} are in PostScript\n"
               "    points. {POSITION} may be one of:\n\n"
               "        L left edge\n"
               "        R right edge\n"
               "        I inner edge\n"
               "        O outer edge\n"
               "        T top edge\n"
               "        B bottom edge\n"
               "        LB left bottom corner\n"
               "        RB right bottom corner\n"
               "        IB inner bottom corner\n"
               "        OB outer bottom corner\n"
               "        LT left top corner\n"
               "        RT right top corner\n"
               "        IT inner top corner\n"
               "        OT outer top corner\n"
               "        C center of page";
    }

private:
    std::optional<std::string> background_;
    std::optional<std::string> text_;
    int textSize_ = 0;
    float textOpacity_ = 0;
    std::optional<std::array<int, 3>> textColor_;
    int position_ = -1;
    bool flipEven_ = false;
    int numberSize_ = 0;
    float xoff_ = 0;
    float yoff_ = 0;
    std::string mask_;
};

class PageNumberOption final : public CommandOption {
public:
    bool supportsOption(const std::string& option) const override {
        return option == "-pagenumbers" || option == "-loadpagenumbers";
    }

    bool setOption(const std::string& option, const std::string& value, CliOutput&) override {
        if (option == "-loadpagenumbers") {
            for (const std::string& line : readLines(value))
                pageLabels_.push_back(formats::parsePageLabel(line));
        } else if (value == "-") {
            noLabels_ = true;
        } else {
            pageLabels_.push_back(formats::parsePageLabel(value));
        }
        return true;
    }

    void run(pdf::Tweak& tweak, pdf::InputFile&, CliOutput&) override {
        if (noLabels_)
            tweak.setPageNumbers({});
        if (!pageLabels_.empty())
            tweak.setPageNumbers(pageLabels_);
    }

    std::string summary() const override {
        return " -pagenumbers            Add PDF page number format(s)\n"
               " -loadpagenumbers        Load PDF page number format from CSV file\n";
    }

    std::string help(const std::string&) const override {
        return " -pagenumbers {NUMBERFORMAT}\n"
               "    Add a page number format.\n"
               " -pagenumbers -\n"
               "    Remove all page number formats.\n"
               " -loadpagenumbers {FILE}\n"
               "    Load page numbers from text file.\n\n"
               " A page number format looks like this:\n"
               "    page=[prefix:]pagenum[type]\n"
               "  [page] is the page number of the first page where the format\n"
               "         is used.\n"
               "  [pagenum] is the number that this page should get\n"
               "  [type] is the number type: nothing or one of 'I i A a -'.\n"
               "  [prefix] if given (separated by a colon) is the prefix before\n"
               "         the number.\n\n"
               " Examples:\n"
               " 1=Page :1\n"
               " 10=10A\n"
               " 20=Chapter:1I\n"
               " 100=Appendix:100-";
    }

private:
    std::vector<formats::PageLabelFormat> pageLabels_;
    bool noLabels_ = false;
};

class BookmarksOption final : public CommandOption {
public:
    bool supportsOption(const std::string& option) const override {
        return option == "-bookmark" || option == "-loadbookmarks";
    }

    bool setOption(const std::string& option, const std::string& value, CliOutput&) override {
        if (option == "-loadbookmarks") {
            for (const std::string& line : readLines(value))
                bookmarks_.push_back(formats::parseBookmark(line));
        } else if (value == "-") {
            noBookmarks_ = true;
        } else {
            bookmarks_.push_back(formats::parseBookmark(value));
        }
        return true;
    }

    void run(pdf::Tweak& tweak, pdf::InputFile&, CliOutput&) override {
        if (noBookmarks_)
            tweak.updateBookmarks({});
        if (!bookmarks_.empty())
            tweak.updateBookmarks(bookmarks_);
    }

    std::string summary() const override {
        return " -bookmark               Add PDF bookmark(s)\n"
               " -loadbookmarks          Load PDF bookmarks from CSV file\n";
    }

    std::string help(const std::string&) const override {
        return " -bookmark {BOOKMARK}\n"
               "    Add a pdf bookmark.\n"
               " -bookmark -\n"
               "    Remove all PDF bookmarks\n"
               " -loadbookmarks {FILE}\n"
               "    Load PDF bookmarks from CSV file.\n\n"
               " The file format is the same used by -savebookmarks; the -bookmark\n"
               " options needs single lines from it.";
    }

private:
    std::vector<formats::Bookmark> bookmarks_;
    bool noBookmarks_ = false;
};

class AttachOption final : public CommandOption {
public:
    bool supportsOption(const std::string& option) const override { return option == "-attach"; }

    bool setOption(const std::string&, const std::string& value, CliOutput&) override {
        files_.emplace_back(fromUtf8(value));
        return true;
    }

    void run(pdf::Tweak& tweak, pdf::InputFile&, CliOutput&) override {
        for (const std::filesystem::path& file : files_)
            tweak.addFile(file);
    }

    std::string summary() const override { return " -attach                 Attach a file\n"; }

    std::string help(const std::string&) const override {
        return " -attach {FILENAME}\n"
               "    Attach file {FILENAME}";
    }

private:
    std::vector<std::filesystem::path> files_;
};

class TransitionOption final : public CommandOption {
public:
    bool supportsOption(const std::string& option) const override {
        return option == "-trans" || option == "-transition" || option == "-transitionfile" ||
               option == "-transfile";
    }

    bool setOption(const std::string& option, const std::string& value, CliOutput&) override {
        if (option.size() >= 4 && option.compare(option.size() - 4, 4, "file") == 0) {
            for (const std::string& line : readLines(value))
                transitions_.push_back(line);
        } else {
            transitions_.push_back(value);
        }
        return true;
    }

    void run(pdf::Tweak& tweak, pdf::InputFile&, CliOutput& output) override {
        static const std::regex pattern(R"((~?[0-9]+)(?:-(~?[0-9]+))?:([A-Za-z_-]+),([0-9]+)(,[0-9]+)?)");
        const auto pageOf = [](std::string text) {
            std::replace(text.begin(), text.end(), '~', '-');
            return javaParseInt(text);
        };
        const std::vector<std::string>& names = formats::transitionNames();
        for (const std::string& trans : transitions_) {
            std::smatch m;
            if (!std::regex_match(trans, m, pattern)) {
                output.err("Skipping unparsable transition: " + trans + "\r\n");
                continue;
            }
            int from = pageOf(m[1].str());
            int to = m[2].matched ? pageOf(m[2].str()) : from;
            if (from < 0)
                from += tweak.pageCount() + 1;
            if (to < 0)
                to += tweak.pageCount() + 1;
            std::string transName = m[3].str();
            std::replace(transName.begin(), transName.end(), '_', ' ');
            const auto found = std::find(names.begin(), names.end(), transName);
            const int tduration = javaParseInt(m[4].str());
            const int pduration = m[5].matched ? javaParseInt(m[5].str().substr(1)) : -1;
            if (found == names.end()) {
                output.err("Skipping unknown transition name: " + transName + "\r\n");
                continue;
            }
            for (int i = from; i <= to; ++i)
                tweak.setTransition(i, static_cast<int>(found - names.begin()), tduration, pduration);
        }
    }

    std::string summary() const override {
        return " -trans[ition]           Add a page transition\n"
               " -trans[ition]file       Load page transitions from a file\n";
    }

    std::string help(const std::string&) const override {
        std::string transNames;
        for (const std::string& name : formats::transitionNames())
            transNames += "\n    " + underscores(name);
        return " -trans[ition] {FROM}[-{TO}]:{TRANS},{DURATION}[,{PAGEDUR}]\n"
               "    Add page transition {TRANS} to pages {FROM} to {TO}.\n"
               "    The transition will have a duration of {DURATION}.\n"
               "    If {PAGEDUR} is given, these pages remain visible for\n"
               "    that many seconds.\n\n"
               " -trans[ition]file {FILENAME}\n"
               "    Load page transitions from a file. The format is the same\n"
               "    as in the -trans command.\n\n"
               " Available transitions:" +
               transNames;
    }

private:
    std::vector<std::string> transitions_;
};

class ViewerOptions final : public CommandOption {
public:
    bool supportsOption(const std::string& option) const override {
        return option == "-pagemode" || option == "-pagelayout" || option == "-viewpref";
    }

    bool setOption(const std::string& option, const std::string& value, CliOutput& output) override {
        if (option == "-pagemode") {
            pageMode_ = indexOf(formats::pageModeNames(), value);
            if (pageMode_ == -1) {
                output.err("Invalid page mode: " + value + "\r\n");
                return false;
            }
        } else if (option == "-pagelayout") {
            pageLayout_ = indexOf(formats::pageLayoutNames(), value);
            if (pageLayout_ == -1) {
                output.err("Invalid page layout: " + value + "\r\n");
                return false;
            }
        } else if (option == "-viewpref") {
            const size_t pos = value.find('=');
            if (pos == std::string::npos) {
                output.err("Invalid viewer preference: " + value + "\r\n");
                return false;
            }
            std::string name = value.substr(0, pos);
            std::replace(name.begin(), name.end(), '_', ' ');
            if (name == "Display document tile")
                name = "Display document title"; // the name before version 1.2.0
            const std::string pvalue = value.substr(pos + 1);
            for (const formats::ViewerPreference& vp : formats::viewerPreferences()) {
                if (vp.name != name)
                    continue;
                for (const std::string& v : vp.values) {
                    if (v == pvalue) {
                        const auto existing =
                            std::find_if(optionalPrefs_.begin(), optionalPrefs_.end(),
                                         [&](const auto& entry) { return entry.first == vp.key; });
                        if (existing != optionalPrefs_.end())
                            existing->second = v;
                        else
                            optionalPrefs_.emplace_back(vp.key, v);
                        return true;
                    }
                }
            }
            output.err("Invalid viewer preference: " + value + "\r\n");
            return false;
        }
        return true;
    }

    void run(pdf::Tweak& tweak, pdf::InputFile&, CliOutput&) override {
        if (pageMode_ == -1 && pageLayout_ == -1 && optionalPrefs_.empty())
            return;
        if (pageMode_ == -1)
            pageMode_ = 0;
        if (pageLayout_ == -1)
            pageLayout_ = 0;
        // PdfWriter.PageModeUseNone << pagemode plus PdfWriter.PageLayoutSinglePage << pagelayout
        tweak.setViewerPreferences((64 << pageMode_) + (1 << pageLayout_), optionalPrefs_);
    }

    std::string summary() const override {
        return " -pagemode               Set page mode (viewer preference)\n"
               " -pagelayout             Set page layout (viewer preference)\n"
               " -viewpref               Set additional viewer preferences\n";
    }

    std::string help(const std::string&) const override {
        return " -pagemode {PAGEMODE}\n"
               " -pagelayout {PAGELAYOUT}\n"
               " -viewpref {NAME}={VALUE}\n"
               "    Set viewer preferences. Names and values are like in the GUI,\n"
               "    except that spaces are replaced by underscores.";
    }

private:
    static int indexOf(const std::vector<std::string>& names, const std::string& value) {
        for (size_t i = 0; i < names.size(); ++i)
            if (underscores(names[i]) == value)
                return static_cast<int>(i);
        return -1;
    }

    int pageMode_ = -1;
    int pageLayout_ = -1;
    std::vector<std::pair<std::string, std::string>> optionalPrefs_;
};

} // namespace

std::unique_ptr<CommandOption> makeWatermarkOptions() { return std::make_unique<WatermarkOptions>(); }
std::unique_ptr<CommandOption> makePageNumberOption() { return std::make_unique<PageNumberOption>(); }
std::unique_ptr<CommandOption> makeBookmarksOption() { return std::make_unique<BookmarksOption>(); }
std::unique_ptr<CommandOption> makeAttachOption() { return std::make_unique<AttachOption>(); }
std::unique_ptr<CommandOption> makeTransitionOption() { return std::make_unique<TransitionOption>(); }
std::unique_ptr<CommandOption> makeViewerOptions() { return std::make_unique<ViewerOptions>(); }

} // namespace pdftweak::cli
