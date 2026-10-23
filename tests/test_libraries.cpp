// Checks that the five native libraries are linked and work together. qpdf writes a compressed
// PDF, MuPDF opens and renders it, and libtiff writes every compression jPDF Tweak offers.

#include "runner.h"

#include "core/libraries.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>
#include <tiffio.h>

#include <string>
#include <vector>

namespace {

// fz_try uses setjmp, so this function holds no object with a destructor.
#pragma warning(push)
#pragma warning(disable : 4611) // interaction between _setjmp and C++ object destruction
int mupdfRender(const std::string& pdf, int* pageCount, int* width, int* height) {
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (ctx == nullptr)
        return 0;
    int ok = 0;
    fz_buffer* buffer = nullptr;
    fz_stream* stream = nullptr;
    pdf_document* doc = nullptr;
    fz_pixmap* pixmap = nullptr;
    fz_var(buffer);
    fz_var(stream);
    fz_var(doc);
    fz_var(pixmap);
    fz_try(ctx) {
        buffer = fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char*>(pdf.data()),
                                                pdf.size());
        stream = fz_open_buffer(ctx, buffer);
        doc = pdf_open_document_with_stream(ctx, stream);
        *pageCount = pdf_count_pages(ctx, doc);
        pixmap = fz_new_pixmap_from_page_number(ctx, &doc->super, 0, fz_identity, fz_device_rgb(ctx), 0);
        *width = fz_pixmap_width(ctx, pixmap);
        *height = fz_pixmap_height(ctx, pixmap);
        ok = 1;
    }
    fz_always(ctx) {
        fz_drop_pixmap(ctx, pixmap);
        pdf_drop_document(ctx, doc);
        fz_drop_stream(ctx, stream);
        fz_drop_buffer(ctx, buffer);
    }
    fz_catch(ctx) { ok = 0; }
    fz_drop_context(ctx);
    return ok;
}
#pragma warning(pop)

std::string onePageLetterPdf() {
    QPDF qpdf;
    qpdf.emptyPDF();
    QPDFObjectHandle page =
        QPDFObjectHandle::parse("<< /Type /Page /MediaBox [0 0 612 792] /Resources << >> >>");
    page.replaceKey("/Contents", qpdf.newStream("0 0 1 rg 72 72 468 648 re f"));
    QPDFPageDocumentHelper(qpdf).addPage(QPDFPageObjectHelper(qpdf.makeIndirectObject(page)), false);
    QPDFWriter writer(qpdf);
    writer.setOutputMemory();
    writer.setCompressStreams(true);
    writer.write();
    const std::shared_ptr<Buffer> buffer = writer.getBufferSharedPointer();
    return std::string(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
}

} // namespace

TEST(unit_libraryVersionsMatchThePins) {
    const pdftweak::LibraryVersions v = pdftweak::libraryVersions();
    CHECK_EQUAL(v.qpdf, std::string("12.4.2"));
    CHECK_EQUAL(v.mupdf, std::string("1.28.5"));
    CHECK_EQUAL(v.libtiff, std::string("4.7.2"));
}

TEST(unit_mupdfRendersACompressedPdfFromQpdf) {
    const std::string pdf = onePageLetterPdf();
    CHECK(pdf.find("/FlateDecode") != std::string::npos);
    int pages = 0, width = 0, height = 0;
    CHECK(mupdfRender(pdf, &pages, &width, &height));
    CHECK_EQUAL(pages, 1);
    CHECK_EQUAL(width, 612);
    CHECK_EQUAL(height, 792);
}

TEST(unit_libtiffWritesEveryCompression) {
    const uint16_t compressions[] = {COMPRESSION_NONE, COMPRESSION_LZW,           COMPRESSION_PACKBITS,
                                     COMPRESSION_JPEG, COMPRESSION_ADOBE_DEFLATE, COMPRESSION_DEFLATE};
    for (const uint16_t compression : compressions) {
        CHECK(TIFFIsCODECConfigured(compression));
        const std::string name = "compression-" + std::to_string(compression) + ".tif";
        TIFF* out = TIFFOpen(name.c_str(), "w");
        CHECK(out != nullptr);
        TIFFSetField(out, TIFFTAG_IMAGEWIDTH, 16);
        TIFFSetField(out, TIFFTAG_IMAGELENGTH, 16);
        TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, 3);
        TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, 8);
        TIFFSetField(out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        TIFFSetField(out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
        TIFFSetField(out, TIFFTAG_COMPRESSION, compression);
        TIFFSetField(out, TIFFTAG_ROWSPERSTRIP, 16);
        std::vector<unsigned char> row(16 * 3, 0x80);
        for (uint32_t y = 0; y < 16; ++y)
            CHECK(TIFFWriteScanline(out, row.data(), y, 0) == 1);
        TIFFClose(out);

        TIFF* in = TIFFOpen(name.c_str(), "r");
        CHECK(in != nullptr);
        uint32_t width = 0;
        uint16_t written = 0;
        TIFFGetField(in, TIFFTAG_IMAGEWIDTH, &width);
        TIFFGetField(in, TIFFTAG_COMPRESSION, &written);
        TIFFClose(in);
        CHECK_EQUAL(width, 16u);
        CHECK_EQUAL(written, compression);
    }
}
