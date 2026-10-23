// Self-tests of the fixtures and inspection helpers that the operation tests rely on.

#include "runner.h"

#include "fixtures.h"
#include "inspect.h"

#include <windows.h>
#include <ncrypt.h>
#include <wincrypt.h>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>
#include <tiffio.h>

#include <cmath>
#include <string>
#include <vector>

using namespace pdftweak::test;

namespace {

// "Page 1" in Helvetica 12 is 38.028 points wide (P 667, a g e 556, space 278, 1 556).
const float kPage1Width = 38.028f;

bool approx(float a, float b) {
    return std::fabs(a - b) < 0.01f;
}

} // namespace

TEST(unit_fixturesSayPageN) {
    const auto pdf = fixture(".", "fixture.pdf", 3, 1, 2);
    CHECK_EQUAL(pageCount(pdf), 3);
    CHECK_EQUAL(text(pdf, 2), std::string("Page 2"));
    const auto box = mediaBox(pdf, 3);
    CHECK(box[0] == 0 && box[1] == 0 && box[2] == 595 && box[3] == 842);
    CHECK(content(pdf, 1).find("36 806 Td\n0 -18 Td\n/F1 12 Tf\n(Page 1)Tj") != std::string::npos);

    const auto runs = textPositions(pdf, 1);
    CHECK_EQUAL(runs.size(), size_t(1));
    CHECK_EQUAL(runs[0].text, std::string("Page 1"));
    CHECK(approx(runs[0].x, 36 + kPage1Width / 2));
    CHECK(approx(runs[0].y, 788));
    CHECK_EQUAL(runs[0].angle, 0.0f);

    // The link on page 1 points at the top-left corner of page 2.
    const auto qpdf = openPdf(pdf);
    const auto pages = QPDFPageDocumentHelper(*qpdf).getAllPages();
    QPDFObjectHandle dest = pages[0].getObjectHandle().getKey("/Annots").getArrayItem(0).getKey("/Dest");
    CHECK(dest.getArrayItem(0).getObjGen() == pages[1].getObjectHandle().getObjGen());
    CHECK_EQUAL(dest.getArrayItem(1).getName(), std::string("/XYZ"));
    CHECK_EQUAL(dest.getArrayItem(3).getNumericValue(), 842.0);

    const auto other = pdfWithPages(".", "pages.pdf", "Chapter ", 2);
    CHECK_EQUAL(text(other, 2), std::string("Chapter 2"));
    CHECK_EQUAL(text(pdfWithText(".", "text.pdf", "Report (über)"), 1), std::string("Report (über)"));
}

TEST(unit_fixturesCarryInfoXmpAndAForm) {
    FixtureOptions options;
    options.info = {{"Title", "Kept title"}, {"Author", "Someone"}};
    options.xmp = true;
    options.textField = true;
    const auto pdf = writePdf(".", "form.pdf", {{595, 842, "Page 1"}}, options);
    const auto qpdf = openPdf(pdf);
    QPDFObjectHandle info = qpdf->getTrailer().getKey("/Info");
    CHECK_EQUAL(info.getKey("/Title").getUTF8Value(), std::string("Kept title"));
    CHECK_EQUAL(info.getKey("/Author").getUTF8Value(), std::string("Someone"));
    CHECK(info.getKey("/Producer").getUTF8Value().starts_with("iText® 5.5.13.6"));
    CHECK(qpdf->getRoot().getKey("/Metadata").isStream());
    QPDFObjectHandle field = qpdf->getRoot().getKey("/AcroForm").getKey("/Fields").getArrayItem(0);
    CHECK_EQUAL(field.getKey("/T").getUTF8Value(), std::string("name"));
    CHECK_EQUAL(field.getKey("/V").getUTF8Value(), std::string("value"));
}

TEST(unit_inspectReadsBoxesAndText) {
    const auto sizes = pdfOfSizes(".", "sizes.pdf", {{595.0f, 842.0f}, {842.0f, 595.0f}});
    const auto rotated = setPageEntries(sizes, "rotated.pdf", 2, {{"/Rotate", "-270"}});
    CHECK_EQUAL(rotation(rotated, 1), 0);
    CHECK_EQUAL(rotation(rotated, 2), 90);
    const auto box = mediaBox(rotated, 2);
    CHECK(box[2] == 842 && box[3] == 595);

    // Positions are in user space, whatever the rotation of the page.
    const auto runs = textPositions(rotated, 2);
    CHECK_EQUAL(runs.size(), size_t(1));
    CHECK_EQUAL(runs[0].text, std::string("Page 2"));
    CHECK(approx(runs[0].x, 36 + kPage1Width / 2));
    CHECK(approx(runs[0].y, 559 - 18));
    CHECK_EQUAL(runs[0].angle, 0.0f);

    // A diagonal run is reported by its centre and its angle.
    QPDF pdf;
    pdf.emptyPDF();
    QPDFObjectHandle page = QPDFObjectHandle::parse(
        "<< /Type /Page /MediaBox [0 0 595 842] /Resources << /Font << /F1 << /Type /Font /Subtype /Type1 "
        "/BaseFont /Helvetica /Encoding /WinAnsiEncoding >> >> >> >>");
    page.replaceKey("/Contents", pdf.newStream("q 1 0 0 1 10 20 cm 0.7071 0.7071 -0.7071 0.7071 100 200 cm "
                                               "BT /F1 12 Tf 0 0 Td (Page 1) Tj ET Q"));
    QPDFPageDocumentHelper(pdf).addPage(QPDFPageObjectHelper(pdf.makeIndirectObject(page)), false);
    QPDFWriter writer(pdf, "placed.pdf");
    writer.write();
    const auto matrices = placements("placed.pdf", 1);
    CHECK_EQUAL(matrices.size(), size_t(2));
    CHECK(matrices[0] == (std::array<float, 6>{1, 0, 0, 1, 10, 20}));
    CHECK(matrices[1] == (std::array<float, 6>{0.7071f, 0.7071f, -0.7071f, 0.7071f, 100, 200}));
    const auto diagonal = textPositions("placed.pdf", 1);
    CHECK_EQUAL(diagonal.size(), size_t(1));
    CHECK(std::fabs(diagonal[0].angle - 45) < 0.01f);
    const float half = kPage1Width / 2 * 0.7071f;
    CHECK(approx(diagonal[0].x, 110 + half));
    CHECK(approx(diagonal[0].y, 220 + half));
}

TEST(unit_tiffTagsReadAWrittenTiff) {
    TIFF* out = TIFFOpen("two.tif", "w");
    CHECK(out != nullptr);
    for (int page = 0; page < 2; ++page) {
        TIFFSetField(out, TIFFTAG_IMAGEWIDTH, 16);
        TIFFSetField(out, TIFFTAG_IMAGELENGTH, 8);
        TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, 3);
        TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, 8);
        TIFFSetField(out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        TIFFSetField(out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
        TIFFSetField(out, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
        TIFFSetField(out, TIFFTAG_ROWSPERSTRIP, 4);
        std::vector<unsigned char> row(16 * 3, 0x40);
        for (uint32_t y = 0; y < 8; ++y)
            CHECK(TIFFWriteScanline(out, row.data(), y, 0) == 1);
        CHECK(TIFFWriteDirectory(out) == 1);
    }
    TIFFClose(out);
    const std::string bytes = readFile("two.tif");
    CHECK_EQUAL(tiffPageCount(bytes), 2);
    auto tags = tiffTags(bytes);
    CHECK(tags[256] == std::vector<long long>{16});
    CHECK(tags[257] == std::vector<long long>{8});
    CHECK(tags[258] == (std::vector<long long>{8, 8, 8}));
    CHECK(tags[259] == std::vector<long long>{COMPRESSION_LZW});
    CHECK_EQUAL(tags[273].size(), size_t(2));
}

TEST(unit_pkcs12HoldsTheKeyMykey) {
    const std::string pfx = readFile(pkcs12("."));
    CRYPT_DATA_BLOB blob{static_cast<DWORD>(pfx.size()),
                         reinterpret_cast<BYTE*>(const_cast<char*>(pfx.data()))};
    CHECK(PFXVerifyPassword(&blob, L"secret", 0));
    HCERTSTORE store = PFXImportCertStore(&blob, L"secret", PKCS12_NO_PERSIST_KEY | PKCS12_ALWAYS_CNG_KSP);
    CHECK(store != nullptr);
    PCCERT_CONTEXT cert = CertEnumCertificatesInStore(store, nullptr);
    CHECK(cert != nullptr);
    wchar_t name[64] = L"";
    DWORD size = sizeof name;
    CHECK(CertGetCertificateContextProperty(cert, CERT_FRIENDLY_NAME_PROP_ID, name, &size));
    CHECK(std::wstring(name) == L"mykey");
    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key = 0;
    DWORD spec = 0;
    BOOL mustFree = FALSE;
    // An imported key that is not persisted is only reachable through the cached handle.
    const DWORD flags = CRYPT_ACQUIRE_CACHE_FLAG | CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG;
    if (!CryptAcquireCertificatePrivateKey(cert, flags, nullptr, &key, &spec, &mustFree))
        fail(__FILE__, __LINE__,
             "CryptAcquireCertificatePrivateKey failed with " + std::to_string(GetLastError()));
    CHECK_EQUAL(spec, DWORD{CERT_NCRYPT_KEY_SPEC});
    CHECK(!mustFree);
    CHECK(CertEnumCertificatesInStore(store, cert) == nullptr);
    CertCloseStore(store, 0);
}
