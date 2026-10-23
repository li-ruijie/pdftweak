#include "core/pdf/qpdf_util.h"

#include "core/error.h"
#include "core/java_text.h"
#include "core/progress.h"
#include "core/text.h"

#include <qpdf/Buffer.hh>
#include <qpdf/BufferInputSource.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <new>

namespace pdftweak::pdf {

std::string utf8Path(const std::filesystem::path& path) { return toUtf8(path.wstring()); }

std::shared_ptr<QPDF> openFile(const std::filesystem::path& path, const std::string& password) {
    const std::filesystem::path absolute = std::filesystem::absolute(path);
    std::error_code ignored;
    if (!std::filesystem::is_regular_file(absolute, ignored))
        throw Error(Error::Kind::Io, utf8Path(absolute) + " not found as file or resource.");
    try {
        auto pdf = std::make_shared<QPDF>();
        pdf->setSuppressWarnings(true);
        pdf->processFile(utf8Path(absolute).c_str(), password.c_str());
        return pdf;
    } catch (...) {
        rethrowAsError();
    }
}

std::shared_ptr<QPDF> openBytes(std::string bytes, const std::string& description) {
    try {
        auto pdf = std::make_shared<QPDF>();
        pdf->setSuppressWarnings(true);
        auto source = std::make_shared<BufferInputSource>(description, new Buffer(std::move(bytes)), true);
        pdf->processInputSource(source);
        return pdf;
    } catch (...) {
        rethrowAsError();
    }
}

std::string writeBytes(QPDF& pdf) {
    try {
        QPDFWriter writer(pdf);
        writer.setOutputMemory();
        writer.setPreserveEncryption(false);
        writer.write();
        const std::shared_ptr<Buffer> buffer = writer.getBufferSharedPointer();
        return std::string(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
    } catch (...) {
        rethrowAsError();
    }
}

std::vector<QPDFPageObjectHelper> pages(QPDF& pdf) { return QPDFPageDocumentHelper(pdf).getAllPages(); }

Rect rectangle(QPDFObjectHandle array) {
    float v[4] = {0, 0, 0, 0};
    if (array.isArray()) {
        for (int i = 0; i < 4 && i < array.getArrayNItems(); ++i) {
            QPDFObjectHandle item = array.getArrayItem(i);
            if (item.isNumber())
                v[i] = static_cast<float>(item.getNumericValue());
        }
    }
    return {std::min(v[0], v[2]), std::min(v[1], v[3]), std::max(v[0], v[2]), std::max(v[1], v[3])};
}

Rect pageSize(QPDFObjectHandle page) {
    QPDFObjectHandle box = QPDFPageObjectHelper(page).getMediaBox();
    if (!box.isArray())
        return {0, 0, 612, 792};
    return rectangle(box);
}

int pageRotation(QPDFObjectHandle page) {
    QPDFObjectHandle rotate = QPDFPageObjectHelper(page).getAttribute("/Rotate", false);
    if (!rotate.isNumber())
        return 0;
    const int n = static_cast<int>(rotate.getNumericValue()) % 360;
    return n < 0 ? n + 360 : n;
}

Rect pageSizeWithRotation(QPDFObjectHandle page) {
    Rect r = pageSize(page);
    for (int rotation = pageRotation(page); rotation > 0; rotation -= 90)
        r = {r.lly, r.llx, r.ury, r.urx};
    return r;
}

QPDFObjectHandle copyFrom(QPDF& to, QPDFObjectHandle object) {
    if (object.isIndirect())
        return to.copyForeignObject(object);
    if (object.isDictionary()) {
        QPDFObjectHandle copy = QPDFObjectHandle::newDictionary();
        for (const std::string& key : object.getKeys())
            copy.replaceKey(key, copyFrom(to, object.getKey(key)));
        return copy;
    }
    if (object.isArray()) {
        QPDFObjectHandle copy = QPDFObjectHandle::newArray();
        for (int i = 0; i < object.getArrayNItems(); ++i)
            copy.appendItem(copyFrom(to, object.getArrayItem(i)));
        return copy;
    }
    return QPDFObjectHandle::parse(object.unparse());
}

QPDFObjectHandle itextNumberObject(double value) {
    const std::string text = itextNumber(value);
    if (text.find('.') == std::string::npos)
        return QPDFObjectHandle::newInteger(std::stoll(text));
    return QPDFObjectHandle::newReal(text);
}

void rethrowAsError() {
    try {
        throw;
    } catch (const Error&) {
        throw;
    } catch (const Cancelled&) {
        throw;
    } catch (const QPDFExc& e) {
        if (e.getErrorCode() == qpdf_e_password)
            throw Error(Error::Kind::BadPassword, "Bad user password");
        throw Error(Error::Kind::Document, e.what());
    } catch (const std::bad_alloc&) {
        throw Error(Error::Kind::Io,
                    "PDF Tweak has run out of memory. You can enable the Tempfile option on "
                    "the output tab.");
    } catch (const std::exception& e) {
        throw Error(Error::Kind::Io, e.what());
    }
}

} // namespace pdftweak::pdf
