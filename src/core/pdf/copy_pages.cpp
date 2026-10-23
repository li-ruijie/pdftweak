#include "core/pdf/copy_pages.h"

#include "core/libraries.h"
#include "core/version.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>

#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace pdftweak::pdf {
namespace {

std::shared_ptr<QPDF> emptyDocument() {
    auto pdf = std::make_shared<QPDF>();
    pdf->emptyPDF();
    pdf->setSuppressWarnings(true);
    return pdf;
}

QPDFObjectHandle number(float value) {
    if (value == static_cast<float>(static_cast<long long>(value)))
        return QPDFObjectHandle::newInteger(static_cast<long long>(value));
    return QPDFObjectHandle::newReal(value, 0, true);
}

bool isPage(QPDFObjectHandle object) {
    return object.isDictionary() && object.getKey("/Type").isNameAndEquals("/Page");
}

// A destination array whose page is not among the pages points nowhere.
void clearDestination(QPDFObjectHandle dest, const std::set<QPDFObjGen>& pages) {
    if (dest.isArray() && dest.getArrayNItems() > 0) {
        QPDFObjectHandle target = dest.getArrayItem(0);
        if (isPage(target) && !pages.contains(target.getObjGen()))
            dest.setArrayItem(0, QPDFObjectHandle::newNull());
    }
}

} // namespace

PageCopier::PageCopier() : out_(emptyDocument()), outForm_(*out_) {}

void PageCopier::addPage(QPDF& source, int index) {
    if (!prepared_.contains(&source)) {
        // copies of pages carry the attributes they inherit from the page tree
        source.pushInheritedAttributesToPage();
        prepared_.insert(&source);
        sourceForms_[&source] = std::make_unique<QPDFAcroFormDocumentHelper>(source);
    }
    QPDFObjectHandle from = pages(source).at(static_cast<size_t>(index)).getObjectHandle();
    QPDFObjectHandle page = out_->copyForeignObject(from);
    if (added_.contains(page.getObjGen()))
        page = QPDFPageObjectHelper(page).shallowCopyPage().getObjectHandle();
    page.removeKey("/B"); // beads refer to article threads, which are not copied
    QPDFPageDocumentHelper(*out_).addPage(QPDFPageObjectHelper(page), false);
    added_.insert(page.getObjGen());
    outForm_.fixCopiedAnnotations(page, from, *sourceForms_[&source]);
}

void PageCopier::addBlankPage(const Rect& size) {
    QPDFObjectHandle page = QPDFObjectHandle::parse("<< /Type /Page /Resources << >> >>");
    page.replaceKey("/MediaBox", QPDFObjectHandle::newArray({number(size.llx), number(size.lly),
                                                             number(size.urx), number(size.ury)}));
    page = out_->makeIndirectObject(page);
    QPDFPageDocumentHelper(*out_).addPage(QPDFPageObjectHelper(page), false);
    added_.insert(page.getObjGen());
}

std::shared_ptr<QPDF> PageCopier::finish() {
    for (QPDFPageObjectHelper& page : pages(*out_)) {
        QPDFObjectHandle annots = page.getObjectHandle().getKey("/Annots");
        if (!annots.isArray())
            continue;
        for (QPDFObjectHandle annot : annots.aitems()) {
            if (!annot.isDictionary())
                continue;
            clearDestination(annot.getKey("/Dest"), added_);
            QPDFObjectHandle action = annot.getKey("/A");
            if (action.isDictionary())
                clearDestination(action.getKey("/D"), added_);
        }
    }
    return out_;
}

void copyXmpMetadata(QPDF& from, QPDF& to) {
    QPDFObjectHandle metadata = from.getRoot().getKey("/Metadata");
    if (!metadata.isStream())
        return;
    const std::shared_ptr<Buffer> data = metadata.getStreamData(qpdf_dl_generalized);
    QPDFObjectHandle copy =
        to.newStream(std::string(reinterpret_cast<const char*>(data->getBuffer()), data->getSize()));
    copy.getDict().replaceKey("/Type", QPDFObjectHandle::newName("/Metadata"));
    copy.getDict().replaceKey("/Subtype", QPDFObjectHandle::newName("/XML"));
    to.getRoot().replaceKey("/Metadata", copy);
}

void copyInformation(QPDF& from, QPDF& to) {
    QPDFObjectHandle info = QPDFObjectHandle::newDictionary();
    QPDFObjectHandle source = from.getTrailer().getKey("/Info");
    if (source.isDictionary())
        for (const std::string& key : source.getKeys())
            info.replaceKey(key, copyFrom(to, source.getKey(key)));
    to.getTrailer().replaceKey("/Info", to.makeIndirectObject(info));
    completeNewInformation(to);
}

QPDFObjectHandle completeNewInformation(QPDF& pdf) {
    QPDFObjectHandle info = pdf.getTrailer().getKey("/Info");
    if (!info.isDictionary()) {
        info = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
        pdf.getTrailer().replaceKey("/Info", info);
    }
    if (!info.hasKey("/Producer"))
        info.replaceKey("/Producer", QPDFObjectHandle::newUnicodeString(defaultProducer()));
    if (!info.hasKey("/CreationDate"))
        info.replaceKey("/CreationDate", QPDFObjectHandle::newString(pdfDateNow()));
    return info;
}

std::string defaultProducer() {
    const LibraryVersions v = libraryVersions();
    return PDFTWEAK_PROGRAM_NAME " " PDFTWEAK_VERSION_STRING " (based on qpdf " + v.qpdf + " and MuPDF " +
           v.mupdf + ")";
}

std::string pdfDateNow() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::tm copy = local;
    const long long offsetMinutes = static_cast<long long>(_mkgmtime(&copy) - now) / 60;
    char text[40];
    std::snprintf(text, sizeof text, "D:%04d%02d%02d%02d%02d%02d", local.tm_year + 1900, local.tm_mon + 1,
                  local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
    std::string date = text;
    // iText writes Z when the offset is less than an hour, and whole hours and minutes otherwise
    const long long hours = offsetMinutes / 60;
    if (hours == 0)
        return date + "Z";
    std::snprintf(text, sizeof text, "%c%02lld'%02lld'", offsetMinutes < 0 ? '-' : '+', std::llabs(hours),
                  std::llabs(offsetMinutes) - std::llabs(hours) * 60);
    return date + text;
}

std::string w3cDate(const std::string& pdfDate) {
    std::string d = pdfDate.rfind("D:", 0) == 0 ? pdfDate.substr(2) : pdfDate;
    const auto part = [&](size_t at, size_t length, const char* fallback) {
        return d.size() >= at + length ? d.substr(at, length) : std::string(fallback);
    };
    std::string w3c = part(0, 4, "0000") + "-" + part(4, 2, "01") + "-" + part(6, 2, "01") + "T" +
                      part(8, 2, "00") + ":" + part(10, 2, "00") + ":" + part(12, 2, "00");
    if (d.size() > 14) {
        const char sign = d[14];
        if (sign == 'Z') {
            w3c += "Z";
        } else if (sign == '+' || sign == '-') {
            std::string hours = d.substr(15, 2);
            std::string minutes = d.size() >= 20 ? d.substr(18, 2) : "00";
            w3c += std::string(1, sign) + hours + ":" + minutes;
        }
    }
    return w3c;
}

} // namespace pdftweak::pdf
