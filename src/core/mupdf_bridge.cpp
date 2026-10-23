#include "core/mupdf_bridge.h"

#include "core/error.h"
#include "core/sign/signing_key.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include <algorithm>
#include <cstring>

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

// The pdf_pkcs7_signer that MuPDF calls, over a SigningKey. The signFile frame owns it, so keep and
// drop only count.
struct KeySigner {
    pdf_pkcs7_signer base;
    int refs;
    const sign::SigningKey* key;
    size_t maxSize;
    const char* name[5]; // cn, o, ou, email, and c, or null when empty
    char error[256];
};

// Signs the bytes into digest without letting a C++ exception reach MuPDF. Returns the size, or 0
// with the reason in error.
size_t signBytes(KeySigner* signer, const unsigned char* data, size_t size, unsigned char* digest,
                 size_t room) noexcept {
    try {
        const std::string signature =
            signer->key->sign(std::string(reinterpret_cast<const char*>(data), size));
        if (signature.size() > room) {
            strncpy_s(signer->error, "The signature is larger than the room reserved for it", _TRUNCATE);
            return 0;
        }
        std::memcpy(digest, signature.data(), signature.size());
        return signature.size();
    } catch (const std::exception& e) {
        strncpy_s(signer->error, e.what(), _TRUNCATE);
        return 0;
    }
}

pdf_pkcs7_signer* keepSigner(fz_context*, pdf_pkcs7_signer* signer) {
    ++reinterpret_cast<KeySigner*>(signer)->refs;
    return signer;
}

void dropSigner(fz_context*, pdf_pkcs7_signer* signer) {
    if (signer != nullptr)
        --reinterpret_cast<KeySigner*>(signer)->refs;
}

char* copyName(fz_context* ctx, const char* text) {
    return text == nullptr ? nullptr : fz_strdup(ctx, text);
}

pdf_pkcs7_distinguished_name* signingName(fz_context* ctx, pdf_pkcs7_signer* signer) {
    KeySigner* s = reinterpret_cast<KeySigner*>(signer);
    pdf_pkcs7_distinguished_name* dn = fz_malloc_struct(ctx, pdf_pkcs7_distinguished_name);
    fz_try(ctx) {
        dn->cn = copyName(ctx, s->name[0]);
        dn->o = copyName(ctx, s->name[1]);
        dn->ou = copyName(ctx, s->name[2]);
        dn->email = copyName(ctx, s->name[3]);
        dn->c = copyName(ctx, s->name[4]);
    }
    fz_catch(ctx) {
        pdf_signature_drop_distinguished_name(ctx, dn);
        fz_rethrow(ctx);
    }
    return dn;
}

size_t maxDigestSize(fz_context*, pdf_pkcs7_signer* signer) {
    return reinterpret_cast<KeySigner*>(signer)->maxSize;
}

int createDigest(fz_context* ctx, pdf_pkcs7_signer* signer, fz_stream* in, unsigned char* digest,
                 size_t room) {
    KeySigner* s = reinterpret_cast<KeySigner*>(signer);
    fz_buffer* buffer = fz_read_all(ctx, in, 0);
    unsigned char* data = nullptr;
    const size_t size = fz_buffer_storage(ctx, buffer, &data);
    const size_t written = signBytes(s, data, size, digest, room);
    fz_drop_buffer(ctx, buffer);
    if (written == 0)
        fz_throw(ctx, FZ_ERROR_ARGUMENT, "%s", s->error);
    return static_cast<int>(written);
}

// PdfSignatureAppearance.addDocMDP: the certification reference of the signature dictionary, and
// /Perms in the catalog.
void certify(fz_context* ctx, pdf_document* doc, pdf_obj* v, int level) {
    pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
    pdf_obj* references = pdf_dict_put_array(ctx, v, PDF_NAME(Reference), 1);
    pdf_obj* reference = pdf_array_push_dict(ctx, references, 8);
    pdf_obj* params = pdf_dict_put_dict(ctx, reference, PDF_NAME(TransformParams), 3);
    pdf_dict_put_int(ctx, params, PDF_NAME(P), level);
    pdf_dict_put_name(ctx, params, PDF_NAME(V), "1.2");
    pdf_dict_put(ctx, params, PDF_NAME(Type), PDF_NAME(TransformParams));
    pdf_dict_put(ctx, reference, PDF_NAME(TransformMethod), PDF_NAME(DocMDP));
    pdf_dict_put(ctx, reference, PDF_NAME(Type), PDF_NAME(SigRef));
    if (pdf_version(ctx, doc) < 16) {
        pdf_dict_puts_drop(ctx, reference, "DigestValue", pdf_new_string(ctx, "aa", 2));
        pdf_obj* location = pdf_new_array(ctx, doc, 2);
        pdf_dict_puts_drop(ctx, reference, "DigestLocation", location);
        pdf_array_push_int(ctx, location, 0);
        pdf_array_push_int(ctx, location, 0);
        pdf_dict_puts_drop(ctx, reference, "DigestMethod", pdf_new_name(ctx, "MD5"));
    }
    pdf_dict_put(ctx, reference, PDF_NAME(Data), root);
    pdf_obj* perms = pdf_dict_put_dict(ctx, root, PDF_NAME(Perms), 1);
    pdf_dict_put(ctx, perms, PDF_NAME(DocMDP), v);
}

int signC(fz_context* ctx, const char* path, const char* password, pdf_pkcs7_signer* signer, int visible,
          int level, char* error, size_t errorSize) {
    pdf_document* doc = nullptr;
    pdf_page* page = nullptr;
    pdf_annot* widget = nullptr;
    int ok = 1;
    fz_var(doc);
    fz_var(page);
    fz_var(widget);
    fz_try(ctx) {
        doc = pdf_open_document(ctx, path);
        if (pdf_needs_password(ctx, doc) && !pdf_authenticate_password(ctx, doc, password))
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "Bad owner password");
        // PdfSignatureAppearance.getNewSigName
        pdf_obj* fields = pdf_dict_getp(ctx, pdf_trailer(ctx, doc), "Root/AcroForm/Fields");
        char name[32];
        for (int step = 1;; ++step) {
            fz_snprintf(name, sizeof name, "Signature%d", step);
            if (fields == nullptr || pdf_lookup_field(ctx, fields, name) == nullptr)
                break;
        }
        page = pdf_load_page(ctx, doc, 0);
        widget = pdf_create_signature_widget(ctx, page, name);
        pdf_obj* field = pdf_annot_obj(ctx, widget);
        // iText's field is printed and locked, without the lock dictionary that would make MuPDF mark
        // every other field read-only
        pdf_dict_del(ctx, field, PDF_NAME(Lock));
        pdf_dict_put_int(ctx, field, PDF_NAME(F), 132);
        pdf_dict_put_rect(ctx, field, PDF_NAME(Rect),
                          visible ? fz_make_rect(100, 100, 200, 200) : fz_make_rect(0, 0, 0, 0));
        pdf_sign_signature(ctx, widget, signer, PDF_SIGNATURE_DEFAULT_APPEARANCE, nullptr, nullptr,
                           nullptr);
        // MuPDF would put any change after the signing into a new incremental section, which the
        // signature's byte range does not cover, so the signature dictionary is completed in the
        // section of the signature
        doc->disallow_new_increments = 1;
        pdf_obj* v = pdf_dict_get(ctx, field, PDF_NAME(V));
        pdf_dict_put_text_string(ctx, v, PDF_NAME(Reason), "");
        pdf_dict_put_text_string(ctx, v, PDF_NAME(Location), "");
        pdf_dict_put_text_string(ctx, v, PDF_NAME(ContactInfo), "");
        if (level > 0)
            certify(ctx, doc, v, level);
        else
            pdf_dict_del(ctx, v, PDF_NAME(Reference));
        doc->disallow_new_increments = 0;
        pdf_write_options options = pdf_default_write_options;
        options.do_incremental = 1;
        pdf_save_document(ctx, doc, path, &options);
    }
    fz_always(ctx) {
        pdf_drop_annot(ctx, widget);
        fz_drop_page(ctx, reinterpret_cast<fz_page*>(page));
        pdf_drop_document(ctx, doc);
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

void signFile(const std::string& utf8Path, const std::string& ownerPassword, const sign::SigningKey& key,
              bool visible, int certificationLevel) {
    const sign::DistinguishedName dn = key.subject();
    KeySigner signer{};
    signer.base.keep = keepSigner;
    signer.base.drop = dropSigner;
    signer.base.get_signing_name = signingName;
    signer.base.max_digest_size = maxDigestSize;
    signer.base.create_digest = createDigest;
    signer.refs = 1;
    signer.key = &key;
    signer.maxSize = key.maxSignatureSize();
    const std::string* parts[] = {&dn.cn, &dn.o, &dn.ou, &dn.email, &dn.c};
    for (size_t i = 0; i < 5; ++i)
        signer.name[i] = parts[i]->empty() ? nullptr : parts[i]->c_str();
    Context ctx;
    char error[256] = "";
    if (!signC(ctx, utf8Path.c_str(), ownerPassword.c_str(), &signer.base, visible, certificationLevel,
               error, sizeof error))
        throw Error(Error::Kind::Io, error);
}

} // namespace pdftweak::mupdf
