#include "describe.h"

#include "inspect.h"

#include "core/formats/page_labels.h"
#include "core/pdf/input_file.h"
#include "core/pdf/outline.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFNameTreeObjectHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>
#include <vector>

namespace pdftweak::test {
namespace {

namespace fs = std::filesystem;

std::string number(double value) {
    char text[32];
    snprintf(text, sizeof text, "%.2f", std::round(value * 100) / 100);
    std::string s = text;
    while (s.find('.') != std::string::npos && (s.back() == '0' || s.back() == '.')) {
        const bool dot = s.back() == '.';
        s.pop_back();
        if (dot)
            break;
    }
    return s == "-0" ? "0" : s;
}

std::string values(QPDFObjectHandle object);

std::string values(QPDFObjectHandle object) {
    if (object.isNumber())
        return number(object.getNumericValue());
    if (object.isArray()) {
        std::string s = "[";
        for (int i = 0; i < object.getArrayNItems(); ++i)
            s += (i ? " " : "") + values(object.getArrayItem(i));
        return s + "]";
    }
    if (object.isString())
        return "(" + object.getUTF8Value() + ")";
    if (object.isNull() || !object.isInitialized())
        return "null";
    if (object.isDictionary() && !object.isStream()) {
        std::string s = "<<";
        for (const std::string& key : object.getKeys())
            s += " " + key + " " +
                 (object.getKey(key).isIndirect() ? std::string("R") : values(object.getKey(key)));
        return s + " >>";
    }
    return object.unparse();
}

int pageIndexOf(QPDF& pdf, QPDFObjectHandle page) {
    int n = 1;
    for (QPDFObjectHandle p : pdf.getAllPages()) {
        if (p.getObjGen() == page.getObjGen())
            return n;
        ++n;
    }
    return 0;
}

std::string destination(QPDF& pdf, QPDFObjectHandle dest) {
    if (dest.isString() || dest.isName())
        return "named " + dest.unparse();
    if (!dest.isArray() || dest.getArrayNItems() == 0)
        return values(dest);
    std::string s = dest.getArrayItem(0).isDictionary()
                        ? std::to_string(pageIndexOf(pdf, dest.getArrayItem(0)))
                        : values(dest.getArrayItem(0));
    for (int i = 1; i < dest.getArrayNItems(); ++i)
        s += " " + values(dest.getArrayItem(i));
    return s;
}

} // namespace

std::string describe(const fs::path& file, const DescribeOptions& options) {
    std::ostringstream out;
    const auto qpdf = openPdf(file, options.password);
    QPDF& pdf = *qpdf;
    const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
    out << "pages " << pages.size() << "\n";
    for (size_t i = 0; i < pages.size(); ++i) {
        QPDFPageObjectHelper page(pages[i]);
        out << "page " << i + 1 << " media " << values(page.getAttribute("/MediaBox", false));
        if (QPDFObjectHandle crop = page.getAttribute("/CropBox", false); !crop.isNull())
            out << " crop " << values(crop);
        const QPDFObjectHandle rotate = page.getAttribute("/Rotate", false);
        out << " rotate " << (rotate.isInteger() ? ((rotate.getIntValue() % 360) + 360) % 360 : 0) << "\n";
        if (options.text && options.password.empty()) {
            std::string text = pdftweak::test::text(file, static_cast<int>(i + 1));
            std::replace(text.begin(), text.end(), '\n', '|');
            out << "  text " << text << "\n";
        }
        QPDFObjectHandle annots = pages[i].getKey("/Annots");
        for (int a = 0; annots.isArray() && a < annots.getArrayNItems(); ++a) {
            QPDFObjectHandle annot = annots.getArrayItem(a);
            out << "  annot " << annot.getKey("/Subtype").unparse() << " rect "
                << values(annot.getKey("/Rect"));
            if (annot.hasKey("/FT"))
                out << " field " << annot.getKey("/FT").unparse() << " " << values(annot.getKey("/T"));
            if (annot.hasKey("/Dest"))
                out << " dest " << destination(pdf, annot.getKey("/Dest"));
            QPDFObjectHandle action = annot.getKey("/A");
            if (action.isDictionary()) {
                out << " action " << action.getKey("/S").unparse();
                if (action.hasKey("/URI"))
                    out << " " << action.getKey("/URI").getUTF8Value();
                if (action.hasKey("/D"))
                    out << " " << destination(pdf, action.getKey("/D"));
            }
            out << "\n";
        }
        if (pages[i].hasKey("/Trans"))
            out << "  trans " << values(pages[i].getKey("/Trans")) << "\n";
        if (pages[i].hasKey("/Dur"))
            out << "  dur " << values(pages[i].getKey("/Dur")) << "\n";
    }
    QPDFObjectHandle root = pdf.getRoot();
    if (const auto formats = pdftweak::pdf::readPageLabels(pdf)) {
        out << "labels";
        for (const std::string& label :
             pdftweak::formats::labelStrings(*formats, static_cast<int>(pages.size())))
            out << " " << label;
        out << "\n";
    }
    for (const auto& bookmark : pdftweak::pdf::readBookmarks(pdf, 1))
        out << "bookmark " << bookmark.toString() << "\n";
    for (const char* key : {"/PageMode", "/PageLayout", "/ViewerPreferences", "/Lang"})
        if (root.hasKey(key))
            out << key << " " << values(root.getKey(key)) << "\n";
    QPDFObjectHandle names = root.getKey("/Names");
    if (names.isDictionary() && names.hasKey("/EmbeddedFiles")) {
        QPDFNameTreeObjectHelper tree(names.getKey("/EmbeddedFiles"), pdf);
        for (const auto& [name, spec] : tree) {
            std::string content;
            QPDFObjectHandle stream = spec.getKey("/EF").getKey("/F");
            if (stream.isStream()) {
                const auto data = stream.getStreamData(qpdf_dl_generalized);
                content.assign(reinterpret_cast<const char*>(data->getBuffer()), data->getSize());
            }
            out << "attachment " << name << " file " << values(spec.getKey("/F")) << " content " << content
                << "\n";
        }
    }
    QPDFObjectHandle form = root.getKey("/AcroForm");
    if (form.isDictionary()) {
        QPDFObjectHandle fields = form.getKey("/Fields");
        for (int f = 0; fields.isArray() && f < fields.getArrayNItems(); ++f)
            out << "field " << values(fields.getArrayItem(f).getKey("/T")) << "\n";
        if (form.hasKey("/SigFlags"))
            out << "sigflags " << values(form.getKey("/SigFlags")) << "\n";
    }
    if (root.getKey("/Perms").isDictionary()) {
        QPDFObjectHandle reference =
            root.getKey("/Perms").getKey("/DocMDP").getKey("/Reference").getArrayItem(0);
        out << "docmdp " << values(reference.getKey("/TransformParams").getKey("/P")) << "\n";
    }
    if (pdf.isEncrypted()) {
        int r = 0, p = 0, v = 0;
        QPDF::encryption_method_e stream, string, fileMethod;
        pdf.isEncrypted(r, p, v, stream, string, fileMethod);
        if (options.javaPermissions) {
            if (r == 4)
                p |= 512;
            if ((p & 4) == 0)
                p &= ~2048;
        }
        out << "encrypted R " << r << " V " << v << " P " << p << " stream " << stream << " metadata "
            << values(pdf.getTrailer().getKey("/Encrypt").getKey("/EncryptMetadata")) << "\n";
    }
    QPDFObjectHandle info = pdf.getTrailer().getKey("/Info");
    if (info.isDictionary()) {
        const std::set<std::string> keySet = info.getKeys();
        std::vector<std::string> keys(keySet.begin(), keySet.end());
        std::sort(keys.begin(), keys.end());
        for (const std::string& key : keys)
            if (key != "/Producer" && key != "/CreationDate" && key != "/ModDate")
                out << "info " << key << " " << values(info.getKey(key)) << "\n";
    }
    return out.str();
}

} // namespace pdftweak::test
