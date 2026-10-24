#include "core/image/pdf_to_image.h"

#include "core/error.h"
#include "core/image/encoders.h"
#include "core/mupdf_bridge.h"
#include "core/text.h"

#include <algorithm>
#include <array>
#include <filesystem>

namespace pdftweak::image {
namespace {

constexpr std::array<const char*, 4> kColorModes = {"RGB", "GRAY", "BNW", "BNWI"};
constexpr std::array<const char*, 4> kColorModeTexts = {"RGB", "Grayscale", "Black and White",
                                                        "Black and White Indexed"};
constexpr std::array<const char*, 8> kImageTypes = {"PDF", "JPG", "PNG", "GIF",
                                                    "PAM", "PNM", "BMP", "TIFF"};
constexpr std::array<const char*, 6> kCompressions = {"NONE", "LZW", "JPEG", "ZLIB", "PACKBITS", "DEFLATE"};

template <typename E, size_t N>
std::optional<E> parse(const std::array<const char*, N>& names, std::string_view name) {
    for (size_t i = 0; i < N; ++i)
        if (name == names[i])
            return static_cast<E>(i);
    return std::nullopt;
}

// PdfToImage.replaceExtension
std::wstring replaceExtension(const std::wstring& name, const std::wstring& extension) {
    const size_t slash = name.find_last_of(L"/\\");
    const size_t nameStart = slash == std::wstring::npos ? 0 : slash + 1;
    size_t dot = name.rfind(L'.');
    if (dot == std::wstring::npos || dot < nameStart)
        dot = name.size();
    return name.substr(0, dot) + L"." + extension;
}

void threshold(std::vector<unsigned char>& gray) {
    for (unsigned char& value : gray)
        value = value < 128 ? 0 : 255;
}

// MuPdfRenderer.dither: Floyd-Steinberg error diffusion to black and white.
void dither(std::vector<unsigned char>& gray, int width, int height) {
    std::vector<int> error(static_cast<size_t>(width + 2) * 2);
    for (int y = 0; y < height; ++y) {
        const int cur = (y % 2) * (width + 2);
        const int next = ((y + 1) % 2) * (width + 2);
        std::fill(error.begin() + next, error.begin() + next + width + 2, 0);
        for (int x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            const int value = gray[i] + error[static_cast<size_t>(cur + x + 1)] / 16;
            const int out = value < 128 ? 0 : 255;
            gray[i] = static_cast<unsigned char>(out);
            const int e = value - out;
            error[static_cast<size_t>(cur + x + 2)] += e * 7;
            error[static_cast<size_t>(next + x)] += e * 3;
            error[static_cast<size_t>(next + x + 1)] += e * 5;
            error[static_cast<size_t>(next + x + 2)] += e;
        }
    }
}

// MuPdfRenderer.toImage: RGB (with premultiplied alpha if asked for), 8-bit grey, or for the black
// and white modes 8-bit grey with black and white only.
Image toImage(const mupdf::Renderer& renderer, int index, ColorMode colorMode, bool alpha) {
    const bool rgb = colorMode == ColorMode::RGB;
    mupdf::Pixmap pixmap = renderer.render(index, !rgb, alpha && rgb);
    Image image{pixmap.width, pixmap.height, pixmap.n, std::move(pixmap.samples)};
    if (colorMode == ColorMode::BNW)
        threshold(image.samples);
    else if (colorMode == ColorMode::BNWI)
        dither(image.samples, image.width, image.height);
    return image;
}

} // namespace

std::optional<ColorMode> parseColorMode(std::string_view name) {
    return parse<ColorMode>(kColorModes, name);
}
std::optional<ImageType> parseImageType(std::string_view name) {
    return parse<ImageType>(kImageTypes, name);
}
std::optional<TiffCompression> parseTiffCompression(std::string_view name) {
    return parse<TiffCompression>(kCompressions, name);
}
const char* imageTypeName(ImageType type) { return kImageTypes[static_cast<size_t>(type)]; }
const char* tiffCompressionName(TiffCompression compression) {
    return kCompressions[static_cast<size_t>(compression)];
}
const char* colorModeName(ColorMode mode) { return kColorModes[static_cast<size_t>(mode)]; }
const char* colorModeText(ColorMode mode) { return kColorModeTexts[static_cast<size_t>(mode)]; }

std::wstring PdfToImage::imageFileName(const std::wstring& base, const std::wstring& suffix) const {
    std::string extension = imageTypeName(imageType_);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return base + replaceExtension(suffix, fromUtf8(extension));
}

void PdfToImage::checkTiffOptions() const {
    // The JDK's TIFF writer cannot JPEG-compress an image with an alpha channel, which only RGB
    // images have, so Java refused that before any file was written.
    if (compression_ == TiffCompression::JPEG && transparent_ && colorMode_ == ColorMode::RGB)
        throw Error(Error::Kind::Io,
                    "TIFF JPEG compression cannot keep transparency. Turn transparency off "
                    "or choose another compression.");
}

void PdfToImage::convertToImage(const std::string& pdf, const std::wstring& outputFilePath) const {
    if (imageType_ == ImageType::TIFF)
        checkTiffOptions();
    const bool binary = colorMode_ == ColorMode::BNW || colorMode_ == ColorMode::BNWI;
    bool saved = false;
    switch (imageType_) {
    case ImageType::JPG:
    case ImageType::PNG:
    case ImageType::PAM:
    case ImageType::PNM: {
        if (binary)
            break;
        // JPG and PNM have no alpha channel
        const bool alpha = transparent_ && (imageType_ == ImageType::PNG || imageType_ == ImageType::PAM);
        const mupdf::PixmapFormat format = imageType_ == ImageType::JPG   ? mupdf::PixmapFormat::Jpeg
                                           : imageType_ == ImageType::PNG ? mupdf::PixmapFormat::Png
                                           : imageType_ == ImageType::PAM ? mupdf::PixmapFormat::Pam
                                                                          : mupdf::PixmapFormat::Pnm;
        mupdf::Renderer(pdf).save(0, colorMode_ != ColorMode::RGB, alpha, format, toUtf8(outputFilePath),
                                  quality_);
        saved = true;
        break;
    }
    case ImageType::GIF:
    case ImageType::BMP: {
        const Image image = toImage(mupdf::Renderer(pdf), 0, colorMode_, false);
        if (imageType_ == ImageType::GIF)
            writeGif(outputFilePath, image);
        else
            writeBmp(outputFilePath, image);
        saved = true;
        break;
    }
    case ImageType::TIFF: {
        const mupdf::Renderer renderer(pdf);
        writeTiff(
            outputFilePath, 1, [&](int i) { return toImage(renderer, i, colorMode_, transparent_); },
            compression_, quality_);
        saved = true;
        break;
    }
    default: break;
    }
    if (!saved)
        throw Error(Error::Kind::Io, std::string("Cannot save a page as ") + imageTypeName(imageType_) +
                                         " in color mode \"" + colorModeText(colorMode_) + "\"");
}

void PdfToImage::convertToMultiTiff(const std::string& pdf, const std::wstring& outputPath) const {
    checkTiffOptions();
    const mupdf::Renderer renderer(pdf);
    writeTiff(
        replaceExtension(outputPath, L"tiff"), renderer.pageCount(),
        [&](int i) { return toImage(renderer, i, colorMode_, transparent_); }, compression_, quality_);
}

} // namespace pdftweak::image
