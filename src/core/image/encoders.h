#pragma once

// The image encoders of PdfToImage that MuPDF does not provide, over an image as MuPdfRenderer's
// toImage builds it.

#include "core/image/pdf_to_image.h"

#include <functional>
#include <string>
#include <vector>

namespace pdftweak::image {

// 8-bit samples, channels per pixel: 1 for grey, 3 for RGB, or 4 for RGB with alpha, the colours
// premultiplied by alpha.
struct Image {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<unsigned char> samples;
};

// MuPdfRenderer.writeTiff: count pages, each produced by page(i) only while it is written, in one
// TIFF file, which replaces an existing one, with the tags of the JDK's TIFF writer.
void writeTiff(const std::wstring& path, int count, const std::function<Image(int)>& page,
               TiffCompression compression, int quality);

// ImageIO.write for GIF (a palette of up to 256 colours taken from the image, without dithering,
// or the 256 greys for a grey image) and BMP (24-bit, or 8-bit with the 256 greys).
void writeGif(const std::wstring& path, const Image& image);
void writeBmp(const std::wstring& path, const Image& image);

} // namespace pdftweak::image
