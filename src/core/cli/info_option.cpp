// InfoOption: -info, -moreinfo, and -savebookmarks.

#include "core/cli/command_option.h"

#include "core/error.h"
#include "core/files.h"
#include "core/formats/bookmark.h"
#include "core/formats/tables.h"
#include "core/java_text.h"
#include "core/pdf/outline.h"
#include "core/text.h"

#include <fstream>

namespace pdftweak::cli {
namespace {

class InfoOption final : public CommandOption {
public:
    bool supportsOption(const std::string& option) const override {
        return option == "-info" || option == "-moreinfo" || option == "-savebookmarks";
    }

    bool setOption(const std::string& option, const std::string& value, CliOutput&) override {
        infoFilename_ = value;
        moreInfo_ = option == "-moreinfo";
        saveBookmarks_ = option == "-savebookmarks";
        set_ = true;
        return true;
    }

    void run(pdf::Tweak&, pdf::InputFile& masterFile, CliOutput& output) override {
        if (!set_)
            return;
        const std::string text = writeInfo(masterFile);
        if (infoFilename_ == "-") {
            output.out(text);
            return;
        }
        createForWriting(std::filesystem::path(fromUtf8(infoFilename_)), infoFilename_);
        std::ofstream out(std::filesystem::path(fromUtf8(infoFilename_)),
                          std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out)
            throw Error(Error::Kind::Io, infoFilename_ + " (cannot write the file)");
    }

    bool queried() const override { return set_; }

    std::string summary() const override {
        return " -info                   Show information about a PDF file.\n"
               " -moreinfo               Show more information about a PDF file.\n"
               " -savebookmarks          Save PDF bookmarks to CSV file\n";
    }

    std::string help(const std::string& option) const override {
        std::string desc;
        if (option == "-info")
            desc = "Write information about the input PDF file";
        else if (option == "-moreinfo")
            desc = "Write more information about the input PDF file";
        else
            desc = "Save PDF bookmarks";
        return " " + option + " {TEXTFILE}\n" + "    " + desc + " to {TEXTFILE}.\n" + " " + option +
               " -\n" + "    " + desc + " to standard output.";
    }

private:
    static std::string bookmarkLine(const formats::Bookmark& b) {
        const std::string bm = b.toString();
        const std::string bm2 = formats::parseBookmark(bm).toString();
        if (bm != bm2)
            throw Error(Error::Kind::Document, bm + " != " + bm2);
        return bm;
    }

    std::string writeInfo(pdf::InputFile& file) const {
        std::string w;
        const auto line = [&](const std::string& text) { w += text + "\r\n"; };
        if (saveBookmarks_) {
            for (const formats::Bookmark& b : pdf::readBookmarks(*file.qpdf(), 1))
                line(bookmarkLine(b));
            return w;
        }
        line("PAGECOUNT " + std::to_string(file.pageCount()));
        std::string permissions;
        for (size_t i = 0; i < formats::permissionNames().size(); ++i)
            if ((file.permissions() & formats::permissionBits()[i]) != 0)
                permissions += " " + formats::permissionNames()[i];
        line("ENCRYPTION " + std::to_string(file.cryptoMode()) +
             (file.metadataEncrypted() ? " METADATA" : "") + permissions);
        line("    OWNERPW " + file.ownerPassword());
        line("    USERPW " + file.userPassword());
        for (const auto& [key, value] : file.infoDictionary())
            line("DOCINFO " + key + "=" + value);
        if (const auto formats = file.pageLabels())
            for (const formats::PageLabelFormat& format : *formats)
                line("PAGELABELS " + formats::formatPageLabel(format));
        if (moreInfo_) {
            for (const formats::Bookmark& b : pdf::readBookmarks(*file.qpdf(), 1))
                line("BOOKMARK " + bookmarkLine(b));
            float lastWidth = 0, lastHeight = 0;
            int lastChange = 0;
            const int count = file.pageCount();
            const auto size = [&](int to) {
                line("PAGESIZE " + std::to_string(lastChange) + "-" + std::to_string(to) + " " +
                     javaFloat(lastWidth) + "x" + javaFloat(lastHeight));
            };
            for (int i = 1; i <= count; ++i) {
                const pdf::Rect current = file.pageSize(i);
                if (current.width() != lastWidth || current.height() != lastHeight) {
                    if (lastChange != 0)
                        size(i - 1);
                    lastWidth = current.width();
                    lastHeight = current.height();
                    lastChange = i;
                }
            }
            if (lastChange != 0)
                size(count);
        }
        return w;
    }

    std::string infoFilename_;
    bool moreInfo_ = false;
    bool saveBookmarks_ = false;
    bool set_ = false;
};

} // namespace

std::unique_ptr<CommandOption> makeInfoOption() { return std::make_unique<InfoOption>(); }

} // namespace pdftweak::cli
