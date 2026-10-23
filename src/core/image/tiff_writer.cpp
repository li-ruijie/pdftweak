// The TIFF files of MuPdfRenderer.writeTiff with libtiff, tagged as the JDK's TIFFImageWriter tags
// them (spec section 6 and its question 2).
//
// - Grey images are BlackIsZero and RGB images are RGB, with associated alpha as an extra sample.
//   JPEG compression writes RGB as YCbCr with 2 by 2 chroma subsampling.
// - The compression codes are NONE 1, LZW 5, JPEG 7, ZLIB 8, PACKBITS 32773, and DEFLATE 32946. The
//   quality q sets the JPEG quality q, and the deflate level (int) (1 + 8 * q / 100) for ZLIB and
//   DEFLATE. No predictor is used.
// - Rows per strip are max(8192 / bytes per row, 1, 8), rounded for JPEG to a multiple of 8 rows
//   for grey and 16 for RGB when the strip is shorter than the image.
// - The resolution is 1 by 1 without a unit.

#include "core/image/encoders.h"

#include "core/error.h"
#include "core/text.h"

#include <tiffio.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace pdftweak::image {
namespace {

int recordError(TIFF*, void* userData, const char* module, const char* fmt, va_list ap) {
    char message[512];
    vsnprintf(message, sizeof message, fmt, ap);
    std::string& error = *static_cast<std::string*>(userData);
    if (error.empty())
        error = std::string(module ? module : "TIFF") + ": " + message;
    return 1;
}

int ignoreWarning(TIFF*, void*, const char*, const char*, va_list) { return 1; }

uint16_t compressionCode(TiffCompression compression) {
    switch (compression) {
    case TiffCompression::LZW: return COMPRESSION_LZW;
    case TiffCompression::JPEG: return COMPRESSION_JPEG;
    case TiffCompression::ZLIB: return COMPRESSION_ADOBE_DEFLATE;
    case TiffCompression::PACKBITS: return COMPRESSION_PACKBITS;
    case TiffCompression::DEFLATE: return COMPRESSION_DEFLATE;
    default: return COMPRESSION_NONE;
    }
}

} // namespace

void writeTiff(const std::wstring& path, int count, const std::function<Image(int)>& page,
               TiffCompression compression, int quality) {
    std::error_code ignored;
    if (std::filesystem::exists(path, ignored) && !std::filesystem::remove(path, ignored))
        throw Error(Error::Kind::Io, "Cannot replace " + toUtf8(path));
    std::string error;
    TIFFOpenOptions* options = TIFFOpenOptionsAlloc();
    TIFFOpenOptionsSetErrorHandlerExtR(options, recordError, &error);
    TIFFOpenOptionsSetWarningHandlerExtR(options, ignoreWarning, nullptr);
    std::unique_ptr<TIFF, void (*)(TIFF*)> tif(TIFFOpenWExt(path.c_str(), "w", options), TIFFClose);
    TIFFOpenOptionsFree(options);
    const auto fail = [&]() {
        throw Error(Error::Kind::Io,
                    "Cannot write " + toUtf8(path) + (error.empty() ? std::string() : " (" + error + ")"));
    };
    if (!tif)
        fail();
    const uint16_t code = compressionCode(compression);
    for (int i = 0; i < count; ++i) {
        Image image = page(i);
        TIFF* t = tif.get();
        const bool rgb = image.channels >= 3;
        TIFFSetField(t, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(image.width));
        TIFFSetField(t, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(image.height));
        TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, 8);
        TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, image.channels);
        TIFFSetField(t, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        TIFFSetField(t, TIFFTAG_COMPRESSION, code);
        if (image.channels == 4) {
            const uint16_t extra[] = {EXTRASAMPLE_ASSOCALPHA};
            TIFFSetField(t, TIFFTAG_EXTRASAMPLES, 1, extra);
        }
        if (code == COMPRESSION_JPEG && rgb) {
            TIFFSetField(t, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_YCBCR);
            TIFFSetField(t, TIFFTAG_YCBCRSUBSAMPLING, 2, 2);
            TIFFSetField(t, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB);
        } else {
            TIFFSetField(t, TIFFTAG_PHOTOMETRIC, rgb ? PHOTOMETRIC_RGB : PHOTOMETRIC_MINISBLACK);
        }
        if (code == COMPRESSION_JPEG)
            TIFFSetField(t, TIFFTAG_JPEGQUALITY, quality);
        if (code == COMPRESSION_ADOBE_DEFLATE || code == COMPRESSION_DEFLATE)
            TIFFSetField(t, TIFFTAG_ZIPQUALITY,
                         static_cast<int>(1 + 8 * (static_cast<float>(quality) / 100.0f)));
        TIFFSetField(t, TIFFTAG_RESOLUTIONUNIT, RESUNIT_NONE);
        TIFFSetField(t, TIFFTAG_XRESOLUTION, 1.0);
        TIFFSetField(t, TIFFTAG_YRESOLUTION, 1.0);
        const int bytesPerRow = image.width * image.channels;
        int rowsPerStrip = std::max(std::max(8192 / std::max(bytesPerRow, 1), 1), 8);
        rowsPerStrip = std::min(rowsPerStrip, image.height);
        if (code == COMPRESSION_JPEG && rowsPerStrip < image.height) {
            const int multiple = rgb ? 16 : 8;
            rowsPerStrip = std::max(multiple * ((rowsPerStrip + multiple / 2) / multiple), multiple);
        }
        TIFFSetField(t, TIFFTAG_ROWSPERSTRIP, static_cast<uint32_t>(rowsPerStrip));
        for (int y = 0; y < image.height; ++y) {
            unsigned char* row =
                image.samples.data() + static_cast<size_t>(y) * static_cast<size_t>(bytesPerRow);
            if (TIFFWriteScanline(t, row, static_cast<uint32_t>(y), 0) < 0)
                fail();
        }
        if (!TIFFWriteDirectory(t))
            fail();
    }
    TIFF* t = tif.release();
    TIFFClose(t);
    if (!error.empty())
        fail();
}

} // namespace pdftweak::image
