#pragma once

// The FormCopy route of PdfTweak: a new document built from pages of other documents, as iText's
// PdfCopy builds it. Each copied page keeps its annotations and form fields (fixCopiedAnnotations),
// the form fields of every source join the new AcroForm (spec section 3, case 1, where Java kept
// those of the first file only), and a destination that points to a page left out points nowhere,
// as PdfCopy leaves it.

#include "core/pdf/qpdf_util.h"

#include <qpdf/QPDFAcroFormDocumentHelper.hh>

#include <map>
#include <memory>
#include <set>

namespace pdftweak::pdf {

class PageCopier {
public:
    PageCopier();

    // Appends page index (0-based) of source. The same page may be added more than once.
    void addPage(QPDF& source, int index);

    // Appends an empty page of the given size, as PdfCopy.addPage(Rectangle, 0) does.
    void addBlankPage(const Rect& size);

    QPDF& document() { return *out_; }

    // Clears destinations that point to pages that were not added and returns the document.
    std::shared_ptr<QPDF> finish();

private:
    std::shared_ptr<QPDF> out_;
    QPDFAcroFormDocumentHelper outForm_;
    std::map<QPDF*, std::unique_ptr<QPDFAcroFormDocumentHelper>> sourceForms_;
    std::set<QPDF*> prepared_;
    std::set<QPDFObjGen> added_;
};

// copyXMPMetadata: copies the XMP packet of from, decoded, into a new uncompressed metadata stream
// of to.
void copyXmpMetadata(QPDF& from, QPDF& to);

// copyInformation for the new-document steps (decision 10): the Info entries of from replace those
// of to, and CreationDate and Producer are added when from has none.
void copyInformation(QPDF& from, QPDF& to);

// Adds CreationDate (now) and Producer to the Info of a new document where they are missing, as
// iText's PdfWriter supplied both, creating the Info dictionary if need be. Returns it.
QPDFObjectHandle completeNewInformation(QPDF& pdf);

// The Producer of documents PDF Tweak creates (decision 10), such as
// "PDF Tweak 2.0.0 (based on qpdf 12.4.2 and MuPDF 1.28.5)", with the library versions of the build.
std::string defaultProducer();

// The current time as a PDF date, D:YYYYMMDDHHmmSS+HH'mm' in local time, as iText's PdfDate.
std::string pdfDateNow();

// A PDF date as a W3C date (YYYY-MM-DDThh:mm:ss+hh:mm), as iText's PdfDate.getW3CDate.
std::string w3cDate(const std::string& pdfDate);

} // namespace pdftweak::pdf
