#include "core/pdf/input_file.h"

#include "core/pdf/copy_pages.h"

#include "core/error.h"
#include "core/java_text.h"
#include "core/text.h"
#include "core/version.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDFNameTreeObjectHelper.hh>
#include <qpdf/QPDFNumberTreeObjectHelper.hh>
#include <qpdf/QPDFXRefEntry.hh>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>

namespace pdftweak::pdf {
namespace {

// ---- the order of the keys of the Info dictionary in the file ----

bool isWhite(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\0'; }
bool isDelimiter(char c) {
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' ||
           c == '/' || c == '%';
}

// A tokenizer just good enough to step over PDF objects and collect the keys of one dictionary.
class Scanner {
public:
    explicit Scanner(std::string_view text) : s_(text) {}

    // The keys of the dictionary that starts at the first "<<", in their order, with #xx decoded.
    std::optional<std::vector<std::string>> dictionaryKeys() {
        const size_t start = s_.find("<<");
        if (start == std::string_view::npos)
            return std::nullopt;
        i_ = start + 2;
        std::vector<std::string> keys;
        while (true) {
            skipSpace();
            if (i_ + 1 >= s_.size())
                return std::nullopt;
            if (s_[i_] == '>' && s_[i_ + 1] == '>')
                return keys;
            if (s_[i_] != '/')
                return std::nullopt;
            keys.push_back(decodeName(token()));
            if (!skipObject())
                return std::nullopt;
        }
    }

private:
    void skipSpace() {
        while (i_ < s_.size()) {
            if (isWhite(s_[i_])) {
                ++i_;
            } else if (s_[i_] == '%') {
                while (i_ < s_.size() && s_[i_] != '\r' && s_[i_] != '\n')
                    ++i_;
            } else {
                break;
            }
        }
    }

    // A name (with its slash) or a regular token.
    std::string_view token() {
        const size_t start = i_;
        if (i_ < s_.size() && s_[i_] == '/')
            ++i_;
        while (i_ < s_.size() && !isWhite(s_[i_]) && !isDelimiter(s_[i_]))
            ++i_;
        return s_.substr(start, i_ - start);
    }

    static bool isInteger(std::string_view t) {
        return !t.empty() && std::all_of(t.begin(), t.end(), [](char c) { return c >= '0' && c <= '9'; });
    }

    bool skipObject() {
        skipSpace();
        if (i_ >= s_.size())
            return false;
        const char c = s_[i_];
        if (c == '(') {
            int depth = 0;
            for (; i_ < s_.size(); ++i_) {
                if (s_[i_] == '\\')
                    ++i_;
                else if (s_[i_] == '(')
                    ++depth;
                else if (s_[i_] == ')' && --depth == 0)
                    return ++i_, true;
            }
            return false;
        }
        if (c == '<' && i_ + 1 < s_.size() && s_[i_ + 1] == '<') {
            i_ += 2;
            while (true) {
                skipSpace();
                if (i_ + 1 >= s_.size())
                    return false;
                if (s_[i_] == '>' && s_[i_ + 1] == '>')
                    return i_ += 2, true;
                if (!skipObject())
                    return false;
            }
        }
        if (c == '<') {
            const size_t end = s_.find('>', i_);
            if (end == std::string_view::npos)
                return false;
            i_ = end + 1;
            return true;
        }
        if (c == '[') {
            ++i_;
            while (true) {
                skipSpace();
                if (i_ >= s_.size())
                    return false;
                if (s_[i_] == ']')
                    return ++i_, true;
                if (!skipObject())
                    return false;
            }
        }
        const std::string_view first = token();
        if (first.empty())
            return false;
        if (isInteger(first)) {
            // an indirect reference "n g R" counts as one object
            const size_t mark = i_;
            skipSpace();
            const std::string_view second = token();
            skipSpace();
            const std::string_view third = token();
            if (!(isInteger(second) && third == "R"))
                i_ = mark;
        }
        return true;
    }

    static std::string decodeName(std::string_view name) {
        std::string decoded = "/";
        for (size_t k = 1; k < name.size(); ++k) {
            if (name[k] == '#' && k + 2 < name.size()) {
                decoded += static_cast<char>(std::stoi(std::string(name.substr(k + 1, 2)), nullptr, 16));
                k += 2;
            } else {
                decoded += name[k];
            }
        }
        return decoded;
    }

    std::string_view s_;
    size_t i_ = 0;
};

// The Info keys in the order the file writes them, which iText's LinkedHashMap keeps and qpdf does
// not. Falls back to qpdf's order where the dictionary cannot be found or read.
std::vector<std::string> infoKeyOrder(QPDF& pdf, const std::filesystem::path& file) {
    QPDFObjectHandle info = pdf.getTrailer().getKey("/Info");
    std::vector<std::string> sorted;
    if (info.isDictionary())
        for (const std::string& key : info.getKeys())
            sorted.push_back(key);
    if (!info.isIndirect())
        return sorted;
    std::optional<std::vector<std::string>> keys;
    try {
        const std::map<QPDFObjGen, QPDFXRefEntry> xref = pdf.getXRefTable();
        const auto entry = xref.find(info.getObjGen());
        if (entry != xref.end() && entry->second.getType() == 1) {
            std::ifstream in(file, std::ios::binary);
            in.seekg(entry->second.getOffset());
            std::string text(65536, '\0');
            in.read(text.data(), static_cast<std::streamsize>(text.size()));
            text.resize(static_cast<size_t>(in.gcount()));
            keys = Scanner(text).dictionaryKeys();
        } else if (entry != xref.end() && entry->second.getType() == 2) {
            QPDFObjectHandle stream = pdf.getObject(entry->second.getObjStreamNumber(), 0);
            const std::shared_ptr<Buffer> buffer = stream.getStreamData(qpdf_dl_generalized);
            const std::string data(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
            const long long first = stream.getDict().getKey("/First").getIntValue();
            // the header holds pairs of object number and offset
            std::string_view header(data);
            header = header.substr(0, static_cast<size_t>(first));
            std::vector<long long> numbers;
            size_t k = 0;
            while (k < header.size()) {
                while (k < header.size() && isWhite(header[k]))
                    ++k;
                size_t start = k;
                while (k < header.size() && header[k] >= '0' && header[k] <= '9')
                    ++k;
                if (k == start)
                    break;
                numbers.push_back(std::stoll(std::string(header.substr(start, k - start))));
            }
            for (size_t n = 0; n + 1 < numbers.size(); n += 2) {
                if (numbers[n] == info.getObjectID()) {
                    keys =
                        Scanner(std::string_view(data).substr(static_cast<size_t>(first + numbers[n + 1])))
                            .dictionaryKeys();
                    break;
                }
            }
        }
    } catch (const std::exception&) {
        keys.reset();
    }
    if (!keys)
        return sorted;
    std::vector<std::string> order;
    for (const std::string& key : *keys)
        if (std::find(sorted.begin(), sorted.end(), key) != sorted.end() &&
            std::find(order.begin(), order.end(), key) == order.end())
            order.push_back(key);
    for (const std::string& key : sorted)
        if (std::find(order.begin(), order.end(), key) == order.end())
            order.push_back(key);
    return order;
}

// ---- PdfReader.consolidateNamedDestinations ----

struct NamedDestinations {
    std::map<std::string, QPDFObjectHandle> byName;   // keys of the /Dests dictionary
    std::map<std::string, QPDFObjectHandle> byString; // keys of the /Names /Dests tree

    bool empty() const { return byName.empty() && byString.empty(); }
};

// PdfReader.getNameArray: a destination array, given directly or as the /D of a dictionary.
QPDFObjectHandle nameArray(QPDFObjectHandle value) {
    if (value.isArray())
        return value;
    if (value.isDictionary() && value.getKey("/D").isArray())
        return value.getKey("/D");
    return QPDFObjectHandle::newNull();
}

NamedDestinations namedDestinations(QPDF& pdf) {
    NamedDestinations names;
    QPDFObjectHandle root = pdf.getRoot();
    QPDFObjectHandle dests = root.getKey("/Dests");
    if (dests.isDictionary()) {
        for (const std::string& key : dests.getKeys()) {
            QPDFObjectHandle array = nameArray(dests.getKey(key));
            if (array.isArray())
                names.byName[key] = array;
        }
    }
    QPDFObjectHandle tree = root.getKey("/Names").getKey("/Dests");
    if (tree.isDictionary()) {
        try {
            for (const auto& [key, value] : QPDFNameTreeObjectHelper(tree, pdf, false)) {
                QPDFObjectHandle array = nameArray(value);
                if (array.isArray())
                    names.byString[key] = array;
            }
        } catch (const std::exception&) {
            // a broken name tree gives the names read so far, as iText's reader does
        }
    }
    return names;
}

// The destination a name or string stands for, or null.
QPDFObjectHandle lookup(const NamedDestinations& names, QPDFObjectHandle name) {
    if (name.isName()) {
        const auto found = names.byName.find(name.getName());
        if (found != names.byName.end())
            return found->second;
    } else if (name.isString()) {
        const auto found = names.byString.find(name.getUTF8Value());
        if (found != names.byString.end())
            return found->second;
    }
    return QPDFObjectHandle::newNull();
}

// PdfReader.replaceNamedDestination.
void replaceNamedDestination(QPDFObjectHandle object, const NamedDestinations& names) {
    if (!object.isDictionary())
        return;
    QPDFObjectHandle dest = object.getKey("/Dest");
    if (!dest.isNull()) {
        QPDFObjectHandle array = lookup(names, dest);
        if (array.isArray())
            object.replaceKey("/Dest", array);
        return;
    }
    QPDFObjectHandle action = object.getKey("/A");
    if (action.isDictionary() && action.getKey("/S").isNameAndEquals("/GoTo")) {
        QPDFObjectHandle array = lookup(names, action.getKey("/D"));
        if (array.isArray())
            action.replaceKey("/D", array);
    }
}

void consolidateNamedDestinations(QPDF& pdf) {
    const NamedDestinations names = namedDestinations(pdf);
    if (names.empty())
        return;
    for (QPDFPageObjectHelper& page : pages(pdf)) {
        QPDFObjectHandle annots = page.getObjectHandle().getKey("/Annots");
        if (annots.isArray())
            for (QPDFObjectHandle annot : annots.aitems())
                replaceNamedDestination(annot, names);
    }
    QPDFObjectHandle outlines = pdf.getRoot().getKey("/Outlines");
    if (!outlines.isDictionary())
        return;
    // PdfReader.iterateBookmarks, without recursion and safe against loops
    std::set<QPDFObjGen> seen;
    std::vector<QPDFObjectHandle> pending{outlines.getKey("/First")};
    while (!pending.empty()) {
        QPDFObjectHandle item = pending.back();
        pending.pop_back();
        while (item.isDictionary()) {
            if (item.isIndirect() && !seen.insert(item.getObjGen()).second)
                break;
            replaceNamedDestination(item, names);
            if (item.hasKey("/First"))
                pending.push_back(item.getKey("/First"));
            item = item.getKey("/Next");
        }
    }
}

QPDFObjectHandle encryptDictionary(QPDF& pdf) { return pdf.getTrailer().getKey("/Encrypt"); }

int revision(QPDF& pdf) {
    QPDFObjectHandle r = encryptDictionary(pdf).getKey("/R");
    return r.isInteger() ? r.getIntValueAsInt() : 0;
}

} // namespace

InputFile::InputFile(std::filesystem::path file, const std::string& password)
    : file_(std::move(file)), ownerPassword_(password) {
    reopen();
}

void InputFile::reopen() {
    pdf_ = openFile(file_, toLatin1(ownerPassword_));
    try {
        if (pdf_->isEncrypted() && !pdf_->ownerPasswordMatched())
            throw Error(Error::Kind::BadPassword, "PdfReader not opened with owner password");
        consolidateNamedDestinations(*pdf_);
        infoOrder_ = std::make_shared<std::vector<std::string>>(infoKeyOrder(*pdf_, file_));
    } catch (...) {
        rethrowAsError();
    }
}

int InputFile::pageCount() const { return static_cast<int>(pages(*pdf_).size()); }

Rect InputFile::pageSize(int page) const {
    return pageSizeWithRotation(pages(*pdf_).at(static_cast<size_t>(page - 1)).getObjectHandle());
}

std::vector<std::pair<std::string, std::string>> InputFile::infoDictionary() const {
    return readInfoDictionary(*pdf_, *infoOrder_);
}

int InputFile::cryptoMode() const {
    if (!pdf_->isEncrypted())
        return -1;
    int r = 0, p = 0, v = 0;
    QPDF::encryption_method_e streams, strings, files;
    pdf_->isEncrypted(r, p, v, streams, strings, files);
    int mode = 0;
    if (r == 2)
        mode = 0;
    else if (r == 3)
        mode = 1;
    else if (r == 4)
        mode = streams == QPDF::e_aes ? 2 : 1;
    else
        mode = 3;
    if (r >= 4 && !metadataEncrypted())
        mode |= 8;
    return mode;
}

bool InputFile::metadataEncrypted() const {
    if (!pdf_->isEncrypted())
        return false;
    if (revision(*pdf_) < 4)
        return true;
    QPDFObjectHandle flag = encryptDictionary(*pdf_).getKey("/EncryptMetadata");
    return !(flag.isBool() && !flag.getBoolValue());
}

int InputFile::permissions() const {
    if (!pdf_->isEncrypted())
        return 0;
    int r = 0, p = 0;
    pdf_->isEncrypted(r, p);
    // 40-bit encryption does not support some flags, but sets them.
    if (r == 2)
        return static_cast<int>(static_cast<unsigned>(p) & 0xFFFF00FFu);
    return p;
}

std::string InputFile::userPassword() const {
    if (!pdf_->isEncrypted() || !pdf_->ownerPasswordMatched() || revision(*pdf_) >= 5)
        return "";
    return javaUtf8Decode(pdf_->getTrimmedUserPassword());
}

std::optional<std::vector<formats::PageLabelFormat>> InputFile::pageLabels() const {
    return readPageLabels(*pdf_);
}

std::vector<std::pair<std::string, std::string>> readInfoDictionary(QPDF& pdf,
                                                                    const std::vector<std::string>& order) {
    QPDFObjectHandle info = pdf.getTrailer().getKey("/Info");
    std::vector<std::string> keys;
    std::vector<std::string> values;
    if (info.isDictionary()) {
        std::vector<std::string> ordered;
        for (const std::string& key : order)
            if (info.hasKey(key))
                ordered.push_back(key);
        for (const std::string& key : info.getKeys())
            if (std::find(ordered.begin(), ordered.end(), key) == ordered.end())
                ordered.push_back(key);
        for (const std::string& key : ordered) {
            QPDFObjectHandle value = info.getKey(key);
            std::string text;
            if (value.isNull())
                continue;
            if (value.isString())
                text = value.getUTF8Value();
            else if (value.isName())
                text = value.getName().substr(1);
            else
                text = value.unparseResolved();
            keys.push_back(key.substr(1));
            values.push_back(text);
        }
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i] == "Producer" &&
            values[i].find("modified by " PDFTWEAK_PROGRAM_NAME) == std::string::npos) {
            values[i] += "; modified by " + defaultProducer();
        }
    }
    std::vector<std::pair<std::string, std::string>> result;
    for (const size_t i : javaHashMapOrder(keys))
        result.emplace_back(keys[i], values[i]);
    return result;
}

std::optional<std::vector<formats::PageLabelFormat>> readPageLabels(QPDF& pdf) {
    QPDFObjectHandle labels = pdf.getRoot().getKey("/PageLabels");
    if (!labels.isDictionary())
        return std::nullopt;
    std::vector<formats::PageLabelFormat> formats;
    try {
        for (const auto& [key, value] : QPDFNumberTreeObjectHelper(labels, pdf, false)) {
            if (!value.isDictionary())
                continue;
            formats::PageLabelFormat format;
            format.physicalPage = static_cast<int>(key) + 1;
            QPDFObjectHandle start = value.getKey("/St");
            format.logicalPage = start.isNumber() ? static_cast<int>(start.getNumericValue()) : 1;
            QPDFObjectHandle prefix = value.getKey("/P");
            format.prefix = prefix.isString() ? prefix.getUTF8Value() : std::string();
            QPDFObjectHandle style = value.getKey("/S");
            if (!style.isName()) {
                format.style = formats::LabelStyle::Empty;
            } else {
                const std::string name = style.getName();
                const char type = name.size() > 1 ? name[1] : 'D';
                format.style = type == 'R'   ? formats::LabelStyle::UpperRoman
                               : type == 'r' ? formats::LabelStyle::LowerRoman
                               : type == 'A' ? formats::LabelStyle::UpperLetters
                               : type == 'a' ? formats::LabelStyle::LowerLetters
                                             : formats::LabelStyle::Decimal;
            }
            formats.push_back(format);
        }
    } catch (const std::exception&) {
        // a broken number tree gives the labels read so far
    }
    return formats;
}

} // namespace pdftweak::pdf
