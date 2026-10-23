// The page operations of PdfTweak: cropPages, rotatePages, removeRotation, scalePages, and
// shufflePages.

#include "core/pdf/tweak.h"

#include "core/error.h"
#include "core/java_text.h"
#include "core/pdf/copy_pages.h"
#include "core/pdf/rebuild.h"

#include <cmath>
#include <cstdlib>
#include <optional>

namespace pdftweak::pdf {
namespace {

// PdfReader.getBoxSize: the box of a page, inherited only where PDF inherits it, or nothing.
std::optional<Rect> boxSize(QPDFObjectHandle page, PageBox box) {
    static const char* const names[] = {"/MediaBox", "/CropBox", "/BleedBox", "/TrimBox", "/ArtBox"};
    QPDFObjectHandle array = QPDFPageObjectHelper(page).getAttribute(names[static_cast<int>(box)], false);
    if (!array.isArray())
        return std::nullopt;
    return rectangle(array);
}

// PageBox.defaultBox: the box each one falls back to.
std::optional<PageBox> fallback(PageBox box) {
    switch (box) {
    case PageBox::MediaBox: return std::nullopt;
    case PageBox::CropBox: return PageBox::MediaBox;
    case PageBox::BleedBox: return PageBox::CropBox;
    case PageBox::TrimBox: return PageBox::CropBox;
    case PageBox::ArtBox: return PageBox::TrimBox;
    }
    return std::nullopt;
}

// uprightMatrix: the matrix that draws a page with the given /Rotate upright on a page of its
// rotated size.
Matrix uprightMatrix(int rotation, const Rect& rotatedSize) {
    switch (rotation) {
    case 0: return {1, 0, 0, 1, 0, 0};
    case 90: return {0, -1, 1, 0, 0, rotatedSize.height()};
    case 180: return {-1, 0, 0, -1, rotatedSize.width(), rotatedSize.height()};
    case 270: return {0, 1, -1, 0, rotatedSize.width(), 0};
    default: throw Error(Error::Kind::Io, "Unparsable rotation value: " + std::to_string(rotation));
    }
}

// placement: the matrix that places a page of the given size by a shuffle rule.
Matrix placement(const formats::ShuffleRule& sr, const Rect& size) {
    const float s = static_cast<float>(sr.scale);
    float offsetx = static_cast<float>(sr.offsetX);
    float offsety = static_cast<float>(sr.offsetY);
    if (sr.offsetXPercent)
        offsetx = offsetx * size.width() / 100;
    if (sr.offsetYPercent)
        offsety = offsety * size.height() / 100;
    switch (sr.rotate) {
    case 'N': return {s, 0, 0, s, offsetx * s, offsety * s};
    case 'R': return {0, -s, s, 0, offsety * s, -offsetx * s};
    case 'U': return {-s, 0, 0, -s, -offsetx * s, -offsety * s};
    case 'L': return {0, s, -s, 0, -offsety * s, offsetx * s};
    default: throw Error(Error::Kind::Document, std::string(1, sr.rotate));
    }
}

} // namespace

void Tweak::cropPages(PageBox cropTo) {
    try {
        QPDF& source = *current_;
        const std::vector<QPDFPageObjectHelper> sourcePages = pages(source);
        const int count = static_cast<int>(sourcePages.size());
        PageBuilder builder(source);
        PageRemap remap(source, builder);
        std::vector<Rect> sizes(static_cast<size_t>(count) + 1);
        std::vector<Matrix> matrices(static_cast<size_t>(count) + 1);
        std::vector<int> rotations(static_cast<size_t>(count) + 1);
        for (int i = 1; i <= count; ++i) {
            QPDFObjectHandle page = sourcePages[static_cast<size_t>(i - 1)].getObjectHandle();
            const Rect pageSize = pdf::pageSize(page);
            std::optional<Rect> currentSize;
            for (std::optional<PageBox> box = cropTo; box && !currentSize; box = fallback(*box))
                currentSize = boxSize(page, *box);
            sizes[static_cast<size_t>(i)] = currentSize.value_or(pageSize);
            const Rect& size = sizes[static_cast<size_t>(i)];
            matrices[static_cast<size_t>(i)] = {
                1, 0, 0, 1, pageSize.llx - size.llx, pageSize.lly - size.lly};
            rotations[static_cast<size_t>(i)] = pageRotation(page);
            remap.place(i, i, matrices[static_cast<size_t>(i)]);
        }
        progress_.stage("Cropping", count);
        for (int i = 1; i <= count; ++i) {
            checkCancelled();
            const Rect& size = sizes[static_cast<size_t>(i)];
            builder.newPage({0, 0, size.width(), size.height()});
            builder.addTemplate(i, matrices[static_cast<size_t>(i)]);
            if (preserveHyperlinks_)
                remap.copyLinks(i, matrices[static_cast<size_t>(i)]);
            progress_.advance();
        }
        remap.copyCatalog(true);
        std::shared_ptr<QPDF> result = builder.finish();
        copyXmpMetadata(source, *result);
        copyInformation(source, *result);
        // restore rotation
        int i = 1;
        for (QPDFPageObjectHelper& page : pages(*result))
            page.getObjectHandle().replaceKey(
                "/Rotate", QPDFObjectHandle::newInteger(rotations[static_cast<size_t>(i++)]));
        replaceCurrent(*result);
    } catch (...) {
        rethrowAsError();
    }
}

void Tweak::rotatePages(int portraitCount, int landscapeCount) {
    try {
        const std::vector<QPDFPageObjectHelper> all = pages(*current_);
        progress_.stage("Rotating", static_cast<long long>(all.size()));
        for (const QPDFPageObjectHelper& helper : all) {
            checkCancelled();
            QPDFObjectHandle page = helper.getObjectHandle();
            const Rect r = pageSizeWithRotation(page);
            const int count = r.width() > r.height() ? landscapeCount : portraitCount;
            const int rotation = (pageRotation(page) + 90 * count) % 360;
            page.replaceKey("/Rotate", QPDFObjectHandle::newInteger(rotation));
            progress_.advance();
        }
    } catch (...) {
        rethrowAsError();
    }
}

void Tweak::removeRotation() {
    try {
        QPDF& source = *current_;
        const std::vector<QPDFPageObjectHelper> sourcePages = pages(source);
        bool needed = false;
        for (const QPDFPageObjectHelper& page : sourcePages)
            if (pageRotation(page.getObjectHandle()) != 0)
                needed = true;
        if (!needed)
            return;
        const int count = static_cast<int>(sourcePages.size());
        PageBuilder builder(source);
        PageRemap remap(source, builder);
        std::vector<Matrix> matrices(static_cast<size_t>(count) + 1);
        for (int i = 1; i <= count; ++i) {
            QPDFObjectHandle page = sourcePages[static_cast<size_t>(i - 1)].getObjectHandle();
            matrices[static_cast<size_t>(i)] =
                uprightMatrix(pageRotation(page), pageSizeWithRotation(page));
            remap.place(i, i, matrices[static_cast<size_t>(i)]);
        }
        progress_.stage("Removing rotation", count);
        for (int i = 1; i <= count; ++i) {
            checkCancelled();
            const Rect size =
                pageSizeWithRotation(sourcePages[static_cast<size_t>(i - 1)].getObjectHandle());
            builder.newPage({0, 0, size.width(), size.height()}); // strip the origin
            builder.addTemplate(i, matrices[static_cast<size_t>(i)]);
            if (preserveHyperlinks_)
                remap.copyLinks(i, matrices[static_cast<size_t>(i)]);
            progress_.advance();
        }
        remap.copyCatalog(true);
        std::shared_ptr<QPDF> result = builder.finish();
        copyXmpMetadata(source, *result);
        copyInformation(source, *result);
        replaceCurrent(*result);
    } catch (...) {
        rethrowAsError();
    }
}

void Tweak::scalePages(float newWidth, float newHeight, bool noEnlarge, bool preserveAspectRatio) {
    removeRotation();
    try {
        QPDF& source = *current_;
        const std::vector<QPDFPageObjectHelper> sourcePages = pages(source);
        const int count = static_cast<int>(sourcePages.size());
        PageBuilder builder(source);
        PageRemap remap(source, builder);
        std::vector<Matrix> matrices(static_cast<size_t>(count) + 1);
        for (int i = 1; i <= count; ++i) {
            QPDFObjectHandle page = sourcePages[static_cast<size_t>(i - 1)].getObjectHandle();
            const Rect currentSize = pageSizeWithRotation(page);
            if (pageRotation(page) != 0)
                throw Error(Error::Kind::Document, std::to_string(pageRotation(page)));
            float factorX = newWidth / currentSize.width();
            float factorY = newHeight / currentSize.height();
            if (noEnlarge) {
                if (factorX > 1)
                    factorX = 1;
                if (factorY > 1)
                    factorY = 1;
            }
            if (preserveAspectRatio) {
                factorX = std::min(factorX, factorY);
                factorY = factorX;
            }
            const float offsetX = (newWidth - (currentSize.width() * factorX)) / 2.0f;
            const float offsetY = (newHeight - (currentSize.height() * factorY)) / 2.0f;
            matrices[static_cast<size_t>(i)] = {factorX, 0, 0, factorY, offsetX, offsetY};
            remap.place(i, i, matrices[static_cast<size_t>(i)]);
        }
        progress_.stage("Scaling", count);
        for (int i = 1; i <= count; ++i) {
            checkCancelled();
            builder.newPage({0, 0, newWidth, newHeight});
            builder.addTemplate(i, matrices[static_cast<size_t>(i)]);
            if (preserveHyperlinks_)
                remap.copyLinks(i, matrices[static_cast<size_t>(i)]);
            progress_.advance();
        }
        remap.copyCatalog(true);
        std::shared_ptr<QPDF> result = builder.finish();
        copyXmpMetadata(source, *result);
        copyInformation(source, *result);
        replaceCurrent(*result);
    } catch (...) {
        rethrowAsError();
    }
}

void Tweak::shufflePages(const formats::RuleSet& ruleSet) {
    removeRotation();
    try {
        QPDF& source = *current_;
        const std::vector<QPDFPageObjectHelper> sourcePages = pages(source);
        const Rect size = pageSize(sourcePages.at(0).getObjectHandle());
        for (const QPDFPageObjectHelper& page : sourcePages) {
            const Rect r = pageSize(page.getObjectHandle());
            if (r.width() != size.width() || r.height() != size.height())
                throw Error(Error::Kind::Io,
                            "Pages must have equals sizes to be shuffled. Use the Scale option on "
                            "the PageSize tab first.");
            if (pageRotation(page.getObjectHandle()) != 0)
                throw Error(Error::Kind::Document, "java.lang.RuntimeException");
        }
        const int passLength = ruleSet.passLength;
        const int blockSize = ruleSet.blockSize;
        const int pl = std::abs(passLength);
        const int cnt = static_cast<int>(sourcePages.size());
        const int passes = blockSize == 0 ? 1 : (cnt + blockSize - 1) / blockSize;
        PageBuilder builder(source);
        PageRemap remap(source, builder);
        // Calls visit(rule, input page, new page before) for every rule of every pass, in order.
        const auto walk = [&](const auto& visit) {
            for (int pass = 0; pass < passes; ++pass) {
                const int passcnt = pass == passes - 1 ? cnt - pass * blockSize : blockSize;
                const int refcnt = ((passcnt + (pl - 1)) / pl) * pl;
                for (int i = 0; i < passcnt; i += pl) {
                    int idx = i;
                    int reverseIdx = refcnt - idx - pl;
                    if (passLength < 0) {
                        idx = i / 2;
                        reverseIdx = refcnt - idx - pl;
                    }
                    idx += pass * blockSize;
                    reverseIdx += pass * blockSize;
                    for (const formats::ShuffleRule& sr : ruleSet.rules) {
                        int pg = sr.page;
                        if (sr.base == formats::ShuffleRule::Base::Beginning)
                            pg += idx;
                        else if (sr.base == formats::ShuffleRule::Base::End)
                            pg += reverseIdx;
                        if (pg < 1)
                            throw Error(Error::Kind::Io, "Invalid page number. Check your n-up rules.");
                        visit(sr, pg);
                    }
                }
            }
        };
        int ddPage = 0;
        walk([&](const formats::ShuffleRule& sr, int pg) {
            if (sr.newPageBefore)
                ++ddPage;
            if (pg <= cnt)
                remap.place(pg, ddPage, placement(sr, size));
        });
        progress_.stage("Shuffling", ddPage);
        walk([&](const formats::ShuffleRule& sr, int pg) {
            if (sr.newPageBefore) {
                checkCancelled();
                if (builder.pageNumber() > 0)
                    progress_.advance();
                builder.newPage(size);
            }
            if (pg > cnt)
                return; // the page stays, possibly empty
            const Matrix m = placement(sr, size);
            builder.addTemplate(pg, m);
            if (preserveHyperlinks_)
                remap.copyLinks(pg, m);
            if (sr.frameWidth > 0) {
                const float w = m[0] * size.width() + m[2] * size.height();
                const float h = m[1] * size.width() + m[3] * size.height();
                builder.appendContent(itextNumber(static_cast<float>(sr.frameWidth)) + " w\n" +
                                      itextNumber(m[4]) + " " + itextNumber(m[5]) + " " + itextNumber(w) +
                                      " " + itextNumber(h) + " re\nS\n");
            }
        });
        progress_.advance();
        remap.copyCatalog(false); // the page order and count change
        std::shared_ptr<QPDF> result = builder.finish();
        copyXmpMetadata(source, *result);
        copyInformation(source, *result);
        replaceCurrent(*result);
    } catch (...) {
        rethrowAsError();
    }
}

} // namespace pdftweak::pdf
