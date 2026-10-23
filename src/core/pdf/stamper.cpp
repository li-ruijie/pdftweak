#include "core/pdf/stamper.h"

#include "core/error.h"
#include "core/java_text.h"
#include "core/pdf/encodings.h"
#include "core/pdf/helvetica.h"

namespace pdftweak::pdf {

struct Stamper::PageStamp {
    QPDFObjectHandle page;
    std::unique_ptr<Content> under;
    std::unique_ptr<Content> over;
    // PageResources: the names the original resources use, the name given to each added resource,
    // and the added resources by category in the order they were added
    std::set<std::string> forbidden;
    std::map<QPDFObjGen, std::string> used;
    std::map<std::string, std::vector<std::pair<std::string, QPDFObjectHandle>>> added;
};

namespace {

std::string number(float value) { return itextNumber(value); }

// StringUtils.escapeString
std::string escapeString(const std::string& bytes) {
    std::string out = "(";
    for (const char c : bytes) {
        switch (c) {
        case '\r': out += "\\r"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '(':
        case ')':
        case '\\':
            out += '\\';
            out += c;
            break;
        default: out += c;
        }
    }
    return out + ")";
}

// PdfStamperImp.applyRotation, from the page size with rotation of the MediaBox.
std::string rotation(QPDFObjectHandle page) {
    const Rect size = pageSizeWithRotation(page);
    switch (pageRotation(page)) {
    case 90: return "0 1 -1 0 " + number(size.ury) + " 0 cm\n";
    case 180: return "-1 0 0 -1 " + number(size.urx) + " " + number(size.ury) + " cm\n";
    case 270: return "0 -1 1 0 0 " + number(size.urx) + " cm\n";
    default: return {};
    }
}

} // namespace

// ---- Content ----

std::string Stamper::Content::resourceName(const std::string& category, QPDFObjectHandle resource) {
    // PageResources.translateName
    const auto found = page_.used.find(resource.getObjGen());
    if (found != page_.used.end())
        return found->second;
    std::string name;
    do
        name = "/Xi" + std::to_string(stamper_.namePtr_++);
    while (page_.forbidden.count(name) != 0);
    page_.used.emplace(resource.getObjGen(), name);
    page_.added[category].emplace_back(name, resource);
    return name;
}

void Stamper::Content::setGState(QPDFObjectHandle gstate) {
    buffer_ += resourceName("/ExtGState", gstate) + " gs\n";
}

void Stamper::Content::setRgbFill(int red, int green, int blue) {
    const auto component = [](int value) { return number(static_cast<float>(value & 0xFF) / 0xFF); };
    buffer_ += component(red) + " " + component(green) + " " + component(blue) + " rg\n";
}

void Stamper::Content::setFontAndSize(QPDFObjectHandle font, float size) {
    if (size < 0.0001f && size > -0.0001f)
        throw Error(Error::Kind::Document, "Font size too small: " + javaFloat(size));
    size_ = size;
    buffer_ += resourceName("/Font", font) + " " + number(size) + " Tf\n";
}

void Stamper::Content::setTextMatrix(float a, float b, float c, float d, float x, float y) {
    buffer_ += number(a) + " " + number(b) + " " + number(c) + " " + number(d) + " " + number(x) + " " +
               number(y) + " Tm\n";
}

void Stamper::Content::showText(std::string_view utf8) {
    buffer_ += escapeString(winAnsiBytes(utf8)) + "Tj\n";
}

void Stamper::Content::showTextCentered(std::string_view utf8, float x, float y) {
    x -= helvetica::widthPoint(utf8, size_) / 2;
    setTextMatrix(1, 0, 0, 1, x, y);
    showText(utf8);
}

void Stamper::Content::addTemplate(QPDFObjectHandle form, float x, float y) {
    const std::string name = resourceName("/XObject", form);
    buffer_ += "q 1 0 0 1 " + number(x) + " " + number(y) + " cm " + name + " Do Q\n";
}

// ---- Stamper ----

Stamper::Stamper(QPDF& pdf) : pdf_(pdf) {
    // PdfReader gives each page the attributes it inherits
    pdf_.pushInheritedAttributesToPage();
    pages_ = pages(pdf_);
}

Stamper::~Stamper() = default;

Stamper::PageStamp& Stamper::pageStamp(int n) {
    const auto found = stamps_.find(n);
    if (found != stamps_.end())
        return *found->second;
    auto stamp = std::make_unique<PageStamp>();
    stamp->page = pages_.at(static_cast<size_t>(n - 1)).getObjectHandle();
    // PageResources.setOriginalResources: every name in a dictionary of the resources is taken
    QPDFObjectHandle resources = stamp->page.getKey("/Resources");
    if (resources.isDictionary())
        for (const std::string& key : resources.getKeys())
            if (QPDFObjectHandle sub = resources.getKey(key); sub.isDictionary())
                for (const std::string& name : sub.getKeys())
                    stamp->forbidden.insert(name);
    return *stamps_.emplace(n, std::move(stamp)).first->second;
}

Stamper::Content& Stamper::under(int n) {
    PageStamp& stamp = pageStamp(n);
    if (!stamp.under)
        stamp.under.reset(new Content(*this, stamp));
    return *stamp.under;
}

Stamper::Content& Stamper::over(int n) {
    PageStamp& stamp = pageStamp(n);
    if (!stamp.over)
        stamp.over.reset(new Content(*this, stamp));
    return *stamp.over;
}

void Stamper::finish() {
    for (auto& [n, stamp] : stamps_) {
        QPDFObjectHandle page = stamp->page;
        QPDFObjectHandle contents = page.getKey("/Contents");
        QPDFObjectHandle array = QPDFObjectHandle::newArray();
        if (contents.isArray())
            array = QPDFObjectHandle::newArray(contents.getArrayAsVector());
        else if (contents.isStream())
            array.appendItem(contents);

        const std::string turn = rotation(page);
        std::string out;
        if (stamp->under)
            out += "q\n" + turn + stamp->under->buffer_ + "Q\n";
        if (stamp->over)
            out += "q\n";
        array.insertItem(0, pdf_.newStream(out));
        if (stamp->over)
            array.appendItem(pdf_.newStream(" Q\nq\n" + turn + stamp->over->buffer_ + "Q\n"));
        page.replaceKey("/Contents", array);

        // PageResources.getResources: copies of the original resources with the added ones
        QPDFObjectHandle original = page.getKey("/Resources");
        QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
        if (original.isDictionary()) {
            for (const std::string& key : original.getKeys()) {
                QPDFObjectHandle value = original.getKey(key);
                if (value.isDictionary()) {
                    QPDFObjectHandle copy = QPDFObjectHandle::newDictionary();
                    for (const std::string& name : value.getKeys())
                        copy.replaceKey(name, value.getKey(name));
                    value = copy;
                }
                resources.replaceKey(key, value);
            }
        }
        for (const auto& [category, entries] : stamp->added) {
            QPDFObjectHandle sub = resources.getKey(category);
            if (!sub.isDictionary()) {
                sub = QPDFObjectHandle::newDictionary();
                resources.replaceKey(category, sub);
            }
            for (const auto& [name, resource] : entries)
                sub.replaceKey(name, resource);
        }
        page.replaceKey("/Resources", resources);
    }
    stamps_.clear();
}

} // namespace pdftweak::pdf
