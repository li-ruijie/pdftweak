#pragma once

// The PdfStamper route of PdfTweak for content: content added under and over the existing content
// of pages, as iText's PdfStamperImp and StampContent write it. Each page that gets content gets a
// q at the start of its content and the added content in streams of their own, turned with the
// page's /Rotate so that it is upright to the viewer. Added resources are named Xi0, Xi1, and so on
// with a counter shared by all pages, skipping the names the page's resources already use.

#include "core/pdf/qpdf_util.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pdftweak::pdf {

class Stamper {
    struct PageStamp;

public:
    // PdfContentByte, for the operators the watermark and page numbers use. Text is shown in the
    // Helvetica of helvetica.h, whatever font object setFontAndSize names.
    class Content {
    public:
        void saveState() { buffer_ += "q\n"; }
        void restoreState() { buffer_ += "Q\n"; }
        void beginText() { buffer_ += "BT\n"; }
        void endText() { buffer_ += "ET\n"; }
        void setGState(QPDFObjectHandle gstate);
        void setRgbFill(int red, int green, int blue);
        // Throws Error(Document) for a size between -0.0001 and 0.0001, as iText refuses it.
        void setFontAndSize(QPDFObjectHandle font, float size);
        void setTextMatrix(float a, float b, float c, float d, float x, float y);
        void showText(std::string_view utf8);
        // showTextAligned(ALIGN_CENTER, text, x, y, 0)
        void showTextCentered(std::string_view utf8, float x, float y);
        // addTemplate(form, x, y)
        void addTemplate(QPDFObjectHandle form, float x, float y);

    private:
        friend class Stamper;
        Content(Stamper& stamper, PageStamp& page) : stamper_(stamper), page_(page) {}
        std::string resourceName(const std::string& category, QPDFObjectHandle resource);

        Stamper& stamper_;
        PageStamp& page_;
        std::string buffer_;
        float size_ = 0;
    };

    explicit Stamper(QPDF& pdf);
    ~Stamper();
    Stamper(const Stamper&) = delete;
    Stamper& operator=(const Stamper&) = delete;

    // getUnderContent and getOverContent of page n (1-based).
    Content& under(int n);
    Content& over(int n);

    // PdfStamperImp.alterContents and alterResources: writes the content into the pages.
    void finish();

private:
    PageStamp& pageStamp(int n);

    QPDF& pdf_;
    std::vector<QPDFPageObjectHelper> pages_;
    std::map<int, std::unique_ptr<PageStamp>> stamps_;
    int namePtr_ = 0;
};

} // namespace pdftweak::pdf
