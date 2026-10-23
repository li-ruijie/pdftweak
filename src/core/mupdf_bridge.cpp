#include "core/mupdf_bridge.h"

#include "core/error.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include <algorithm>

namespace pdftweak::mupdf {
namespace {

// The MuPDF context of one call sequence, dropped whatever happens.
class Context {
public:
    Context() : ctx_(fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT)) {
        if (ctx_ == nullptr)
            throw Error(Error::Kind::Io, "MuPDF cannot create a context");
    }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    ~Context() { fz_drop_context(ctx_); }
    operator fz_context*() const { return ctx_; }

private:
    fz_context* ctx_;
};

// The bytes of a MuPDF buffer, which the caller drops.
std::string take(fz_context* ctx, fz_buffer* buffer) {
    unsigned char* data = nullptr;
    const size_t size = fz_buffer_storage(ctx, buffer, &data);
    std::string bytes(reinterpret_cast<const char*>(data), size);
    fz_drop_buffer(ctx, buffer);
    return bytes;
}

#pragma warning(push)
#pragma warning(disable : 4611) // interaction between _setjmp and C++ object destruction

// The functions below hold no object with a destructor. Each returns null on failure with MuPDF's
// message in error.

fz_buffer* optimizeC(fz_context* ctx, const char* data, size_t size, char* error, size_t errorSize) {
    fz_buffer* input = nullptr;
    fz_stream* stream = nullptr;
    pdf_document* doc = nullptr;
    fz_buffer* output = nullptr;
    fz_output* out = nullptr;
    fz_var(input);
    fz_var(stream);
    fz_var(doc);
    fz_var(output);
    fz_var(out);
    fz_try(ctx) {
        input = fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char*>(data), size);
        stream = fz_open_buffer(ctx, input);
        doc = pdf_open_document_with_stream(ctx, stream);
        output = fz_new_buffer(ctx, size);
        out = fz_new_output_with_buffer(ctx, output);
        pdf_write_options options = pdf_default_write_options;
        options.do_garbage = 3;
        pdf_write_document(ctx, doc, out, &options);
        fz_close_output(ctx, out);
    }
    fz_always(ctx) {
        fz_drop_output(ctx, out);
        pdf_drop_document(ctx, doc);
        fz_drop_stream(ctx, stream);
        fz_drop_buffer(ctx, input);
    }
    fz_catch(ctx) {
        strncpy_s(error, errorSize, fz_caught_message(ctx), _TRUNCATE);
        fz_drop_buffer(ctx, output);
        output = nullptr;
    }
    return output;
}

fz_document* openC(fz_context* ctx, const char* data, size_t size, fz_buffer** buffer, char* error,
                   size_t errorSize) {
    fz_document* doc = nullptr;
    fz_var(doc);
    fz_try(ctx) {
        fz_register_document_handlers(ctx);
        *buffer = fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char*>(data), size);
        doc = fz_open_document_with_buffer(ctx, "application/pdf", *buffer);
    }
    fz_catch(ctx) {
        strncpy_s(error, errorSize, fz_caught_message(ctx), _TRUNCATE);
        doc = nullptr;
    }
    return doc;
}

int countC(fz_context* ctx, fz_document* doc, char* error, size_t errorSize) {
    int count = -1;
    fz_try(ctx) { count = fz_count_pages(ctx, doc); }
    fz_catch(ctx) {
        strncpy_s(error, errorSize, fz_caught_message(ctx), _TRUNCATE);
        count = -1;
    }
    return count;
}

fz_pixmap* renderC(fz_context* ctx, fz_document* doc, int index, int gray, int alpha, char* error,
                   size_t errorSize) {
    fz_page* page = nullptr;
    fz_pixmap* pixmap = nullptr;
    fz_var(page);
    fz_var(pixmap);
    fz_try(ctx) {
        page = fz_load_page(ctx, doc, index);
        pixmap = fz_new_pixmap_from_page(ctx, page, fz_identity,
                                         gray ? fz_device_gray(ctx) : fz_device_rgb(ctx), alpha);
        fz_set_pixmap_resolution(ctx, pixmap, 72, 72);
    }
    fz_always(ctx) { fz_drop_page(ctx, page); }
    fz_catch(ctx) {
        strncpy_s(error, errorSize, fz_caught_message(ctx), _TRUNCATE);
        fz_drop_pixmap(ctx, pixmap);
        pixmap = nullptr;
    }
    return pixmap;
}

int saveC(fz_context* ctx, fz_pixmap* pixmap, int format, const char* path, int quality, char* error,
          size_t errorSize) {
    int ok = 1;
    fz_try(ctx) {
        switch (format) {
        case 0: fz_save_pixmap_as_jpeg(ctx, pixmap, path, quality); break;
        case 1: fz_save_pixmap_as_png(ctx, pixmap, path); break;
        case 2: fz_save_pixmap_as_pam(ctx, pixmap, path); break;
        default: fz_save_pixmap_as_pnm(ctx, pixmap, path); break;
        }
    }
    fz_catch(ctx) {
        strncpy_s(error, errorSize, fz_caught_message(ctx), _TRUNCATE);
        ok = 0;
    }
    return ok;
}

#pragma warning(pop)

} // namespace

std::string optimize(const std::string& pdf) {
    Context ctx;
    char error[256] = "";
    fz_buffer* buffer = optimizeC(ctx, pdf.data(), pdf.size(), error, sizeof error);
    if (buffer == nullptr)
        throw Error(Error::Kind::Document, error);
    return take(ctx, buffer);
}

struct Renderer::State {
    Context ctx;
    fz_buffer* buffer = nullptr;
    fz_document* doc = nullptr;

    ~State() {
        fz_drop_document(ctx, doc);
        fz_drop_buffer(ctx, buffer);
    }

    // a pixmap that the caller drops
    fz_pixmap* render(int index, bool gray, bool alpha) const {
        char error[256] = "";
        fz_pixmap* pixmap = renderC(ctx, doc, index, gray, alpha, error, sizeof error);
        if (pixmap == nullptr)
            throw Error(Error::Kind::Document, error);
        return pixmap;
    }
};

Renderer::Renderer(const std::string& pdf) : state_(std::make_unique<State>()) {
    char error[256] = "";
    state_->doc = openC(state_->ctx, pdf.data(), pdf.size(), &state_->buffer, error, sizeof error);
    if (state_->doc == nullptr)
        throw Error(Error::Kind::Document, error);
}

Renderer::~Renderer() = default;

int Renderer::pageCount() const {
    char error[256] = "";
    const int count = countC(state_->ctx, state_->doc, error, sizeof error);
    if (count < 0)
        throw Error(Error::Kind::Document, error);
    return count;
}

Pixmap Renderer::render(int index, bool gray, bool alpha) const {
    fz_pixmap* pixmap = state_->render(index, gray, alpha);
    Pixmap result;
    result.width = fz_pixmap_width(state_->ctx, pixmap);
    result.height = fz_pixmap_height(state_->ctx, pixmap);
    result.n = fz_pixmap_components(state_->ctx, pixmap);
    const ptrdiff_t stride = fz_pixmap_stride(state_->ctx, pixmap);
    const unsigned char* samples = fz_pixmap_samples(state_->ctx, pixmap);
    const size_t row = static_cast<size_t>(result.width) * static_cast<size_t>(result.n);
    result.samples.resize(row * static_cast<size_t>(result.height));
    for (int y = 0; y < result.height; ++y)
        std::copy_n(samples + y * stride, row, result.samples.begin() + static_cast<ptrdiff_t>(row) * y);
    fz_drop_pixmap(state_->ctx, pixmap);
    return result;
}

void Renderer::save(int index, bool gray, bool alpha, PixmapFormat format, const std::string& utf8Path,
                    int quality) const {
    fz_pixmap* pixmap = state_->render(index, gray, alpha);
    char error[256] = "";
    const int ok = saveC(state_->ctx, pixmap, static_cast<int>(format), utf8Path.c_str(), quality, error,
                         sizeof error);
    fz_drop_pixmap(state_->ctx, pixmap);
    if (!ok)
        throw Error(Error::Kind::Io, error);
}

} // namespace pdftweak::mupdf
