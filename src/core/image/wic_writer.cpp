// GIF and BMP files with the Windows Imaging Component, in place of Java's ImageIO writers.

#include "core/image/encoders.h"

#include "core/error.h"
#include "core/text.h"

#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace pdftweak::image {
namespace {

using Microsoft::WRL::ComPtr;

// COM for the current thread, released when it was started here.
class ComScope {
public:
    ComScope() : initialised_(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}
    ~ComScope() {
        if (initialised_)
            CoUninitialize();
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    bool initialised_;
};

void check(HRESULT hr, const std::wstring& path) {
    if (FAILED(hr)) {
        char code[16];
        snprintf(code, sizeof code, "0x%08lX", static_cast<unsigned long>(hr));
        throw Error(Error::Kind::Io, "Cannot write " + toUtf8(path) + " (" + code + ")");
    }
}

// Writes the image as 8-bit indexed (gif, or a grey image) or 24-bit BGR with the container's
// encoder. A grey image takes the 256 greys as its palette, and a colour image for a GIF a palette
// of up to 256 colours computed from it, without dithering.
void write(const std::wstring& path, const Image& image, bool gif) {
    ComScope com;
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
          path);
    const bool gray = image.channels == 1;
    const UINT stride = static_cast<UINT>(image.width * image.channels);
    ComPtr<IWICBitmap> bitmap;
    check(factory->CreateBitmapFromMemory(static_cast<UINT>(image.width), static_cast<UINT>(image.height),
                                          gray ? GUID_WICPixelFormat8bppGray : GUID_WICPixelFormat24bppRGB,
                                          stride, static_cast<UINT>(image.samples.size()),
                                          const_cast<BYTE*>(image.samples.data()), &bitmap),
          path);
    ComPtr<IWICPalette> palette;
    WICPixelFormatGUID target = GUID_WICPixelFormat24bppBGR;
    if (gray || gif) {
        target = GUID_WICPixelFormat8bppIndexed;
        check(factory->CreatePalette(&palette), path);
        if (gray)
            check(palette->InitializePredefined(WICBitmapPaletteTypeFixedGray256, FALSE), path);
        else
            check(palette->InitializeFromBitmap(bitmap.Get(), 256, FALSE), path);
    }
    ComPtr<IWICFormatConverter> converter;
    check(factory->CreateFormatConverter(&converter), path);
    check(converter->Initialize(bitmap.Get(), target, WICBitmapDitherTypeNone, palette.Get(), 0,
                                WICBitmapPaletteTypeCustom),
          path);
    ComPtr<IWICStream> stream;
    check(factory->CreateStream(&stream), path);
    check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), path);
    ComPtr<IWICBitmapEncoder> encoder;
    check(
        factory->CreateEncoder(gif ? GUID_ContainerFormatGif : GUID_ContainerFormatBmp, nullptr, &encoder),
        path);
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), path);
    ComPtr<IWICBitmapFrameEncode> frame;
    check(encoder->CreateNewFrame(&frame, nullptr), path);
    check(frame->Initialize(nullptr), path);
    check(frame->SetSize(static_cast<UINT>(image.width), static_cast<UINT>(image.height)), path);
    WICPixelFormatGUID format = target;
    check(frame->SetPixelFormat(&format), path);
    if (palette)
        check(frame->SetPalette(palette.Get()), path);
    check(frame->WriteSource(converter.Get(), nullptr), path);
    check(frame->Commit(), path);
    check(encoder->Commit(), path);
}

} // namespace

void writeGif(const std::wstring& path, const Image& image) { write(path, image, true); }

void writeBmp(const std::wstring& path, const Image& image) { write(path, image, false); }

} // namespace pdftweak::image
