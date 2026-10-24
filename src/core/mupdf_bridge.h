#pragma once

// Every MuPDF call of the core lives in mupdf_bridge.cpp. MuPDF reports errors
// with setjmp and longjmp, so each call sequence sits in a function whose fz_try block holds no
// object with a destructor, and the error becomes a pdftweak::Error after fz_catch.

#include <memory>
#include <string>
#include <vector>

namespace pdftweak::sign {
class SigningKey;
}

namespace pdftweak::mupdf {

// "Optimise size": writes the PDF again with MuPDF's garbage collection at level 3, which renumbers
// objects and merges duplicates, as PdfSmartCopy did in Java.
std::string optimize(const std::string& pdf);

// A rendered page: height rows of width * n samples, where n counts the alpha channel, with the
// colours premultiplied by alpha as MuPDF renders them.
struct Pixmap {
    int width = 0;
    int height = 0;
    int n = 0;
    std::vector<unsigned char> samples;
};

// The image formats that MuPDF writes itself.
enum class PixmapFormat { Jpeg, Png, Pam, Pnm };

// A PDF opened for rendering at 72 dpi with the identity matrix, annotations included, as
// MuPdfRenderer does with Page.toPixmap.
class Renderer {
public:
    explicit Renderer(const std::string& pdf);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    int pageCount() const;

    // Page index (0-based) in DeviceGray or DeviceRGB, with an alpha channel if alpha is set.
    Pixmap render(int index, bool gray, bool alpha) const;

    // Renders page index as render does, marks it as 72 dpi, and saves it in the format to the file
    // (UTF-8). The quality applies to JPEG.
    void save(int index, bool gray, bool alpha, PixmapFormat format, const std::string& utf8Path,
              int quality) const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

// Signing. Opens the finished PDF file, with the owner password when it is
// encrypted, adds the signature field SignatureN (the first free N) to page 1 at [100 100 200 200]
// when visible and with an empty rectangle otherwise, signs it with the key, certifies it at the
// level (1 to 3) when it is above 0, and appends the signature to the file as an incremental update.
void signFile(const std::string& utf8Path, const std::string& ownerPassword, const sign::SigningKey& key,
              bool visible, int certificationLevel);

} // namespace pdftweak::mupdf
