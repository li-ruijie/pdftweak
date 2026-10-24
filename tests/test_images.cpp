// Ports of the image export tests of OperationTests.java and RegressionTests.java.

#include "runner.h"

#include "cli_run.h"
#include "fixtures.h"
#include "inspect.h"

#include "core/image/pdf_to_image.h"
#include "core/text.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <array>
#include <filesystem>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

using namespace pdftweak::test;

namespace {

const std::pair<float, float> kA4{595.0f, 842.0f};

unsigned readBig32(const std::string& bytes, size_t at) {
    return static_cast<unsigned>(static_cast<unsigned char>(bytes[at])) << 24 |
           static_cast<unsigned>(static_cast<unsigned char>(bytes[at + 1])) << 16 |
           static_cast<unsigned>(static_cast<unsigned char>(bytes[at + 2])) << 8 |
           static_cast<unsigned>(static_cast<unsigned char>(bytes[at + 3]));
}

unsigned readLittle(const std::string& bytes, size_t at, size_t size) {
    unsigned value = 0;
    for (size_t i = 0; i < size; ++i)
        value |= static_cast<unsigned>(static_cast<unsigned char>(bytes[at + i])) << (8 * i);
    return value;
}

// OperationTests.distinctStripValues: the number of distinct byte values in the strips of an
// uncompressed TIFF.
int distinctStripValues(const std::string& tiff, const std::map<int, std::vector<long long>>& tags) {
    CHECK_EQUAL(tags.at(259).at(0), 1LL);
    std::array<bool, 256> seen{};
    int distinct = 0;
    const std::vector<long long>& offsets = tags.at(273);
    const std::vector<long long>& counts = tags.at(279);
    for (size_t s = 0; s < offsets.size(); ++s) {
        for (long long i = offsets[s]; i < offsets[s] + counts[s]; ++i) {
            const auto v = static_cast<unsigned char>(tiff.at(static_cast<size_t>(i)));
            if (!seen[v]) {
                seen[v] = true;
                ++distinct;
            }
        }
    }
    return distinct;
}

// ImageIO.read(file).getRGB(x, y): the pixel as non-premultiplied ARGB, decoded by WIC.
unsigned argbAt(const std::filesystem::path& file, int x, int y) {
    using Microsoft::WRL::ComPtr;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ComPtr<IWICImagingFactory> factory;
    CHECK(SUCCEEDED(
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))));
    ComPtr<IWICBitmapDecoder> decoder;
    CHECK(SUCCEEDED(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ,
                                                       WICDecodeMetadataCacheOnDemand, &decoder)));
    ComPtr<IWICBitmapFrameDecode> frame;
    CHECK(SUCCEEDED(decoder->GetFrame(0, &frame)));
    ComPtr<IWICFormatConverter> converter;
    CHECK(SUCCEEDED(factory->CreateFormatConverter(&converter)));
    CHECK(
        SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                        nullptr, 0, WICBitmapPaletteTypeCustom)));
    const WICRect rect{x, y, 1, 1};
    BYTE bgra[4] = {};
    CHECK(SUCCEEDED(converter->CopyPixels(&rect, 4, 4, bgra)));
    return static_cast<unsigned>(bgra[3]) << 24 | static_cast<unsigned>(bgra[2]) << 16 |
           static_cast<unsigned>(bgra[1]) << 8 | bgra[0];
}

} // namespace

TEST(unit_imageBurstNamesOnlyChangeTheFileExtension) {
    using namespace pdftweak::image;
    const PdfToImage png(true, ColorMode::RGB, ImageType::PNG, TiffCompression::NONE, 100, false);
    CHECK_EQUAL(pdftweak::toUtf8(png.imageFileName(L"a.dir\\pg1", L"")),
                pdftweak::toUtf8(L"a.dir\\pg1.png"));
    CHECK_EQUAL(pdftweak::toUtf8(png.imageFileName(L"a.dir\\pg1", L".pdf")),
                pdftweak::toUtf8(L"a.dir\\pg1.png"));
    CHECK_EQUAL(pdftweak::toUtf8(png.imageFileName(L"pg1.2", L"")), pdftweak::toUtf8(L"pg1.2.png"));
    CHECK_EQUAL(pdftweak::toUtf8(png.imageFileName(L"pg1.2", L"_x")), pdftweak::toUtf8(L"pg1.2_x.png"));
}

TEST(main_imageExportFormatsAndColorModes) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4});
    // type, color mode, transparency
    const std::vector<std::array<std::string, 3>> runs = {
        {"JPG", "RGB", "NO"},   {"JPG", "GRAY", "NO"},  {"PNG", "RGB", "NO"},  {"PNG", "RGB", "YES"},
        {"PNG", "GRAY", "NO"},  {"PNG", "GRAY", "YES"}, {"GIF", "RGB", "NO"},  {"GIF", "BNW", "NO"},
        {"PAM", "RGB", "NO"},   {"PAM", "RGB", "YES"},  {"PAM", "GRAY", "NO"}, {"PAM", "GRAY", "YES"},
        {"PNM", "RGB", "NO"},   {"PNM", "GRAY", "NO"},  {"BMP", "BNW", "NO"},  {"TIFF", "RGB", "NO"},
        {"TIFF", "RGB", "YES"}, {"TIFF", "GRAY", "NO"}, {"TIFF", "BNW", "NO"}, {"TIFF", "BNWI", "NO"}};
    for (const auto& r : runs)
        runCli({in, "-burstfiletype", r[0], "-colormode", r[1], "-transparency", r[2], "-ob",
                std::filesystem::absolute(r[0] + "-" + r[1] + "-" + r[2] + "-*")});
    for (const auto& r : runs) {
        std::string lower = r[0];
        for (char& c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::string name = r[0] + "-" + r[1] + "-" + r[2] + "-1." + lower;
        const std::string image = readFile(name);
        const bool gray = r[1] == "GRAY";
        const bool alpha = r[2] == "YES";
        if (r[0] == "JPG") {
            CHECK(static_cast<unsigned char>(image[0]) == 0xff &&
                  static_cast<unsigned char>(image[1]) == 0xd8);
        } else if (r[0] == "PNG") {
            CHECK(image.compare(0, 4, "\x89PNG") == 0);
            CHECK_EQUAL(std::to_string(readBig32(image, 16)) + "x" + std::to_string(readBig32(image, 20)),
                        std::string("595x842"));
            const int colorType = gray ? (alpha ? 4 : 0) : (alpha ? 6 : 2);
            CHECK_EQUAL(static_cast<int>(image[25]), colorType);
        } else if (r[0] == "GIF") {
            CHECK(image.compare(0, 4, "GIF8") == 0);
            CHECK_EQUAL(std::to_string(readLittle(image, 6, 2)) + "x" +
                            std::to_string(readLittle(image, 8, 2)),
                        std::string("595x842"));
        } else if (r[0] == "PAM") {
            std::string header = image.substr(0, 200);
            header = header.substr(0, header.find("ENDHDR"));
            const std::string tuple = std::string(gray ? "GRAYSCALE" : "RGB") + (alpha ? "_ALPHA" : "");
            CHECK(header.compare(0, 2, "P7") == 0 && header.find("WIDTH 595") != std::string::npos &&
                  header.find("HEIGHT 842") != std::string::npos &&
                  header.find("TUPLTYPE " + tuple + "\n") != std::string::npos);
        } else if (r[0] == "PNM") {
            CHECK(image.compare(0, 2, gray ? "P5" : "P6") == 0);
            CHECK(image.substr(0, 20).find("595 842") != std::string::npos);
        } else if (r[0] == "BMP") {
            CHECK(image.compare(0, 2, "BM") == 0);
        } else {
            const auto tags = tiffTags(image);
            CHECK_EQUAL(std::to_string(tags.at(256).at(0)) + "x" + std::to_string(tags.at(257).at(0)),
                        std::string("595x842"));
            CHECK_EQUAL(tags.at(262).at(0), r[1] == "RGB" ? 2LL : 1LL);
            const long long samples = tags.count(277) ? tags.at(277).at(0) : 1LL;
            CHECK_EQUAL(samples, r[1] == "RGB" ? (alpha ? 4LL : 3LL) : 1LL);
            CHECK_EQUAL(tags.at(258).at(0), 8LL);
            if (r[1].compare(0, 3, "BNW") == 0)
                CHECK_EQUAL(distinctStripValues(image, tags), 2);
        }
    }
}

TEST(main_imageExportTiffCompressionAndJpegQuality) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4});
    const std::vector<std::string> compressions = {"NONE", "LZW", "JPEG", "ZLIB", "PACKBITS", "DEFLATE"};
    for (const std::string& c : compressions)
        runCli({in, "-burstfiletype", "TIFF", "-tiffcompression", c, "-ob",
                std::filesystem::absolute(c + "-*.tiff")});
    for (const std::string q : {"10", "90"})
        runCli({in, "-burstfiletype", "JPG", "-imagequality", q, "-ob",
                std::filesystem::absolute("q" + q + "-*.jpg")});
    const long long expected[] = {1, 5, 7, 8, 32773, 32946};
    for (size_t i = 0; i < compressions.size(); ++i)
        CHECK_EQUAL(tiffTags(readFile(compressions[i] + "-1.tiff")).at(259).at(0), expected[i]);
    CHECK(std::filesystem::file_size("q10-1.jpg") < std::filesystem::file_size("q90-1.jpg"));
    // JPEG in a TIFF stores RGB as YCbCr, as the JDK's writer does
    CHECK_EQUAL(tiffTags(readFile("JPEG-1.tiff")).at(262).at(0), 6LL);
    // the JDK writes no PlanarConfiguration, leaving the default of chunky samples
    CHECK_EQUAL(tiffTags(readFile("NONE-1.tiff")).count(284), size_t(0));
}

// A half transparent red square stays red at half opacity, in a PNG (written by MuPDF) and a TIFF.
TEST(main_imageExportKeepsTheColourOfTransparentPixels) {
    pdfOfSizes(".", "plain.pdf", {kA4});
    {
        QPDF pdf;
        pdf.processFile("plain.pdf");
        QPDFObjectHandle page = pdf.getAllPages().at(0);
        page.replaceKey("/Resources", QPDFObjectHandle::parse("<< /ExtGState << /GS1 << /ca 0.5 >> >> >>"));
        page.replaceKey("/Contents", pdf.newStream("/GS1 gs 1 0 0 rg 100 100 200 200 re f\n"));
        QPDFWriter writer(pdf, "in.pdf");
        writer.write();
    }
    for (const std::string type : {"PNG", "TIFF"})
        runCli({"in.pdf", "-burstfiletype", type, "-colormode", "RGB", "-transparency", "YES", "-ob",
                std::filesystem::absolute(type + "-*")});
    for (const std::string name : {"PNG-1.png", "TIFF-1.tiff"}) {
        // the middle of the square, with y counted from the top of the page
        const unsigned argb = argbAt(name, 200, 842 - 200);
        const unsigned alpha = argb >> 24, red = (argb >> 16) & 0xff, green = (argb >> 8) & 0xff,
                       blue = argb & 0xff;
        if (!(alpha >= 126 && alpha <= 129 && red >= 250 && green <= 3 && blue <= 3))
            fail(__FILE__, __LINE__,
                 name + " pixel is " + std::to_string(alpha) + "," + std::to_string(red) + "," +
                     std::to_string(green) + "," + std::to_string(blue));
    }
}

TEST(main_imageBurstNamesOnlyChangeTheFileExtension) {
    const auto in = fixture(".", "in.pdf", 2);
    runCli({in, "-burstfiletype", "PNG", "-ob", std::filesystem::absolute("a.dir\\pg*")});
    for (int page = 1; page <= 2; ++page)
        CHECK(std::filesystem::is_regular_file("a.dir\\pg" + std::to_string(page) + ".png"));
    CHECK(!std::filesystem::exists("a.png"));
}

TEST(main_tiffOutputReplacesAnExistingFile) {
    const auto in = fixture(".", "in.pdf", 2);
    for (int run = 1; run <= 2; ++run) {
        runCli({in, "-oi", std::filesystem::absolute("multi.tiff")});
        runCli({in, "-burstfiletype", "TIFF", "-ob", std::filesystem::absolute("t*.tiff")});
    }
    CHECK_EQUAL(tiffPageCount(readFile("multi.tiff")), 2);
    for (int page = 1; page <= 2; ++page)
        CHECK_EQUAL(tiffPageCount(readFile("t" + std::to_string(page) + ".tiff")), 1);
    // -oi gives the file the extension tiff
    runCli({in, "-oi", std::filesystem::absolute("named.tif")});
    CHECK(std::filesystem::is_regular_file("named.tiff"));
    // Java's check of the output name fails first on Windows, so its message about * in a
    // multipage TIFF name never shows there
    CHECK_EQUAL(cliError({in, "-oi", std::filesystem::absolute("star*.tiff")}), std::string("Bad pathname"));
}

// The JDK's TIFF writer cannot JPEG-compress an image with an alpha channel.
TEST(main_tiffJpegCompressionRefusesTransparency) {
    const auto in = fixture(".", "in.pdf", 2);
    const std::string message =
        "TIFF JPEG compression cannot keep transparency. Turn transparency off or choose "
        "another compression.";
    CHECK_EQUAL(cliError({in, "-burstfiletype", "TIFF", "-tiffcompression", "JPEG", "-transparency", "YES",
                          "-ob", std::filesystem::absolute("t*.tiff")}),
                message);
    CHECK_EQUAL(cliError({in, "-tiffcompression", "JPEG", "-transparency", "YES", "-oi",
                          std::filesystem::absolute("multi.tiff")}),
                message);
    for (const auto& entry : std::filesystem::directory_iterator("."))
        CHECK(entry.path().extension() != ".tiff");
    // Only RGB keeps an alpha channel, so a grayscale TIFF can still use JPEG.
    runCli({in, "-colormode", "GRAY", "-tiffcompression", "JPEG", "-transparency", "YES", "-oi",
            std::filesystem::absolute("gray.tiff")});
    CHECK_EQUAL(tiffPageCount(readFile("gray.tiff")), 2);
}

TEST(main_imageOptionsWithPdfBurstTypeWriteAPdf) {
    // RegressionTests.main_imageOptionsOnlyNeedTheLibraryForImageOutput, whose missing library has no
    // counterpart in a program that links MuPDF
    const auto in = fixture(".", "in.pdf", 1);
    runCli({in, "-colormode", "GRAY", "-tiffcompression", "LZW", "-transparency", "NO", "-imagequality",
            "80", "-burstfiletype", "PDF", "out.pdf"});
    CHECK_EQUAL(pageCount("out.pdf"), 1);
    CHECK_EQUAL(cliError({in, "-imagequality", "101", "bad.pdf"}),
                std::string("Please refer to \"-help -imagequality\""));
    CHECK_EQUAL(cliError({in, "-colormode", "CMYK", "bad.pdf"}),
                std::string("Please refer to \"-help -colormode\""));
}

TEST(main_imageExportWritesBmpInEveryColourMode) {
    const auto in = fixture(".", "in.pdf", 1);
    for (const std::string mode : {"RGB", "GRAY", "BNW", "BNWI"})
        runCli({in, "-burstfiletype", "BMP", "-colormode", mode, "-ob",
                std::filesystem::absolute(mode + "-*.bmp")});
    for (const std::string mode : {"RGB", "GRAY", "BNW", "BNWI"}) {
        const std::string bmp = readFile(mode + "-1.bmp");
        CHECK(bmp.compare(0, 2, "BM") == 0);
        const int height = static_cast<int>(readLittle(bmp, 22, 4));
        CHECK_EQUAL(std::to_string(readLittle(bmp, 18, 4)) + "x" +
                        std::to_string(height < 0 ? -height : height),
                    std::string("595x842"));
    }
}

TEST(main_imageExportReportsUnsupportedColourModes) {
    const auto in = fixture(".", "in.pdf", 1);
    const std::string error = cliError(
        {in, "-burstfiletype", "PNG", "-colormode", "BNW", "-ob", std::filesystem::absolute("p*.png")});
    CHECK(error.find("Black and White") != std::string::npos);
    CHECK(!std::filesystem::exists("p1.png"));
}
