#include "core/pdf/rebuild.h"

#include "core/java_text.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDFNameTreeObjectHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>

#include <algorithm>
#include <optional>

namespace pdftweak::pdf {
namespace {

std::string bytes(const std::shared_ptr<Buffer>& buffer) {
    return std::string(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
}

QPDFObjectHandle rectangleObject(const Rect& r) {
    return QPDFObjectHandle::newArray({itextNumberObject(r.llx), itextNumberObject(r.lly),
                                       itextNumberObject(r.urx), itextNumberObject(r.ury)});
}

std::array<float, 2> transform(float x, float y, const Matrix& m) {
    return {m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]};
}

bool isType(QPDFObjectHandle object, const char* type) {
    return object.isDictionary() && object.getKey("/Type").isNameAndEquals(type);
}

bool missing(QPDFObjectHandle object) { return !object.isInitialized() || object.isNull(); }

std::optional<float> number(QPDFObjectHandle array, int index) {
    if (index < array.getArrayNItems() && array.getArrayItem(index).isNumber())
        return static_cast<float>(array.getArrayItem(index).getNumericValue());
    return std::nullopt;
}

// p * u + q * v + k, or null if a term with a coefficient that is not 0 is null.
QPDFObjectHandle coordinate(float p, std::optional<float> u, float q, std::optional<float> v, float k) {
    if ((p != 0 && !u) || (q != 0 && !v))
        return QPDFObjectHandle::newNull();
    return itextNumberObject((p == 0 ? 0.0f : p * *u) + (q == 0 ? 0.0f : q * *v) + k);
}

QPDFObjectHandle copyScalar(QPDFObjectHandle object) { return QPDFObjectHandle::parse(object.unparse()); }

} // namespace

// ---- PageBuilder ----

PageBuilder::PageBuilder(QPDF& source) : source_(source), sourcePages_(pages(source)) {
    out_ = std::make_shared<QPDF>();
    out_->emptyPDF();
    out_->setSuppressWarnings(true);
}

QPDFObjectHandle PageBuilder::pageReference(int n) {
    auto found = reserved_.find(n);
    if (found == reserved_.end())
        found = reserved_.emplace(n, out_->newIndirectNull()).first;
    return found->second;
}

void PageBuilder::newPage(const Rect& mediaBox) {
    endPage();
    ++pageNumber_;
    QPDFObjectHandle page = QPDFObjectHandle::parse("<< /Type /Page /Resources << /XObject << >> >> >>");
    page.replaceKey("/MediaBox", rectangleObject(mediaBox));
    const QPDFObjectHandle reference = pageReference(pageNumber_);
    out_->replaceObject(reference.getObjGen(), page);
    page_ = out_->getObject(reference.getObjGen());
    QPDFPageDocumentHelper(*out_).addPage(QPDFPageObjectHelper(page_), false);
}

QPDFObjectHandle PageBuilder::importedPage(int sourcePage) {
    const auto found = templates_.find(sourcePage);
    if (found != templates_.end())
        return found->second.second;
    QPDFObjectHandle page = sourcePages_.at(static_cast<size_t>(sourcePage - 1)).getObjectHandle();
    QPDFObjectHandle contents = page.getKey("/Contents");
    QPDFObjectHandle form;
    if (contents.isStream()) {
        // the content stream as it is, with its filters (PdfReaderInstance.getFormXObject)
        form = out_->newStream();
        QPDFObjectHandle dict = contents.getDict();
        for (const std::string& key : dict.getKeys())
            if (key != "/Length" && key != "/Filter" && key != "/DecodeParms")
                form.getDict().replaceKey(key, copyFrom(*out_, dict.getKey(key)));
        form.replaceStreamData(bytes(contents.getRawStreamData()), copyFrom(*out_, dict.getKey("/Filter")),
                               copyFrom(*out_, dict.getKey("/DecodeParms")));
    } else {
        // PdfReader.getPageContent: the streams joined by newlines
        std::string data;
        if (contents.isArray()) {
            for (int i = 0; i < contents.getArrayNItems(); ++i) {
                QPDFObjectHandle part = contents.getArrayItem(i);
                if (part.isStream())
                    data += bytes(part.getStreamData(qpdf_dl_generalized));
                if (i != contents.getArrayNItems() - 1)
                    data += '\n';
            }
        }
        form = out_->newStream(data);
    }
    QPDFObjectHandle resources = QPDFPageObjectHelper(page).getAttribute("/Resources", false);
    QPDFObjectHandle dict = form.getDict();
    if (!resources.isNull())
        dict.replaceKey("/Resources", copyFrom(*out_, resources));
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Form"));
    const Rect box = pageSize(page);
    dict.replaceKey("/BBox", rectangleObject(box));
    dict.replaceKey("/Matrix",
                    QPDFObjectHandle::newArray({itextNumberObject(1), itextNumberObject(0),
                                                itextNumberObject(0), itextNumberObject(1),
                                                itextNumberObject(-box.llx), itextNumberObject(-box.lly)}));
    dict.replaceKey("/FormType", QPDFObjectHandle::newInteger(1));
    const std::string name = "/Xf" + std::to_string(templates_.size() + 1);
    templates_.emplace(sourcePage, std::make_pair(name, form));
    return form;
}

void PageBuilder::addTemplate(int sourcePage, const Matrix& m) {
    QPDFObjectHandle form = importedPage(sourcePage);
    const std::string& name = templates_.at(sourcePage).first;
    page_.getKey("/Resources").getKey("/XObject").replaceKey(name, form);
    content_ += "q ";
    for (const float value : m)
        content_ += itextNumber(value) + " ";
    content_ += "cm " + name + " Do Q\n";
}

void PageBuilder::addAnnotation(QPDFObjectHandle annotation) {
    annotation = out_->makeIndirectObject(annotation);
    QPDFObjectHandle annots = page_.getKey("/Annots");
    if (!annots.isArray()) {
        annots = QPDFObjectHandle::newArray();
        page_.replaceKey("/Annots", annots);
    }
    annots.appendItem(annotation);
}

void PageBuilder::endPage() {
    if (page_.isInitialized())
        page_.replaceKey("/Contents", out_->newStream(content_));
    content_.clear();
}

std::shared_ptr<QPDF> PageBuilder::finish() {
    endPage();
    page_ = QPDFObjectHandle();
    return out_;
}

// ---- PageRemap ----

PageRemap::PageRemap(QPDF& source, PageBuilder& builder)
    : source_(source), builder_(builder), out_(builder.document()) {
    int number = 1;
    for (QPDFPageObjectHelper& page : pages(source))
        inputPages_[page.getObjectHandle().getObjGen()] = number++;
    outputPages_.assign(static_cast<size_t>(number), 0);
    matrices_.assign(static_cast<size_t>(number), Matrix{});
}

void PageRemap::place(int inputPage, int outputPage, const Matrix& m) {
    if (outputPages_[static_cast<size_t>(inputPage)] == 0) {
        outputPages_[static_cast<size_t>(inputPage)] = outputPage;
        matrices_[static_cast<size_t>(inputPage)] = m;
    }
}

void PageRemap::copyLinks(int inputPage, const Matrix& m) {
    QPDFObjectHandle annots =
        pages(source_).at(static_cast<size_t>(inputPage - 1)).getObjectHandle().getKey("/Annots");
    if (!annots.isArray())
        return;
    for (QPDFObjectHandle link : annots.aitems()) {
        if (!link.isDictionary() || !link.getKey("/Subtype").isNameAndEquals("/Link"))
            continue;
        QPDFObjectHandle copy = copyDirect(link);
        if (missing(copy))
            continue;
        const auto lost = [&](const char* key) { return link.hasKey(key) && !copy.hasKey(key); };
        if (lost("/Dest") || lost("/A"))
            continue; // it points at a page that is not shown
        const Rect r = rectangle(link.getKey("/Rect"));
        const auto p = transform(r.llx, r.lly, m);
        const auto q = transform(r.urx, r.ury, m);
        copy.removeKey("/Rect");
        copy.removeKey("/P");            // the input page
        copy.removeKey("/StructParent"); // the structure tree is not carried over
        QPDFObjectHandle annotation = QPDFObjectHandle::newDictionary();
        annotation.replaceKey("/Rect", rectangleObject({std::min(p[0], q[0]), std::min(p[1], q[1]),
                                                        std::max(p[0], q[0]), std::max(p[1], q[1])}));
        for (const std::string& key : copy.getKeys())
            annotation.replaceKey(key, copy.getKey(key));
        builder_.addAnnotation(annotation);
    }
}

void PageRemap::copyCatalog(bool keepPageLabels) {
    QPDFObjectHandle catalog = source_.getRoot();
    QPDFObjectHandle root = out_.getRoot();
    const auto put = [&](const std::string& key, QPDFObjectHandle value) {
        if (missing(value))
            return;
        if ((value.isDictionary() || value.isArray()) && !value.isIndirect())
            value = out_.makeIndirectObject(value);
        root.replaceKey(key, value);
    };
    for (const char* key : {"/Outlines", "/Dests", "/ViewerPreferences", "/PageMode", "/PageLayout",
                            "/OpenAction", "/Lang", "/OCProperties"})
        put(key, copy(catalog.getKey(key)));
    if (keepPageLabels)
        put("/PageLabels", copy(catalog.getKey("/PageLabels")));
    QPDFObjectHandle names = catalog.getKey("/Names");
    if (!names.isDictionary())
        return;
    QPDFObjectHandle newNames = QPDFObjectHandle::newDictionary();
    QPDFObjectHandle dests = names.getKey("/Dests");
    if (dests.isDictionary()) {
        std::vector<std::pair<std::string, QPDFObjectHandle>> entries;
        try {
            for (const auto& [key, value] : QPDFNameTreeObjectHelper(dests, source_, false)) {
                QPDFObjectHandle dest = copy(value);
                if (!missing(dest))
                    entries.emplace_back(key, dest);
            }
        } catch (const std::exception&) {
            // a broken tree gives the entries read so far
        }
        if (!entries.empty()) {
            QPDFNameTreeObjectHelper tree = QPDFNameTreeObjectHelper::newEmpty(out_);
            for (const auto& [key, value] : entries)
                tree.insert(key, value);
            newNames.replaceKey("/Dests", tree.getObjectHandle());
        }
    }
    QPDFObjectHandle files = copy(names.getKey("/EmbeddedFiles"));
    if (!missing(files))
        newNames.replaceKey("/EmbeddedFiles", files);
    if (!newNames.getKeys().empty())
        put("/Names", newNames);
}

QPDFObjectHandle PageRemap::copy(QPDFObjectHandle object) {
    if (!object.isInitialized())
        return QPDFObjectHandle();
    if (!object.isIndirect())
        return copyDirect(object);
    const QPDFObjGen og = object.getObjGen();
    if (const auto page = inputPages_.find(og); page != inputPages_.end()) {
        const int output = outputPages_[static_cast<size_t>(page->second)];
        return output == 0 ? QPDFObjectHandle() : builder_.pageReference(output);
    }
    if (const auto copied = copies_.find(og); copied != copies_.end())
        return copied->second;
    if (object.isNull())
        return QPDFObjectHandle();
    // shared with the imported pages, which copyForeignObject maps once per document
    if (object.isStream() || isType(object, "/OCG") || isType(object, "/OCMD"))
        return out_.copyForeignObject(object);
    if (isType(object, "/Pages") || isType(object, "/Page"))
        return QPDFObjectHandle(); // the page tree, or a page outside of it
    if (pointsNowhere(object))
        return QPDFObjectHandle();
    QPDFObjectHandle copied = out_.newIndirectNull();
    copies_[og] = copied;
    QPDFObjectHandle value = copyDirect(object);
    out_.replaceObject(copied.getObjGen(), missing(value) ? QPDFObjectHandle::newNull() : value);
    return copied;
}

QPDFObjectHandle PageRemap::copyDirect(QPDFObjectHandle object) {
    if (object.isDictionary()) {
        if (pointsNowhere(object))
            return QPDFObjectHandle();
        QPDFObjectHandle result = QPDFObjectHandle::newDictionary();
        for (const std::string& key : object.getKeys()) {
            if (key == "/SE")
                continue; // the structure tree is not carried over
            QPDFObjectHandle value = copy(object.getKey(key));
            if (!missing(value))
                result.replaceKey(key, value);
        }
        return result;
    }
    if (object.isArray()) {
        if (inputPage(object) != 0)
            return destination(object);
        QPDFObjectHandle result = QPDFObjectHandle::newArray();
        for (int i = 0; i < object.getArrayNItems(); ++i) {
            QPDFObjectHandle value = copy(object.getArrayItem(i));
            result.appendItem(missing(value) ? QPDFObjectHandle::newNull() : value);
        }
        return result;
    }
    return copyScalar(object);
}

bool PageRemap::pointsNowhere(QPDFObjectHandle object) const {
    if (object.isDictionary())
        object = object.getKey("/D");
    if (!object.isArray())
        return false;
    const int page = inputPage(object);
    return page != 0 && outputPages_[static_cast<size_t>(page)] == 0;
}

int PageRemap::inputPage(QPDFObjectHandle array) const {
    if (array.getArrayNItems() < 2 || !array.getArrayItem(0).isIndirect() ||
        !array.getArrayItem(1).isName())
        return 0;
    const auto found = inputPages_.find(array.getArrayItem(0).getObjGen());
    return found == inputPages_.end() ? 0 : found->second;
}

QPDFObjectHandle PageRemap::destination(QPDFObjectHandle dest) {
    const int page = inputPage(dest);
    const int output = outputPages_[static_cast<size_t>(page)];
    if (output == 0)
        return QPDFObjectHandle();
    const Matrix& m = matrices_[static_cast<size_t>(page)];
    const bool quarterTurn = m[0] == 0 && m[3] == 0;
    const std::string type = dest.getArrayItem(1).getName();
    QPDFObjectHandle result = QPDFObjectHandle::newArray({builder_.pageReference(output)});
    const auto name = [](const char* n) { return QPDFObjectHandle::newName(n); };
    if (type == "/XYZ") {
        const auto x = number(dest, 2), y = number(dest, 3);
        result.appendItem(name("/XYZ"));
        result.appendItem(coordinate(m[0], x, m[2], y, m[4]));
        result.appendItem(coordinate(m[1], x, m[3], y, m[5]));
        for (int i = 4; i < dest.getArrayNItems(); ++i)
            result.appendItem(copyScalar(dest.getArrayItem(i))); // the zoom
    } else if (type == "/FitH" || type == "/FitBH") {
        const auto top = number(dest, 2);
        if (quarterTurn) {
            result.appendItem(name(type == "/FitH" ? "/FitV" : "/FitBV"));
            result.appendItem(coordinate(0, std::nullopt, m[2], top, m[4]));
        } else {
            result.appendItem(name(type.c_str()));
            result.appendItem(coordinate(0, std::nullopt, m[3], top, m[5]));
        }
    } else if (type == "/FitV" || type == "/FitBV") {
        const auto left = number(dest, 2);
        if (quarterTurn) {
            result.appendItem(name(type == "/FitV" ? "/FitH" : "/FitBH"));
            result.appendItem(coordinate(m[1], left, 0, std::nullopt, m[5]));
        } else {
            result.appendItem(name(type.c_str()));
            result.appendItem(coordinate(m[0], left, 0, std::nullopt, m[4]));
        }
    } else if (type == "/FitR" && dest.getArrayNItems() >= 6) {
        const auto p = transform(number(dest, 2).value_or(0), number(dest, 3).value_or(0), m);
        const auto q = transform(number(dest, 4).value_or(0), number(dest, 5).value_or(0), m);
        result.appendItem(name("/FitR"));
        result.appendItem(itextNumberObject(std::min(p[0], q[0])));
        result.appendItem(itextNumberObject(std::min(p[1], q[1])));
        result.appendItem(itextNumberObject(std::max(p[0], q[0])));
        result.appendItem(itextNumberObject(std::max(p[1], q[1])));
    } else {
        for (int i = 1; i < dest.getArrayNItems(); ++i)
            result.appendItem(copyScalar(dest.getArrayItem(i))); // /Fit and /FitB keep only their page
    }
    return result;
}

} // namespace pdftweak::pdf
