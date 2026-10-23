#pragma once

// The PdfWriter route of PdfTweak: a new document whose pages draw pages of the current document
// as Form XObjects (iText's PdfImportedPage), and PageRemap, which carries over the catalog entries,
// destinations, and links that point at those pages (decision 16).

#include "core/pdf/qpdf_util.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pdftweak::pdf {

// A PDF matrix a b c d e f, in the float arithmetic of the Java code.
using Matrix = std::array<float, 6>;

class PageBuilder {
public:
    explicit PageBuilder(QPDF& source);

    QPDF& document() { return *out_; }

    // Document.newPage with a page of the given MediaBox. Output pages are numbered from 1.
    void newPage(const Rect& mediaBox);
    int pageNumber() const { return pageNumber_; }

    // The page object of output page n, which may be created before the page is started, as
    // PdfWriter.getPageReference reserves it.
    QPDFObjectHandle pageReference(int n);

    // PdfContentByte.addTemplate of the imported page (1-based) of the source under matrix m.
    void addTemplate(int sourcePage, const Matrix& m);

    // Content appended to the current page, such as the frame of a shuffle rule.
    void appendContent(const std::string& content) { content_ += content; }

    // Adds an annotation to the current page.
    void addAnnotation(QPDFObjectHandle annotation);

    // Ends the last page and returns the document.
    std::shared_ptr<QPDF> finish();

private:
    void endPage();
    QPDFObjectHandle importedPage(int sourcePage);

    QPDF& source_;
    std::vector<QPDFPageObjectHelper> sourcePages_;
    std::shared_ptr<QPDF> out_;
    std::map<int, QPDFObjectHandle> reserved_;
    std::map<int, std::pair<std::string, QPDFObjectHandle>> templates_;
    int pageNumber_ = 0;
    QPDFObjectHandle page_;
    std::string content_;
};

// PageRemap: where each input page appears in the output, and the copying of what points at pages.
class PageRemap {
public:
    PageRemap(QPDF& source, PageBuilder& builder);

    // Records that inputPage is shown on outputPage through m, unless an earlier output page shows it.
    void place(int inputPage, int outputPage, const Matrix& m);

    // Adds the links of inputPage, placed through m, to the current output page. Call it after
    // placing every page.
    void copyLinks(int inputPage, const Matrix& m);

    // Copies the kept catalog entries, the named destinations and attachments of /Names, and the page
    // labels if keepPageLabels. Call it after placing every page.
    void copyCatalog(bool keepPageLabels);

private:
    QPDFObjectHandle copy(QPDFObjectHandle object);
    QPDFObjectHandle copyDirect(QPDFObjectHandle object);
    bool pointsNowhere(QPDFObjectHandle object) const;
    int inputPage(QPDFObjectHandle array) const;
    QPDFObjectHandle destination(QPDFObjectHandle dest);

    QPDF& source_;
    PageBuilder& builder_;
    QPDF& out_;
    std::map<QPDFObjGen, int> inputPages_;
    std::vector<int> outputPages_;
    std::vector<Matrix> matrices_;
    std::map<QPDFObjGen, QPDFObjectHandle> copies_;
};

} // namespace pdftweak::pdf
