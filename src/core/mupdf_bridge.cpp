#include "core/mupdf_bridge.h"

#include "core/error.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

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

} // namespace pdftweak::mupdf
