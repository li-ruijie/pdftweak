#pragma once

// PdfToImage: the export of PDF pages as image files, as burst output (-burstfiletype with -ob) and
// as one multipage TIFF (-oi). Pages render at 72 dpi (spec section 6). MuPDF writes JPG, PNG, PAM,
// and PNM, Windows Imaging Component writes GIF and BMP, and libtiff writes TIFF.

#include <optional>
#include <string>
#include <string_view>

namespace pdftweak::image {

// PdfToImage.ColorMode. The black and white modes keep 8-bit grey with the values 0 and 255 only,
// by a threshold (BNW) or by Floyd-Steinberg error diffusion (BNWI).
enum class ColorMode { RGB, GRAY, BNW, BNWI };

// PdfToImage.ImageType
enum class ImageType { PDF, JPG, PNG, GIF, PAM, PNM, BMP, TIFF };

// PdfToImage.TiffCompression
enum class TiffCompression { NONE, LZW, JPEG, ZLIB, PACKBITS, DEFLATE };

// The constant names, as Enum.valueOf accepts and Enum.toString writes them.
std::optional<ColorMode> parseColorMode(std::string_view name);
std::optional<ImageType> parseImageType(std::string_view name);
std::optional<TiffCompression> parseTiffCompression(std::string_view name);
const char* imageTypeName(ImageType type);
const char* tiffCompressionName(TiffCompression compression);
const char* colorModeName(ColorMode mode);

// ColorMode.toString: "RGB", "Grayscale", "Black and White", and "Black and White Indexed".
const char* colorModeText(ColorMode mode);

class PdfToImage {
public:
    PdfToImage(bool burstImages, ColorMode colorMode, ImageType imageType, TiffCompression compression,
               int quality, bool transparent)
        : colorMode_(colorMode), compression_(compression), quality_(quality), transparent_(transparent),
          imageType_(imageType), burstImages_(burstImages) {}

    // Whether burst output writes images rather than PDF files.
    bool shouldExecute() const { return burstImages_; }

    ColorMode colorMode() const { return colorMode_; }
    ImageType imageType() const { return imageType_; }
    TiffCompression compression() const { return compression_; }
    int quality() const { return quality_; }
    bool transparent() const { return transparent_; }

    // getImageFileName: base, the output name up to and including the page number, followed by
    // suffix with its extension replaced by the image type's, or given one if it has none.
    std::wstring imageFileName(const std::wstring& base, const std::wstring& suffix) const;

    // convertToImage: the first page of the PDF as an image file. Throws Error(Io, "Cannot save a
    // page as X in color mode \"Y\"") for a colour mode the type cannot hold, before writing.
    void convertToImage(const std::string& pdf, const std::wstring& outputFilePath) const;

    // convertToMultiTiff: every page of the PDF in one TIFF file, named with the extension tiff.
    void convertToMultiTiff(const std::string& pdf, const std::wstring& outputPath) const;

private:
    void checkTiffOptions() const;

    ColorMode colorMode_;
    TiffCompression compression_;
    int quality_;
    bool transparent_;
    ImageType imageType_;
    bool burstImages_;
};

} // namespace pdftweak::image
