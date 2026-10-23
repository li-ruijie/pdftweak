#include "inspect.h"

#include "fixtures.h"
#include "runner.h"

#include "core/java_text.h"
#include "core/text.h"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <qpdf/Pl_String.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <numbers>
#include <regex>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

namespace pdftweak::test {
namespace {

QPDFPageObjectHelper pageOf(QPDF& pdf, int page) {
    const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(pdf).getAllPages();
    if (page < 1 || page > static_cast<int>(pages.size()))
        throw Failure("No page " + std::to_string(page));
    return pages[static_cast<size_t>(page - 1)];
}

// fz_try uses setjmp, so this function holds no object with a destructor. It returns the structured
// text of one page and the inverse of the matrix from PDF user space to MuPDF's page space, or null
// with MuPDF's message in error.
#pragma warning(push)
#pragma warning(disable : 4611) // interaction between _setjmp and C++ object destruction
fz_stext_page* loadText(fz_context* ctx, const char* path, int pageNumber, int flags, fz_matrix* inverse,
                        char* error, size_t errorSize) {
    pdf_document* doc = nullptr;
    pdf_page* page = nullptr;
    fz_stext_page* stext = nullptr;
    fz_var(doc);
    fz_var(page);
    fz_var(stext);
    fz_try(ctx) {
        doc = pdf_open_document(ctx, path);
        page = pdf_load_page(ctx, doc, pageNumber - 1);
        fz_rect box;
        fz_matrix ctm;
        pdf_page_transform(ctx, page, &box, &ctm);
        *inverse = fz_invert_matrix(ctm);
        fz_stext_options options;
        std::memset(&options, 0, sizeof options);
        options.flags = flags;
        stext = fz_new_stext_page_from_page(ctx, &page->super, &options);
    }
    fz_always(ctx) {
        pdf_drop_page(ctx, page);
        pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        strncpy_s(error, errorSize, fz_caught_message(ctx), _TRUNCATE);
        stext = nullptr;
    }
    return stext;
}
#pragma warning(pop)

void collectLines(const fz_stext_block* block, std::vector<const fz_stext_line*>& lines) {
    for (; block != nullptr; block = block->next) {
        if (block->type == FZ_STEXT_BLOCK_TEXT) {
            for (const fz_stext_line* line = block->u.t.first_line; line != nullptr; line = line->next)
                lines.push_back(line);
        } else if (block->type == FZ_STEXT_BLOCK_STRUCT && block->u.s.down != nullptr) {
            collectLines(block->u.s.down->first_block, lines);
        }
    }
}

struct Line {
    std::string text;
    fz_point start{};
    fz_point end{};
};

// The lines of text on a page, with the start and end of each baseline in PDF user space.
std::vector<Line> textLines(const std::filesystem::path& pdf, int page, int flags) {
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (ctx == nullptr)
        throw Failure("fz_new_context failed");
    fz_matrix inverse{};
    char error[256] = "";
    const std::string path = toUtf8(pdf.wstring());
    fz_stext_page* stext = loadText(ctx, path.c_str(), page, flags, &inverse, error, sizeof error);
    if (stext == nullptr) {
        fz_drop_context(ctx);
        throw Failure("MuPDF cannot read page " + std::to_string(page) + " of " + pdf.string() + ": " +
                      error);
    }
    std::vector<const fz_stext_line*> found;
    collectLines(stext->first_block, found);
    std::vector<Line> lines;
    for (const fz_stext_line* l : found) {
        if (l->first_char == nullptr)
            continue;
        Line line;
        std::u32string points;
        for (const fz_stext_char* c = l->first_char; c != nullptr; c = c->next)
            points.push_back(static_cast<char32_t>(c->c));
        line.text = fromCodePoints(points);
        const fz_stext_char* last = l->last_char;
        const fz_point end{last->origin.x + last->quad.lr.x - last->quad.ll.x,
                           last->origin.y + last->quad.lr.y - last->quad.ll.y};
        line.start = fz_transform_point(l->first_char->origin, inverse);
        line.end = fz_transform_point(end, inverse);
        lines.push_back(line);
    }
    fz_drop_stext_page(ctx, stext);
    fz_drop_context(ctx);
    return lines;
}

// The DER length of the CMS structure at the start of the bytes, which /Contents pads with zeros.
size_t derLength(const std::string& der) {
    if (der.size() < 2 || static_cast<unsigned char>(der[0]) != 0x30)
        throw Failure("The signature is not a DER SEQUENCE");
    const unsigned char first = static_cast<unsigned char>(der[1]);
    if (first < 0x80)
        return 2 + size_t{first};
    const size_t count = first & 0x7f;
    size_t length = 0;
    for (size_t i = 0; i < count; ++i)
        length = (length << 8) | static_cast<unsigned char>(der.at(2 + i));
    return 2 + count + length;
}

std::string fromHex(const std::string& hex) {
    std::string bytes;
    int high = -1;
    for (const char c : hex) {
        int value = -1;
        if (c >= '0' && c <= '9')
            value = c - '0';
        else if (c >= 'a' && c <= 'f')
            value = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            value = c - 'A' + 10;
        if (value < 0)
            continue;
        if (high < 0) {
            high = value;
        } else {
            bytes += static_cast<char>(high * 16 + value);
            high = -1;
        }
    }
    return bytes;
}

} // namespace

std::shared_ptr<QPDF> openPdf(const std::filesystem::path& pdf, const std::string& password) {
    auto qpdf = std::make_shared<QPDF>();
    try {
        qpdf->processFile(pdf.string().c_str(), password.empty() ? nullptr : password.c_str());
    } catch (const std::exception& e) {
        throw Failure(std::string("Cannot open ") + pdf.string() + ": " + e.what());
    }
    return qpdf;
}

int pageCount(const std::filesystem::path& pdf, const std::string& password) {
    const auto qpdf = openPdf(pdf, password);
    return static_cast<int>(QPDFPageDocumentHelper(*qpdf).getAllPages().size());
}

std::array<float, 4> mediaBox(const std::filesystem::path& pdf, int page) {
    const auto qpdf = openPdf(pdf);
    const QPDFObjectHandle::Rectangle r = pageOf(*qpdf, page).getMediaBox().getArrayAsRectangle();
    return {static_cast<float>(std::min(r.llx, r.urx)), static_cast<float>(std::min(r.lly, r.ury)),
            static_cast<float>(std::max(r.llx, r.urx)), static_cast<float>(std::max(r.lly, r.ury))};
}

int rotation(const std::filesystem::path& pdf, int page) {
    const auto qpdf = openPdf(pdf);
    QPDFObjectHandle rotate = pageOf(*qpdf, page).getAttribute("/Rotate", false);
    if (!rotate.isNumber())
        return 0;
    int n = static_cast<int>(rotate.getNumericValue()) % 360;
    return n < 0 ? n + 360 : n;
}

std::string content(const std::filesystem::path& pdf, int page) {
    const auto qpdf = openPdf(pdf);
    std::string bytes;
    Pl_String out("content", nullptr, bytes);
    pageOf(*qpdf, page).pipeContents(&out);
    return bytes;
}

std::vector<std::array<float, 6>> placements(const std::filesystem::path& pdf, int page) {
    static const std::string n = "(-?[0-9.]+)";
    static const std::regex cm(n + " " + n + " " + n + " " + n + " " + n + " " + n + " cm");
    const std::string text = content(pdf, page);
    std::vector<std::array<float, 6>> result;
    for (auto m = std::sregex_iterator(text.begin(), text.end(), cm); m != std::sregex_iterator(); ++m) {
        std::array<float, 6> matrix{};
        for (size_t i = 0; i < 6; ++i) {
            const std::string number = (*m)[i + 1].str();
            std::from_chars(number.data(), number.data() + number.size(), matrix[i]);
        }
        result.push_back(matrix);
    }
    return result;
}

std::vector<TextRun> textPositions(const std::filesystem::path& pdf, int page) {
    std::vector<TextRun> runs;
    for (const Line& line : textLines(pdf, page, FZ_STEXT_INHIBIT_SPACES)) {
        const float dx = line.end.x - line.start.x;
        const float dy = line.end.y - line.start.y;
        const float angle = static_cast<float>(std::atan2(dy, dx) * 180 / std::numbers::pi);
        // as in the Java tests, the watermark is checked by its centre, page numbers by where they start
        runs.push_back({line.text, (line.start.x + line.end.x) / 2,
                        angle == 0 ? line.start.y : (line.start.y + line.end.y) / 2, angle});
    }
    return runs;
}

std::string text(const std::filesystem::path& pdf, int page) {
    std::string result;
    for (const Line& line : textLines(pdf, page, 0)) {
        if (!result.empty())
            result += '\n';
        result += line.text;
    }
    return result;
}

std::string pageTexts(const std::filesystem::path& pdf) {
    std::string list = "[";
    for (int page = 1; page <= pageCount(pdf); ++page)
        list += (page > 1 ? ", " : "") + javaTrim(text(pdf, page));
    return list + "]";
}

std::string pageNumbers(const std::filesystem::path& pdf) {
    std::string list = "[";
    for (int page = 1; page <= pageCount(pdf); ++page) {
        const std::string t = javaTrim(text(pdf, page));
        list +=
            (page > 1 ? ", " : "") + (t.empty() ? std::string("0") : t.substr(std::string("Page ").size()));
    }
    return list + "]";
}

int pageEntry(const std::filesystem::path& pdf, int page, const std::string& key) {
    const auto qpdf = openPdf(pdf);
    QPDFObjectHandle value = pageOf(*qpdf, page).getObjectHandle().getKey("/" + key);
    return value.isInteger() ? value.getIntValueAsInt() : -1;
}

int objectCount(const std::filesystem::path& pdf) {
    const auto qpdf = openPdf(pdf);
    return static_cast<int>(qpdf->getAllObjects().size());
}

std::map<int, std::vector<long long>> tiffTags(const std::string& tiff) {
    const bool little = !tiff.empty() && tiff[0] == 'I';
    auto read = [&](size_t at, size_t size) {
        if (at + size > tiff.size())
            throw Failure("TIFF too short");
        unsigned long long value = 0;
        for (size_t i = 0; i < size; ++i) {
            const unsigned long long byte =
                static_cast<unsigned char>(tiff[at + (little ? i : size - 1 - i)]);
            value |= byte << (8 * i);
        }
        return value;
    };
    if (read(2, 2) != 42)
        throw Failure("TIFF signature: expected <42> but was <" + std::to_string(read(2, 2)) + ">");
    const size_t ifd = read(4, 4);
    std::map<int, std::vector<long long>> tags;
    for (size_t i = 0, n = read(ifd, 2); i < n; ++i) {
        const size_t entry = ifd + 2 + 12 * i;
        const int tag = static_cast<int>(read(entry, 2));
        const unsigned long long type = read(entry + 2, 2);
        const size_t count = read(entry + 4, 4);
        if (type != 3 && type != 4) {
            tags[tag] = {};
            continue;
        }
        const size_t size = type == 3 ? 2 : 4;
        const size_t at = count * size <= 4 ? entry + 8 : read(entry + 8, 4);
        std::vector<long long> values;
        for (size_t v = 0; v < count; ++v)
            values.push_back(static_cast<long long>(read(at + size * v, size)));
        tags[tag] = values;
    }
    return tags;
}

int tiffPageCount(const std::string& tiff) {
    const bool little = !tiff.empty() && tiff[0] == 'I';
    auto read = [&](size_t at, size_t size) {
        if (at + size > tiff.size())
            throw Failure("TIFF too short");
        size_t value = 0;
        for (size_t i = 0; i < size; ++i)
            value |= size_t{static_cast<unsigned char>(tiff[at + (little ? i : size - 1 - i)])} << (8 * i);
        return value;
    };
    int count = 0;
    for (size_t ifd = read(4, 4); ifd != 0; ifd = read(ifd + 2 + 12 * read(ifd, 2), 4)) {
        if (++count > 10000)
            throw Failure("IFD chain does not end");
    }
    return count;
}

std::string verifySignature(const std::filesystem::path& pdf, const std::string& password) {
    const std::string file = readFile(pdf);
    const auto qpdf = openPdf(pdf, password);
    std::vector<QPDFObjectHandle> signatures;
    for (QPDFFormFieldObjectHelper& field : QPDFAcroFormDocumentHelper(*qpdf).getFormFields()) {
        QPDFObjectHandle value = field.getValue();
        if (field.getFieldType() == "/Sig" && value.isDictionary())
            signatures.push_back(value);
    }
    if (signatures.size() != 1)
        throw Failure("signatures in " + pdf.filename().string() + ": expected <1> but was <" +
                      std::to_string(signatures.size()) + ">");
    QPDFObjectHandle sig = signatures[0];
    long long range[4];
    for (int i = 0; i < 4; ++i)
        range[i] = sig.getKey("/ByteRange").getArrayItem(i).getIntValue();
    if (range[0] != 0 || range[2] + range[3] != static_cast<long long>(file.size()))
        throw Failure("byte ranges do not cover the file");
    const std::string data = file.substr(0, static_cast<size_t>(range[1])) +
                             file.substr(static_cast<size_t>(range[2]), static_cast<size_t>(range[3]));
    // /Contents is read from the gap between the ranges, which encryption never touches.
    std::string cms =
        fromHex(file.substr(static_cast<size_t>(range[1]), static_cast<size_t>(range[2] - range[1])));
    cms.resize(derLength(cms));
    const std::string subFilter = sig.getKey("/SubFilter").getName();

    CRYPT_VERIFY_MESSAGE_PARA para{};
    para.cbSize = sizeof para;
    para.dwMsgAndCertEncodingType = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
    const BYTE* blob = reinterpret_cast<const BYTE*>(cms.data());
    const DWORD blobSize = static_cast<DWORD>(cms.size());
    if (subFilter == "/adbe.pkcs7.sha1") {
        // The signed content is the SHA-1 digest of the byte ranges.
        DWORD contentSize = 0;
        if (!CryptVerifyMessageSignature(&para, 0, blob, blobSize, nullptr, &contentSize, nullptr))
            throw Failure(pdf.filename().string() + " signature does not verify");
        std::string signedDigest(contentSize, '\0');
        if (!CryptVerifyMessageSignature(&para, 0, blob, blobSize,
                                         reinterpret_cast<BYTE*>(signedDigest.data()), &contentSize,
                                         nullptr))
            throw Failure(pdf.filename().string() + " signature does not verify");
        signedDigest.resize(contentSize);
        std::string digest(20, '\0');
        if (BCryptHash(BCRYPT_SHA1_ALG_HANDLE, nullptr, 0,
                       reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                       static_cast<ULONG>(data.size()), reinterpret_cast<PUCHAR>(digest.data()), 20) != 0)
            throw Failure("BCryptHash failed");
        if (digest != signedDigest)
            throw Failure(pdf.filename().string() + " signed digest does not match the byte ranges");
    } else {
        const BYTE* parts[] = {reinterpret_cast<const BYTE*>(data.data())};
        DWORD sizes[] = {static_cast<DWORD>(data.size())};
        if (!CryptVerifyDetachedMessageSignature(&para, 0, blob, blobSize, 1, parts, sizes, nullptr))
            throw Failure(pdf.filename().string() + " signature does not verify over the byte ranges (" +
                          subFilter + "), error " + std::to_string(GetLastError()));
    }
    return subFilter;
}

} // namespace pdftweak::test
