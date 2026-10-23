// PdfTweak.writeOutput and the settings it applies to the output.

#include "core/pdf/tweak.h"

#include "core/error.h"
#include "core/files.h"
#include "core/formats/page_labels.h"
#include "core/formats/xmp.h"
#include "core/mupdf_bridge.h"
#include "core/pdf/copy_pages.h"
#include "core/sign/signing_key.h"
#include "core/text.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>

namespace pdftweak::pdf {
namespace {

void replaceAll(std::wstring& text, const std::wstring& from, const std::wstring& to) {
    for (size_t at = text.find(from); at != std::wstring::npos; at = text.find(from, at + to.size()))
        text.replace(at, from.size(), to);
}

// inputFiles.contains(file.getCanonicalFile()), which fails as Java's canonicalisation does
bool isInput(const std::vector<std::filesystem::path>& inputs, const std::filesystem::path& file) {
    checkCanonical(file);
    std::error_code ignored;
    if (!std::filesystem::exists(file, ignored))
        return false;
    return std::any_of(inputs.begin(), inputs.end(), [&](const auto& input) {
        return std::filesystem::equivalent(input, file, ignored);
    });
}

void writeFile(QPDF& pdf, const std::filesystem::path& file,
               const std::function<void(QPDFWriter&)>& configure) {
    createForWriting(file, utf8Path(file));
    QPDFWriter writer(pdf, utf8Path(file).c_str());
    configure(writer);
    writer.write();
}

} // namespace

void Tweak::configureWriter(QPDFWriter& writer, bool uncompressed, bool fullyCompressed) const {
    writer.setPreserveEncryption(false);
    if (uncompressed) {
        writer.setCompressStreams(false);
        writer.setDecodeLevel(qpdf_dl_generalized);
    }
    if (fullyCompressed)
        writer.setObjectStreamMode(qpdf_o_generate);
    if (encryptionMode_ == -1)
        return;
    // spec section 5.1 and decision 4
    const int p = encryptionPermissions_;
    const int base = encryptionMode_ & 7;
    const bool encryptMetadata = (encryptionMode_ & 8) == 0;
    if (base > 3 || (encryptionMode_ & 16) != 0)
        throw Error(Error::Kind::Document, "No valid encryption mode");
    const char* user = userPassword_.c_str();
    const char* owner = ownerPassword_.c_str();
    if (base == 0) {
        writer.setR2EncryptionParametersInsecure(user, owner, p & 4, p & 8, p & 16, p & 32);
        return;
    }
    const qpdf_r3_print_e print = (p & 4) == 0      ? qpdf_r3p_none
                                  : (p & 2048) != 0 ? qpdf_r3p_full
                                                    : qpdf_r3p_low;
    const bool accessibility = p & 512, extract = p & 16, assemble = p & 1024, annotate = p & 32,
               fill = p & 256, modify = p & 8;
    if (base == 1 && encryptMetadata)
        writer.setR3EncryptionParametersInsecure(user, owner, accessibility, extract, assemble, annotate,
                                                 fill, modify, print);
    else if (base == 1 || base == 2)
        writer.setR4EncryptionParametersInsecure(user, owner, accessibility, extract, assemble, annotate,
                                                 fill, modify, print, encryptMetadata, base == 2);
    else
        writer.setR5EncryptionParameters(user, owner, accessibility, extract, assemble, annotate, fill,
                                         modify, print, encryptMetadata);
}

void Tweak::finishInformation(QPDF& pdf) const {
    // decision 10: ModDate becomes now, Producer stays or is supplied, and existing XMP properties
    // follow
    const std::string date = pdfDateNow();
    QPDFObjectHandle info = pdf.getTrailer().getKey("/Info");
    if (!info.isDictionary()) {
        info = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
        pdf.getTrailer().replaceKey("/Info", info);
    }
    if (!info.hasKey("/Producer"))
        info.replaceKey("/Producer", QPDFObjectHandle::newUnicodeString(defaultProducer()));
    info.replaceKey("/ModDate", QPDFObjectHandle::newString(date));
    QPDFObjectHandle metadata = pdf.getRoot().getKey("/Metadata");
    if (!metadata.isStream())
        return;
    std::optional<std::string> producer;
    if (info.getKey("/Producer").isString())
        producer = info.getKey("/Producer").getUTF8Value();
    const std::shared_ptr<Buffer> data = metadata.getStreamData(qpdf_dl_generalized);
    const std::string packet(reinterpret_cast<const char*>(data->getBuffer()), data->getSize());
    const std::optional<std::string> updated = formats::updateXmpPacket(packet, producer, w3cDate(date));
    if (updated && *updated != packet)
        metadata.replaceStreamData(*updated, QPDFObjectHandle::newNull(), QPDFObjectHandle::newNull());
}

void Tweak::setSignature(const std::wstring& keystore, const std::string& alias,
                         const std::string& password, int certificationLevel, bool visible) {
    signingKey_ = std::make_unique<sign::SigningKey>(keystore, alias, password);
    certificationLevel_ = certificationLevel;
    signatureVisible_ = visible;
}

void Tweak::writeOutput(std::string outputFile, bool multipageTiff, bool burst, bool uncompressed,
                        bool sizeOptimize, bool fullyCompressed) {
    try {
        std::wstring name = fromUtf8(outputFile);
        if (name.find(L'\\') == std::wstring::npos)
            name = std::filesystem::absolute(name).wstring();
        if (sizeOptimize) {
            checkCancelled();
            progress_.stage("Optimising size", 1);
            std::shared_ptr<QPDF> optimized =
                openBytes(mupdf::optimize(writeBytes(*current_)), "optimised document");
            // a new document, as Java's PdfSmartCopy made one
            completeNewInformation(*optimized);
            replaceCurrent(*optimized);
            progress_.advance();
        }
        replaceAll(name, L"<F>", inputFileName_);
        replaceAll(name, L"<FX>", inputFileFullName_);
        replaceAll(name, L"<P>", inputFilePath_);
        if (name.find(L"<#>") != std::wstring::npos) {
            for (int i = 1;; ++i) {
                std::wstring candidate = name;
                replaceAll(candidate, L"<#>", std::to_wstring(i));
                std::error_code ignored;
                if (!std::filesystem::exists(candidate, ignored)) {
                    name = candidate;
                    break;
                }
            }
        }
        if (!burst && isInput(inputFiles_, name))
            throw Error(Error::Kind::Io, "Output file must be different from input file(s)");
        const auto configure = [&](QPDFWriter& writer) {
            configureWriter(writer, uncompressed, fullyCompressed);
        };
        if (multipageTiff) {
            if (name.find(L'*') != std::wstring::npos)
                throw Error(Error::Kind::Io, "TIFF multipage filename should not contain *");
            if (!pdfImages_)
                throw Error(Error::Kind::Io, "No image settings for the multipage TIFF");
            const int total = pageCount();
            progress_.stage("Writing TIFF", 1);
            PageCopier copy;
            for (int page = 1; page <= total; ++page)
                copy.addPage(*current_, page - 1);
            std::shared_ptr<QPDF> all = copy.finish();
            pdfImages_->convertToMultiTiff(writeBytes(*all), name);
            progress_.advance();
        } else if (burst) {
            const size_t star = name.find(L'*');
            if (star == std::wstring::npos)
                throw Error(Error::Kind::Io, "Output filename does not contain *");
            const std::wstring prefix = name.substr(0, star);
            const std::wstring suffix = name.substr(star + 1);
            const int total = pageCount();
            std::vector<std::string> labels;
            if (const auto formats = readPageLabels(*current_))
                labels = formats::labelStrings(*formats, total);
            const bool burstImages = pdfImages_ && pdfImages_->shouldExecute();
            progress_.stage("Bursting", total);
            for (int page = 1; page <= total; ++page) {
                checkCancelled();
                const std::string number = labels.empty() ? std::to_string(page) : labels[page - 1];
                const std::filesystem::path outFile = prefix + fromUtf8(number) + suffix;
                if (isInput(inputFiles_, outFile))
                    throw Error(Error::Kind::Io, "Output file must be different from input file(s)");
                std::error_code ignored;
                if (outFile.has_parent_path() &&
                    !std::filesystem::is_directory(outFile.parent_path(), ignored))
                    std::filesystem::create_directories(outFile.parent_path(), ignored);
                PageCopier copy;
                copy.addPage(*current_, page - 1);
                std::shared_ptr<QPDF> single = copy.finish();
                if (burstImages) {
                    pdfImages_->convertToImage(
                        writeBytes(*single), pdfImages_->imageFileName(prefix + fromUtf8(number), suffix));
                    progress_.advance();
                    continue;
                }
                // a burst page is a new document without the Info or XMP of its source, as in Java
                completeNewInformation(*single);
                finishInformation(*single);
                writeFile(*single, outFile, configure);
                progress_.advance();
            }
        } else {
            checkCancelled();
            progress_.stage("Writing output", 1);
            applyOutputSettings(*current_);
            finishInformation(*current_);
            if (signingKey_) {
                // spec section 7: qpdf writes a temporary file, MuPDF appends the signature to it, and
                // the signed file becomes the output
                createForWriting(name, toUtf8(name));
                const std::filesystem::path temp = temporaryFile();
                try {
                    writeFile(*current_, temp, configure);
                    mupdf::signFile(utf8Path(temp), encryptionMode_ == -1 ? std::string() : ownerPassword_,
                                    *signingKey_, signatureVisible_, certificationLevel_);
                    std::filesystem::copy_file(temp, name,
                                               std::filesystem::copy_options::overwrite_existing);
                } catch (...) {
                    std::error_code ignored;
                    std::filesystem::remove(temp, ignored);
                    throw;
                }
                std::error_code ignored;
                std::filesystem::remove(temp, ignored);
            } else {
                writeFile(*current_, name, configure);
            }
            progress_.advance();
        }
        current_.reset();
        for (const std::filesystem::path* temp : {&tempfile1_, &tempfile2_}) {
            std::error_code failed;
            if (!temp->empty() && !std::filesystem::remove(*temp, failed) && failed)
                throw Error(Error::Kind::Io, "Cannot delete " + utf8Path(*temp));
        }
        tempfile1_.clear();
        tempfile2_.clear();
    } catch (...) {
        rethrowAsError();
    }
}

} // namespace pdftweak::pdf
