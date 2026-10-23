#include "core/pdf/tweak.h"

#include "core/error.h"
#include "core/java_text.h"
#include "core/pdf/copy_pages.h"
#include "core/text.h"

#include <windows.h>

#include <qpdf/QPDFWriter.hh>

#include <algorithm>

namespace pdftweak::pdf {
namespace {

std::filesystem::path createTempFile() {
    wchar_t directory[MAX_PATH + 1];
    wchar_t name[MAX_PATH + 1];
    if (GetTempPathW(MAX_PATH + 1, directory) == 0 || GetTempFileNameW(directory, L"~pd", 0, name) == 0)
        throw Error(Error::Kind::Io, "Cannot create a temporary file");
    return std::filesystem::absolute(name);
}

} // namespace

std::filesystem::path Tweak::temporaryFile() { return createTempFile(); }

Tweak::Tweak(std::shared_ptr<InputFile> single, bool useTempFiles, Progress& progress)
    : progress_(progress), current_(single->qpdf()), infoOrder_(single->infoOrder_) {
    if (useTempFiles) {
        tempfile1_ = createTempFile();
        tempfile2_ = createTempFile();
    }
    const std::filesystem::path file = std::filesystem::absolute(single->file());
    inputFilePath_ = file.parent_path().wstring();
    inputFileFullName_ = file.filename().wstring();
    const size_t dot = inputFileFullName_.rfind(L'.');
    inputFileName_ = dot == std::wstring::npos ? inputFileFullName_ : inputFileFullName_.substr(0, dot);
    inputFiles_.push_back(file);
}

Tweak::Tweak(std::shared_ptr<InputFile> first, const std::vector<InputRange>& ranges, bool useTempFiles,
             int interleaveSize, Progress& progress)
    : Tweak(first, useTempFiles, progress) {
    try {
        PageCopier copy;
        const auto add = [&](const InputRange& range, int page) {
            checkCancelled();
            if (page == -1)
                copy.addBlankPage(range.file->pageSize(1));
            else
                copy.addPage(*range.file->qpdf(), page - 1);
            progress_.advance();
        };
        long long total = 0;
        std::vector<std::vector<int>> pagesPerRange;
        if (interleaveSize == 0) {
            int pagesBefore = 0;
            for (const InputRange& range : ranges) {
                pagesPerRange.push_back(range.range.pages(pagesBefore));
                pagesBefore += static_cast<int>(pagesPerRange.back().size());
                total += static_cast<long long>(pagesPerRange.back().size());
            }
            progress_.stage("Merging", total);
            for (size_t r = 0; r < ranges.size(); ++r) {
                for (const int page : pagesPerRange[r])
                    add(ranges[r], page);
                const std::filesystem::path file = std::filesystem::absolute(ranges[r].file->file());
                const bool known = std::any_of(inputFiles_.begin(), inputFiles_.end(), [&](const auto& f) {
                    std::error_code ignored;
                    return std::filesystem::equivalent(f, file, ignored);
                });
                if (!known)
                    inputFiles_.push_back(file);
            }
        } else {
            size_t maxLength = 0;
            for (const InputRange& range : ranges) {
                pagesPerRange.push_back(range.range.pages(0));
                maxLength = std::max(maxLength, pagesPerRange.back().size());
            }
            const size_t size = static_cast<size_t>(interleaveSize);
            const size_t blockCount = (maxLength + size - 1) / size;
            progress_.stage("Merging", static_cast<long long>(blockCount * size * ranges.size()));
            for (size_t block = 0; block < blockCount; ++block) {
                for (size_t r = 0; r < ranges.size(); ++r) {
                    for (size_t k = 0; k < size; ++k) {
                        const size_t index = block * size + k;
                        add(ranges[r], index < pagesPerRange[r].size() ? pagesPerRange[r][index] : -1);
                    }
                }
            }
        }
        QPDF& firstPdf = *first->qpdf();
        std::shared_ptr<QPDF> merged = copy.finish();
        copyXmpMetadata(firstPdf, *merged);
        copyInformation(firstPdf, *merged);
        replaceCurrent(*merged);
    } catch (...) {
        cleanup();
        rethrowAsError();
    }
}

Tweak::~Tweak() { cleanup(); }

int Tweak::pageCount() const { return static_cast<int>(pages(*current_).size()); }

void Tweak::checkCancelled() const {
    if (progress_.cancelled())
        throw Cancelled();
}

void Tweak::replaceCurrent(QPDF& next) {
    if (!tempfile1_.empty()) {
        std::swap(tempfile1_, tempfile2_);
        {
            QPDFWriter writer(next, utf8Path(tempfile1_).c_str());
            writer.setPreserveEncryption(false);
            writer.write();
        }
        current_ = openFile(tempfile1_, "");
    } else {
        current_ = openBytes(writeBytes(next), "current document");
    }
    infoOrder_ = std::make_shared<std::vector<std::string>>();
}

void Tweak::cleanup() {
    std::error_code ignored;
    if (!tempfile1_.empty())
        std::filesystem::remove(tempfile1_, ignored);
    if (!tempfile2_.empty())
        std::filesystem::remove(tempfile2_, ignored);
}

void Tweak::updateInfoDictionary(const std::vector<std::pair<std::string, std::string>>& newInfo) {
    QPDFObjectHandle info = current_->getTrailer().getKey("/Info");
    if (info.isDictionary()) {
        for (const auto& [key, value] : newInfo) {
            const std::string name = "/" + key;
            const auto known = std::find(infoOrder_->begin(), infoOrder_->end(), name);
            if (value.empty()) {
                info.removeKey(name);
                if (known != infoOrder_->end())
                    infoOrder_->erase(known);
            } else {
                info.replaceKey(name, QPDFObjectHandle::newUnicodeString(value));
                if (known == infoOrder_->end())
                    infoOrder_->push_back(name);
            }
        }
    }
    // remove XMP metadata
    current_->getRoot().removeKey("/Metadata");
}

void Tweak::setEncryption(int mode, int permissions, const std::string& ownerPassword,
                          const std::string& userPassword) {
    encryptionMode_ = mode;
    encryptionPermissions_ = permissions;
    userPassword_ = userPassword;
    ownerPassword_ = ownerPassword;
    if (ownerPassword.empty())
        throw Error(Error::Kind::Io, "Owner password may not be empty");
}

void Tweak::addPageMarks() {
    int number = 1;
    for (QPDFPageObjectHelper& page : pages(*current_))
        page.getObjectHandle().replaceKey("/pdftk_PageNum", QPDFObjectHandle::newInteger(number++));
}

void Tweak::removePageMarks() {
    for (QPDFPageObjectHelper& page : pages(*current_))
        page.getObjectHandle().removeKey("/pdftk_PageNum");
}

} // namespace pdftweak::pdf
