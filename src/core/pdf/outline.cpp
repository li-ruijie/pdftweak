#include "core/pdf/outline.h"

#include "core/error.h"
#include "core/java_text.h"
#include "core/pdf/qpdf_util.h"
#include "core/text.h"

#include <map>
#include <set>
#include <stdexcept>

namespace pdftweak::pdf {
namespace {

// One map of SimpleBookmark.getBookmark: its entries in the order they were put, and the kids.
struct Item {
    std::vector<std::pair<std::string, std::string>> entries;
    std::vector<Item> kids;
    bool hasKids = false;

    void put(const std::string& key, const std::string& value) {
        for (auto& entry : entries) {
            if (entry.first == key) {
                entry.second = value;
                return;
            }
        }
        entries.emplace_back(key, value);
    }

    const std::string* get(const std::string& key) const {
        for (const auto& entry : entries)
            if (entry.first == key)
                return &entry.second;
        return nullptr;
    }
};

// Thrown where the Java code fails inside the try block that SimpleBookmark ignores.
struct Skipped : std::runtime_error {
    Skipped() : std::runtime_error("skipped") {}
};

// PdfObject.toString for the objects destinations hold. An element of an array prints as the
// indirect reference it is, unless the Java code resolved it first (resolved).
std::string javaToString(QPDFObjectHandle object, bool resolved = false) {
    if (!resolved && object.isIndirect())
        return std::to_string(object.getObjectID()) + " " + std::to_string(object.getGeneration()) + " R";
    if (object.isString())
        return latin1ToUtf8(object.getStringValue());
    if (object.isDictionary()) {
        QPDFObjectHandle type = object.getKey("/Type");
        return type.isName() ? "Dictionary of type: " + type.unparse() : "Dictionary";
    }
    if (object.isArray()) {
        std::string text = "[";
        for (int i = 0; i < object.getArrayNItems(); ++i)
            text += (i ? ", " : "") + javaToString(object.getArrayItem(i));
        return text + "]";
    }
    return object.unparse();
}

std::string unicode(QPDFObjectHandle string) {
    if (!string.isString())
        throw Skipped();
    return string.getUTF8Value();
}

// SimpleBookmark.makeBookmarkParam.
std::string bookmarkParam(QPDFObjectHandle dest, const std::map<int, int>& pages) {
    QPDFObjectHandle first = dest.getArrayItem(0);
    std::string text;
    if (first.isNumber() && !first.isIndirect()) {
        text = std::to_string(static_cast<int>(first.getNumericValue()) + 1);
    } else if (first.isIndirect()) {
        // getNumber: a /Pages node stands for its first kid
        int number = first.getObjectID();
        if (first.isDictionary() && first.getKey("/Type").isNameAndEquals("/Pages") &&
            first.getKey("/Kids").isArray() && first.getKey("/Kids").getArrayItem(0).isIndirect())
            number = first.getKey("/Kids").getArrayItem(0).getObjectID();
        const auto found = pages.find(number);
        text = std::to_string(found == pages.end() ? 0 : found->second);
    } else {
        throw Skipped(); // the ClassCastException of a direct object
    }
    if (dest.getArrayNItems() < 2)
        throw Skipped();
    text += " " + javaToString(dest.getArrayItem(1)).substr(1);
    for (int k = 2; k < dest.getArrayNItems(); ++k)
        text += " " + javaToString(dest.getArrayItem(k));
    return text;
}

// SimpleBookmark.mapGotoBookmark.
void mapGotoBookmark(Item& item, QPDFObjectHandle dest, const std::map<int, int>& pages) {
    if (dest.isString())
        item.put("Named", javaToString(dest, true));
    else if (dest.isName())
        item.put("Named", latin1ToUtf8(dest.getName().substr(1)));
    else if (dest.isArray())
        item.put("Page", bookmarkParam(dest, pages));
    item.put("Action", "GoTo");
}

std::string fileName(QPDFObjectHandle file) {
    if (file.isString())
        return file.getUTF8Value();
    if (file.isDictionary() && file.getKey("/F").isString())
        return file.getKey("/F").getUTF8Value();
    throw Skipped();
}

// SimpleBookmark.bookmarkDepth.
std::vector<Item> bookmarkDepth(QPDFObjectHandle outline, const std::map<int, int>& pages,
                                std::set<QPDFObjGen>& seen) {
    std::vector<Item> list;
    while (outline.isDictionary()) {
        if (outline.isIndirect() && !seen.insert(outline.getObjGen()).second)
            break;
        Item item;
        QPDFObjectHandle title = outline.getKey("/Title");
        item.put("Title", title.isString() ? title.getUTF8Value() : std::string());
        QPDFObjectHandle color = outline.getKey("/C");
        if (color.isArray() && color.getArrayNItems() == 3 && color.getArrayItem(0).isNumber() &&
            color.getArrayItem(1).isNumber() && color.getArrayItem(2).isNumber()) {
            std::string text;
            for (int i = 0; i < 3; ++i)
                text += (i ? " " : "") +
                        itextNumber(static_cast<float>(color.getArrayItem(i).getNumericValue()));
            item.put("Color", text);
        }
        QPDFObjectHandle style = outline.getKey("/F");
        if (style.isNumber()) {
            const int f = static_cast<int>(style.getNumericValue());
            std::string s;
            if ((f & 1) != 0)
                s += "italic ";
            if ((f & 2) != 0)
                s += "bold ";
            s = javaTrim(s);
            if (!s.empty())
                item.put("Style", s);
        }
        QPDFObjectHandle count = outline.getKey("/Count");
        if (count.isNumber() && static_cast<int>(count.getNumericValue()) < 0)
            item.put("Open", "false");
        try {
            QPDFObjectHandle dest = outline.getKey("/Dest");
            QPDFObjectHandle action = outline.getKey("/A");
            if (!dest.isNull()) {
                mapGotoBookmark(item, dest, pages);
            } else if (action.isDictionary()) {
                QPDFObjectHandle type = action.getKey("/S");
                if (type.isNameAndEquals("/GoTo")) {
                    dest = action.getKey("/D");
                    if (!dest.isNull())
                        mapGotoBookmark(item, dest, pages);
                } else if (type.isNameAndEquals("/URI")) {
                    item.put("Action", "URI");
                    item.put("URI", unicode(action.getKey("/URI")));
                } else if (type.isNameAndEquals("/JavaScript")) {
                    item.put("Action", "JS");
                    item.put("Code", javaToString(action.getKey("/JS"), true));
                } else if (type.isNameAndEquals("/GoToR")) {
                    dest = action.getKey("/D");
                    if (dest.isString()) {
                        item.put("Named", javaToString(dest, true));
                    } else if (dest.isName()) {
                        item.put("NamedN", latin1ToUtf8(dest.getName().substr(1)));
                    } else if (dest.isArray()) {
                        std::string s =
                            javaToString(dest.getArrayItem(0)) + " " + javaToString(dest.getArrayItem(1));
                        for (int k = 2; k < dest.getArrayNItems(); ++k)
                            s += " " + javaToString(dest.getArrayItem(k));
                        item.put("Page", s);
                    }
                    item.put("Action", "GoToR");
                    QPDFObjectHandle file = action.getKey("/F");
                    if (!file.isNull())
                        item.put("File", fileName(file));
                    QPDFObjectHandle newWindow = action.getKey("/NewWindow");
                    if (!newWindow.isNull())
                        item.put("NewWindow", javaToString(newWindow, true));
                } else if (type.isNameAndEquals("/Launch")) {
                    item.put("Action", "Launch");
                    QPDFObjectHandle file = action.getKey("/F");
                    if (file.isNull())
                        file = action.getKey("/Win");
                    if (!file.isNull())
                        item.put("File", fileName(file));
                }
            }
        } catch (const Skipped&) {
            // SimpleBookmark ignores exceptions here, keeping what it put so far
        }
        QPDFObjectHandle first = outline.getKey("/First");
        if (first.isDictionary()) {
            item.hasKids = true;
            item.put("Kids", "");
            item.kids = bookmarkDepth(first, pages, seen);
        }
        list.push_back(std::move(item));
        outline = outline.getKey("/Next");
    }
    return list;
}

// PdfBookmark.fillInBookmark.
void fillInBookmark(std::vector<formats::Bookmark>& result, const Item& item, int depth) {
    // new HashMap(bookmark_) of a HashMap whose keys went in in the order of item.entries
    std::vector<std::string> keys;
    for (const auto& entry : item.entries)
        keys.push_back(entry.first);
    std::vector<std::string> source;
    for (const size_t i : javaHashMapOrder(keys))
        source.push_back(keys[i]);
    std::vector<std::string> copied;
    for (const size_t i : javaHashMapOrder(source, javaHashMapCopyCapacity(source.size())))
        copied.push_back(source[i]);

    std::set<std::string> removed{"Kids"};
    int pageNumber = 0;
    std::string pagePosition;
    const std::string* action = item.get("Action");
    const std::string* page = item.get("Page");
    if (action && *action == "GoTo" && page) {
        removed.insert("Action");
        removed.insert("Page");
        const std::string text = javaTrim(*page);
        const size_t pos = text.find(' ');
        if (pos == std::string::npos) {
            pageNumber = javaParseInt(text);
        } else {
            pageNumber = javaParseInt(text.substr(0, pos));
            pagePosition = text.substr(pos + 1);
        }
        if (pageNumber == 0)
            throw Error(Error::Kind::Document, "java.lang.RuntimeException");
    }
    bool open = true;
    if (const std::string* value = item.get("Open")) {
        removed.insert("Open");
        if (*value == "false")
            open = false;
    }
    const std::string* style = item.get("Style");
    removed.insert("Style");
    const bool italic = style && style->find("italic") != std::string::npos;
    const bool bold = style && style->find("bold") != std::string::npos;
    removed.insert("Title");
    formats::BookmarkOptions more;
    for (const std::string& key : copied)
        if (!removed.contains(key))
            more.emplace_back(key, *item.get(key));
    result.push_back(formats::makeBookmark(depth, *item.get("Title"), open, pageNumber, pagePosition, bold,
                                           italic, formats::makeOptionString(more)));
    for (const Item& kid : item.kids)
        fillInBookmark(result, kid, depth + 1);
}

} // namespace

std::vector<formats::Bookmark> readBookmarks(QPDF& pdf, int initialDepth) {
    std::vector<formats::Bookmark> result;
    QPDFObjectHandle outlines = pdf.getRoot().getKey("/Outlines");
    if (!outlines.isDictionary())
        return result;
    std::map<int, int> pageNumbers;
    int number = 1;
    for (QPDFPageObjectHelper& page : pages(pdf))
        pageNumbers[page.getObjectHandle().getObjectID()] = number++;
    std::set<QPDFObjGen> seen;
    for (const Item& item : bookmarkDepth(outlines.getKey("/First"), pageNumbers, seen))
        fillInBookmark(result, item, initialDepth);
    return result;
}

} // namespace pdftweak::pdf
