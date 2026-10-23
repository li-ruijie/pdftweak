// The PdfStamper steps of PdfTweak, which change the current document in place: addWatermark,
// updateBookmarks, and setPageNumbers, and the settings that the final write applies (transitions,
// viewer preferences, and attachments). Each step ends as Java's does, by writing the document and
// reading it back.

#include "core/pdf/tweak.h"

#include "core/error.h"
#include "core/java_text.h"
#include "core/pdf/copy_pages.h"
#include "core/pdf/encodings.h"
#include "core/pdf/helvetica.h"
#include "core/pdf/rebuild.h"
#include "core/pdf/stamper.h"
#include "core/text.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>

namespace pdftweak::pdf {
namespace {

// ---- bookmarks ----

// PdfBookmark.toBookmark: the map of one outline item for SimpleBookmark, with its kids.
struct OutlineItem {
    std::map<std::string, std::string> map;
    std::vector<OutlineItem> kids;
};

OutlineItem toBookmark(const formats::Bookmark& b, std::vector<OutlineItem> kids) {
    OutlineItem item;
    for (const auto& [key, value] : b.more)
        item.map[key] = value;
    item.kids = std::move(kids);
    if (b.page != 0) {
        item.map["Action"] = "GoTo";
        item.map["Page"] = std::to_string(b.page) + (b.pagePosition.empty() ? "" : " " + b.pagePosition);
    }
    item.map["Open"] = b.open ? "true" : "false";
    if (b.bold || b.italic)
        item.map["Style"] = javaTrim(std::string(b.italic ? "italic " : "") + (b.bold ? "bold" : ""));
    item.map["Title"] = b.title;
    return item;
}

// PdfBookmark.makeBookmarks
std::vector<OutlineItem> makeBookmarks(const std::vector<formats::Bookmark>& bookmarks, int depth,
                                       size_t from, size_t to) {
    std::vector<OutlineItem> result;
    size_t pos = from;
    while (pos < to) {
        const formats::Bookmark& b = bookmarks[pos];
        if (b.depth != depth)
            throw Error(Error::Kind::Io, "Invalid depth value");
        size_t endPos = pos + 1;
        while (endPos < to && bookmarks[endPos].depth > depth)
            ++endPos;
        std::vector<OutlineItem> kids;
        if (endPos > pos + 1)
            kids = makeBookmarks(bookmarks, depth + 1, pos + 1, endPos);
        result.push_back(toBookmark(b, std::move(kids)));
        pos = endPos;
    }
    return result;
}

const std::string* get(const std::map<std::string, std::string>& map, const char* key) {
    const auto found = map.find(key);
    return found == map.end() ? nullptr : &found->second;
}

// java.util.StringTokenizer with its default delimiters
std::vector<std::string> tokens(const std::string& text) {
    std::vector<std::string> result;
    std::string current;
    for (const char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
            if (!current.empty())
                result.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty())
        result.push_back(current);
    return result;
}

// new PdfNumber(String): the text as written, once Double.parseDouble accepts it
QPDFObjectHandle numberObject(const std::string& text) {
    javaParseDouble(javaTrim(text));
    return QPDFObjectHandle::newReal(text);
}

// The rest of a GoTo or GoToR "Page" value after its page: the fit type and up to four numbers,
// or XYZ 0 10000 0 when there is none.
void appendPosition(QPDFObjectHandle array, const std::vector<std::string>& tk) {
    if (tk.size() < 2) {
        array.appendItem(QPDFObjectHandle::newName("/XYZ"));
        for (const int n : {0, 10000, 0})
            array.appendItem(QPDFObjectHandle::newInteger(n));
        return;
    }
    std::string fn = tk[1];
    if (!fn.empty() && fn[0] == '/')
        fn = fn.substr(1);
    array.appendItem(QPDFObjectHandle::newName("/" + fn));
    for (size_t k = 2; k < tk.size() && k < 6; ++k)
        array.appendItem(tk[k] == "null" ? QPDFObjectHandle::newNull() : numberObject(tk[k]));
}

// SimpleBookmark.createOutlineAction. Like iText, it leaves the item without an action when any
// part of the action fails, such as a page that does not exist.
void createOutlineAction(QPDF& pdf, QPDFObjectHandle outline, const std::map<std::string, std::string>& map,
                         bool namedAsNames) {
    try {
        const std::string* action = get(map, "Action");
        if (action == nullptr)
            return;
        if (*action == "GoTo") {
            if (const std::string* named = get(map, "Named")) {
                outline.replaceKey("/Dest", namedAsNames ? QPDFObjectHandle::newName("/" + *named)
                                                         : QPDFObjectHandle::newString(lowBytes(*named)));
            } else if (const std::string* page = get(map, "Page")) {
                const std::vector<std::string> tk = tokens(*page);
                const int n = javaParseInt(tk.at(0));
                const std::vector<QPDFPageObjectHelper> all = pages(pdf);
                if (n < 1 || n > static_cast<int>(all.size()))
                    return; // PdfStamperImp.getPageReference refuses it
                QPDFObjectHandle array = QPDFObjectHandle::newArray();
                array.appendItem(all[static_cast<size_t>(n - 1)].getObjectHandle());
                appendPosition(array, tk);
                outline.replaceKey("/Dest", array);
            }
        } else if (*action == "GoToR") {
            QPDFObjectHandle dic = QPDFObjectHandle::newDictionary();
            if (const std::string* named = get(map, "Named")) {
                dic.replaceKey("/D", QPDFObjectHandle::newString(lowBytes(*named)));
            } else if (const std::string* namedN = get(map, "NamedN")) {
                dic.replaceKey("/D", QPDFObjectHandle::newName("/" + *namedN));
            } else if (const std::string* page = get(map, "Page")) {
                const std::vector<std::string> tk = tokens(*page);
                QPDFObjectHandle array = QPDFObjectHandle::newArray();
                array.appendItem(numberObject(tk.at(0)));
                appendPosition(array, tk);
                dic.replaceKey("/D", array);
            }
            const std::string* file = get(map, "File");
            if (!dic.getKeys().empty() && file != nullptr) {
                dic.replaceKey("/S", QPDFObjectHandle::newName("/GoToR"));
                dic.replaceKey("/F", QPDFObjectHandle::newString(pdfDocBytes(*file)));
                if (const std::string* nw = get(map, "NewWindow")) {
                    if (*nw == "true" || *nw == "false")
                        dic.replaceKey("/NewWindow", QPDFObjectHandle::newBool(*nw == "true"));
                }
                outline.replaceKey("/A", dic);
            }
        } else if (*action == "URI") {
            if (const std::string* uri = get(map, "URI")) {
                QPDFObjectHandle dic = QPDFObjectHandle::newDictionary();
                dic.replaceKey("/S", QPDFObjectHandle::newName("/URI"));
                dic.replaceKey("/URI", QPDFObjectHandle::newString(pdfDocBytes(*uri)));
                outline.replaceKey("/A", dic);
            }
        } else if (*action == "JS") {
            if (const std::string* code = get(map, "Code")) {
                // PdfAction.javaScript: a string below 100 characters, a stream otherwise
                QPDFObjectHandle dic = QPDFObjectHandle::newDictionary();
                dic.replaceKey("/S", QPDFObjectHandle::newName("/JavaScript"));
                size_t length = 0;
                for (const char32_t c : codePoints(*code))
                    length += c > 0xFFFF ? 2 : 1;
                if (length < 100)
                    dic.replaceKey("/JS", QPDFObjectHandle::newString(pdfDocBytes(*code)));
                else
                    dic.replaceKey("/JS", pdf.newStream(pdfDocBytes(*code)));
                outline.replaceKey("/A", dic);
            }
        } else if (*action == "Launch") {
            if (const std::string* file = get(map, "File")) {
                QPDFObjectHandle dic = QPDFObjectHandle::newDictionary();
                dic.replaceKey("/S", QPDFObjectHandle::newName("/Launch"));
                dic.replaceKey("/F", QPDFObjectHandle::newString(pdfDocBytes(*file)));
                outline.replaceKey("/A", dic);
            }
        }
    } catch (const std::exception&) {
        // empty on purpose, as in iText
    }
}

struct OutlineRange {
    QPDFObjectHandle first;
    QPDFObjectHandle last;
    int count = 0;
};

// SimpleBookmark.iterateOutlines
OutlineRange iterateOutlines(QPDF& pdf, QPDFObjectHandle parent, const std::vector<OutlineItem>& kids,
                             bool namedAsNames) {
    std::vector<QPDFObjectHandle> refs;
    for (size_t k = 0; k < kids.size(); ++k)
        refs.push_back(pdf.newIndirectNull());
    int count = 0;
    for (size_t ptr = 0; ptr < kids.size(); ++ptr) {
        const OutlineItem& item = kids[ptr];
        QPDFObjectHandle outline = QPDFObjectHandle::newDictionary();
        ++count;
        if (!item.kids.empty()) {
            const OutlineRange lower = iterateOutlines(pdf, refs[ptr], item.kids, namedAsNames);
            outline.replaceKey("/First", lower.first);
            outline.replaceKey("/Last", lower.last);
            const std::string* open = get(item.map, "Open");
            if (open != nullptr && *open == "false") {
                outline.replaceKey("/Count", QPDFObjectHandle::newInteger(-lower.count));
            } else {
                outline.replaceKey("/Count", QPDFObjectHandle::newInteger(lower.count));
                count += lower.count;
            }
        }
        outline.replaceKey("/Parent", parent);
        if (ptr > 0)
            outline.replaceKey("/Prev", refs[ptr - 1]);
        if (ptr < refs.size() - 1)
            outline.replaceKey("/Next", refs[ptr + 1]);
        const std::string* title = get(item.map, "Title");
        outline.replaceKey("/Title", QPDFObjectHandle::newUnicodeString(title ? *title : std::string()));
        if (const std::string* color = get(item.map, "Color")) {
            try {
                const std::vector<std::string> tk = tokens(*color);
                QPDFObjectHandle array = QPDFObjectHandle::newArray();
                for (size_t k = 0; k < 3; ++k) {
                    float f = javaParseFloat(javaTrim(tk.at(k)));
                    f = std::clamp(f, 0.0f, 1.0f);
                    array.appendItem(itextNumberObject(f));
                }
                outline.replaceKey("/C", array);
            } catch (const std::exception&) {
                // in case it is malformed
            }
        }
        if (const std::string* style = get(item.map, "Style")) {
            std::string lower = *style;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            int bits = 0;
            if (lower.find("italic") != std::string::npos)
                bits |= 1;
            if (lower.find("bold") != std::string::npos)
                bits |= 2;
            if (bits != 0)
                outline.replaceKey("/F", QPDFObjectHandle::newInteger(bits));
        }
        createOutlineAction(pdf, outline, item.map, namedAsNames);
        pdf.replaceObject(refs[ptr].getObjGen(), outline);
    }
    return {refs.front(), refs.back(), count};
}

// ---- output settings ----

// PdfTransition.getTransitionDictionary
QPDFObjectHandle transitionDictionary(int type, int duration) {
    static const char* const kStyles[] = {
        "",      "/Split", "/Split", "/Split", "/Split",    "/Blinds",  "/Blinds",  "/Box",    "/Box",
        "/Wipe", "/Wipe",  "/Wipe",  "/Wipe",  "/Dissolve", "/Glitter", "/Glitter", "/Glitter"};
    QPDFObjectHandle trans = QPDFObjectHandle::parse("<< /Type /Trans >>");
    if (type < 1 || type > 16)
        return trans;
    trans.replaceKey("/S", QPDFObjectHandle::newName(kStyles[type]));
    trans.replaceKey("/D", QPDFObjectHandle::newInteger(duration));
    const auto name = [&](const char* key, const char* value) {
        trans.replaceKey(key, QPDFObjectHandle::newName(value));
    };
    const auto direction = [&](int degrees) {
        trans.replaceKey("/Di", QPDFObjectHandle::newInteger(degrees));
    };
    switch (type) {
    case 1: name("/Dm", "/V"), name("/M", "/O"); break;
    case 2: name("/Dm", "/H"), name("/M", "/O"); break;
    case 3: name("/Dm", "/V"), name("/M", "/I"); break;
    case 4: name("/Dm", "/H"), name("/M", "/I"); break;
    case 5: name("/Dm", "/V"); break;
    case 6: name("/Dm", "/H"); break;
    case 7: name("/M", "/I"); break;
    case 8: name("/M", "/O"); break;
    case 9: direction(0); break;
    case 10: direction(180); break;
    case 11: direction(90); break;
    case 12: direction(270); break;
    case 14: direction(0); break;
    case 15: direction(270); break;
    case 16: direction(315); break;
    default: break;
    }
    return trans;
}

// A value as formats::viewerPreferences spells it: true or false, a name, or a number.
QPDFObjectHandle preferenceValue(const std::string& value) {
    if (value == "true" || value == "false")
        return QPDFObjectHandle::newBool(value == "true");
    if (!value.empty() && value[0] == '/')
        return QPDFObjectHandle::newName(value);
    return QPDFObjectHandle::newInteger(javaParseInt(value));
}

// The name tree key of an attachment, as PdfDocument.addFileAttachment makes it: the bytes of the
// text string, read as one character per byte.
std::string attachmentKey(const std::string& description) {
    return QPDFObjectHandle::newUnicodeString(description).getStringValue();
}

void readNameTree(QPDFObjectHandle node, std::map<std::string, QPDFObjectHandle>& items,
                  std::vector<std::string>& order) {
    // PdfNameTree.readTree, which keeps the last value of a repeated name
    if (!node.isDictionary())
        return;
    QPDFObjectHandle names = node.getKey("/Names");
    if (names.isArray()) {
        for (int k = 0; k + 1 < names.getArrayNItems(); k += 2) {
            QPDFObjectHandle key = names.getArrayItem(k);
            if (!key.isString())
                continue;
            const std::string name = key.getStringValue();
            if (items.count(name) == 0)
                order.push_back(name);
            items[name] = names.getArrayItem(k + 1);
        }
    } else if (QPDFObjectHandle kids = node.getKey("/Kids"); kids.isArray()) {
        for (int k = 0; k < kids.getArrayNItems(); ++k)
            readNameTree(kids.getArrayItem(k), items, order);
    }
}

} // namespace

// ---- Tweak ----

void Tweak::addWatermark(const InputFile* wmFile, const std::optional<std::string>& wmText, int wmSize,
                         float wmOpacity, const std::optional<std::array<int, 3>>& wmColor, int pnPosition,
                         bool pnFlipEven, int pnSize, float pnHOff, float pnVOff, const std::string& mask) {
    try {
        QPDF& pdf = *current_;
        const int pagecount = pageCount();
        Stamper stamper(pdf);
        QPDFObjectHandle gs1 = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
        gs1.replaceKey("/ca", itextNumberObject(wmOpacity));
        QPDFObjectHandle font = pdf.makeIndirectObject(
            QPDFObjectHandle::parse("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                                    "/Encoding /WinAnsiEncoding >>"));
        float txtwidth = 0;
        QPDFObjectHandle wmTemplate;
        std::vector<std::string> pageLabels;
        std::vector<formats::PageLabelFormat> pageLabelFormats;
        if (wmText)
            txtwidth = helvetica::widthPoint(*wmText, static_cast<float>(wmSize));
        if (wmFile != nullptr)
            wmTemplate = importPage(pdf, pages(*wmFile->qpdf()).at(0).getObjectHandle());
        if (!mask.empty()) {
            const auto formats = readPageLabels(pdf);
            if (formats) {
                pageLabels = formats::labelStrings(*formats, pagecount);
                pageLabelFormats = *formats;
            } else {
                for (int i = 1; i <= pagecount; ++i)
                    pageLabels.push_back(std::to_string(i));
            }
            if (pageLabelFormats.empty())
                pageLabelFormats.push_back(
                    formats::PageLabelFormat{1, formats::LabelStyle::Decimal, "", 1});
        }
        const std::vector<QPDFPageObjectHelper> all = pages(pdf);
        progress_.stage("Adding watermarks", pagecount);
        for (int i = 1; i <= pagecount; ++i) {
            checkCancelled();
            if (wmTemplate.isInitialized())
                stamper.under(i).addTemplate(wmTemplate, 0, 0);
            Stamper::Content& overContent = stamper.over(i);
            const Rect size = pageSizeWithRotation(all[static_cast<size_t>(i - 1)].getObjectHandle());
            if (wmText) {
                const float angle = static_cast<float>(std::atan(size.height() / size.width()));
                const float m1 = static_cast<float>(std::cos(angle));
                const float m2 = static_cast<float>(-std::sin(angle));
                const float m3 = static_cast<float>(std::sin(angle));
                const float m4 = static_cast<float>(std::cos(angle));
                const float xoff = static_cast<float>(-std::cos(static_cast<double>(angle)) * txtwidth / 2 -
                                                      std::sin(static_cast<double>(angle)) * wmSize / 2);
                const float yoff = static_cast<float>(std::sin(static_cast<double>(angle)) * txtwidth / 2 -
                                                      std::cos(static_cast<double>(angle)) * wmSize / 2);
                overContent.saveState();
                overContent.setGState(gs1);
                overContent.beginText();
                if (wmColor)
                    overContent.setRgbFill((*wmColor)[0], (*wmColor)[1], (*wmColor)[2]);
                overContent.setFontAndSize(font, static_cast<float>(wmSize));
                overContent.setTextMatrix(m1, m2, m3, m4, xoff + size.width() / 2,
                                          yoff + size.height() / 2);
                overContent.showText(*wmText);
                overContent.endText();
                overContent.restoreState();
            }
            if (pnPosition != -1) {
                overContent.beginText();
                overContent.setFontAndSize(font, static_cast<float>(pnSize));
                int pnXPosition = pnPosition % 3;
                if (pnFlipEven && i % 2 == 0)
                    pnXPosition = 2 - pnXPosition;
                const float xx = pnHOff * static_cast<float>(pnXPosition == 2 ? -1 : 1) +
                                 size.width() * static_cast<float>(pnXPosition) / 2.0f;
                const float yy = pnVOff * static_cast<float>(pnPosition / 3 == 2 ? -1 : 1) +
                                 size.height() * static_cast<float>(pnPosition / 3) / 2.0f;
                std::string number = std::to_string(i);
                if (!mask.empty()) {
                    int pagenumber = i;
                    for (const formats::PageLabelFormat& format : pageLabelFormats)
                        if (format.physicalPage <= i)
                            pagenumber = i - format.physicalPage + format.logicalPage;
                    const std::string& pagenumbertext = pageLabels[static_cast<size_t>(i - 1)];
                    const FormatArg args[] = {static_cast<long long>(i), static_cast<long long>(pagecount),
                                              static_cast<long long>(pagenumber), pagenumbertext};
                    number = javaFormat(mask, args);
                }
                const float size2 = static_cast<float>(pnSize);
                if ((pnXPosition != 1 && pnHOff * 2 < helvetica::widthPoint(number, size2)) ||
                    (pnPosition / 3 == 0 && pnVOff < helvetica::descentPoint(number, size2)) ||
                    (pnPosition / 3 == 2 && pnVOff < helvetica::ascentPoint(number, size2)))
                    throw Error(Error::Kind::Io,
                                "Page number " + number + " is not within page bounding box");
                overContent.showTextCentered(number, xx, yy);
                overContent.endText();
            }
            progress_.advance();
        }
        stamper.finish();
        // written and read back while the watermark file is still open
        replaceCurrent(pdf);
    } catch (...) {
        rethrowAsError();
    }
}

void Tweak::updateBookmarks(const std::vector<formats::Bookmark>& bookmarks) {
    try {
        checkCancelled();
        const std::vector<OutlineItem> items = makeBookmarks(bookmarks, 1, 0, bookmarks.size());
        QPDF& pdf = *current_;
        // PdfStamperImp.setOutlines
        QPDFObjectHandle catalog = pdf.getRoot();
        catalog.removeKey("/Outlines");
        if (!items.empty()) {
            const bool namedAsNames = catalog.hasKey("/Dests");
            QPDFObjectHandle top = pdf.newIndirectNull();
            const OutlineRange kids = iterateOutlines(pdf, top, items, namedAsNames);
            QPDFObjectHandle dict = QPDFObjectHandle::newDictionary();
            dict.replaceKey("/First", kids.first);
            dict.replaceKey("/Last", kids.last);
            dict.replaceKey("/Count", QPDFObjectHandle::newInteger(kids.count));
            pdf.replaceObject(top.getObjGen(), dict);
            catalog.replaceKey("/Outlines", top);
        }
        replaceCurrent(pdf);
    } catch (...) {
        rethrowAsError();
    }
}

void Tweak::setPageNumbers(const std::vector<formats::PageLabelFormat>& labelFormats) {
    try {
        checkCancelled();
        // PdfPageLabels, which starts with decimal numbers from page 1
        std::map<int, QPDFObjectHandle> map;
        const auto add = [&](int page, int numberStyle, const std::optional<std::string>& text,
                             int firstPage) {
            if (page < 1 || firstPage < 1)
                throw Error(Error::Kind::Document,
                            "In a page label the page numbers must be greater or equal to 1.");
            static const char* const kStyles[] = {"/D", "/R", "/r", "/A", "/a"};
            QPDFObjectHandle dic = QPDFObjectHandle::newDictionary();
            if (numberStyle >= 0 && numberStyle < 5)
                dic.replaceKey("/S", QPDFObjectHandle::newName(kStyles[numberStyle]));
            if (text)
                dic.replaceKey("/P", QPDFObjectHandle::newUnicodeString(*text));
            if (firstPage != 1)
                dic.replaceKey("/St", QPDFObjectHandle::newInteger(firstPage));
            map[page - 1] = dic;
        };
        add(1, 0, std::nullopt, 1);
        for (const formats::PageLabelFormat& format : labelFormats)
            add(format.physicalPage, static_cast<int>(format.style), format.prefix, format.logicalPage);
        QPDFObjectHandle nums = QPDFObjectHandle::newArray();
        for (const auto& [key, dic] : map) {
            nums.appendItem(QPDFObjectHandle::newInteger(key));
            nums.appendItem(dic);
        }
        QPDFObjectHandle labels = QPDFObjectHandle::newDictionary();
        labels.replaceKey("/Nums", nums);
        QPDF& pdf = *current_;
        pdf.getRoot().replaceKey("/PageLabels", labels);
        replaceCurrent(pdf);
    } catch (...) {
        rethrowAsError();
    }
}

void Tweak::setTransition(int page, int type, int tduration, int pduration) {
    if (transitionValues_.empty())
        transitionValues_.assign(static_cast<size_t>(pageCount()), {0, 0, -1});
    if (page < 1 || page > static_cast<int>(transitionValues_.size()))
        throw Error(Error::Kind::Document, "java.lang.ArrayIndexOutOfBoundsException: Index " +
                                               std::to_string(page - 1) + " out of bounds for length " +
                                               std::to_string(transitionValues_.size()));
    transitionValues_[static_cast<size_t>(page - 1)] = {type, tduration, pduration};
}

void Tweak::setViewerPreferences(int simplePrefs,
                                 const std::vector<std::pair<std::string, std::string>>& optionalPrefs) {
    simpleViewerPreferences_ = simplePrefs;
    optionalViewerPreferences_ = optionalPrefs;
}

void Tweak::applyOutputSettings(QPDF& pdf) const {
    const std::vector<QPDFPageObjectHelper> all = pages(pdf);
    if (!transitionValues_.empty()) {
        // PdfStamperImp.setTransition and setDuration
        for (size_t i = 0; i < all.size() && i < transitionValues_.size(); ++i) {
            QPDFObjectHandle page = all[i].getObjectHandle();
            const auto& [type, tduration, pduration] = transitionValues_[i];
            if (type == 0)
                page.removeKey("/Trans");
            else
                page.replaceKey("/Trans", transitionDictionary(type, tduration));
            if (pduration < 0)
                page.removeKey("/Dur");
            else
                page.replaceKey("/Dur", QPDFObjectHandle::newInteger(pduration));
        }
    }
    QPDFObjectHandle catalog = pdf.getRoot();
    if (optionalViewerPreferences_) {
        // PdfViewerPreferencesImp.addToCatalog
        static const char* const kLayouts[] = {"/SinglePage",     "/OneColumn",   "/TwoColumnLeft",
                                               "/TwoColumnRight", "/TwoPageLeft", "/TwoPageRight"};
        static const char* const kModes[] = {"/UseNone",    "/UseOutlines", "/UseThumbs",
                                             "/FullScreen", "/UseOC",       "/UseAttachments"};
        catalog.removeKey("/PageLayout");
        for (int k = 0; k < 6; ++k) {
            if ((simpleViewerPreferences_ & (1 << k)) != 0) {
                catalog.replaceKey("/PageLayout", QPDFObjectHandle::newName(kLayouts[k]));
                break;
            }
        }
        catalog.removeKey("/PageMode");
        for (int k = 0; k < 6; ++k) {
            if ((simpleViewerPreferences_ & (64 << k)) != 0) {
                catalog.replaceKey("/PageMode", QPDFObjectHandle::newName(kModes[k]));
                break;
            }
        }
        catalog.removeKey("/ViewerPreferences");
        if (!optionalViewerPreferences_->empty()) {
            QPDFObjectHandle preferences = QPDFObjectHandle::newDictionary();
            for (const auto& [key, value] : *optionalViewerPreferences_)
                preferences.replaceKey(key, preferenceValue(value));
            catalog.replaceKey("/ViewerPreferences", preferences);
        }
    }
    if (!attachments_.empty()) {
        // PdfFileSpecification.fileEmbedded and PdfDocument.addFileAttachment
        std::map<std::string, QPDFObjectHandle> added;
        std::vector<std::string> addedOrder;
        for (const std::filesystem::path& file : attachments_) {
            const std::filesystem::path absolute = std::filesystem::absolute(file);
            std::ifstream in(absolute, std::ios::binary);
            if (!in)
                throw Error(Error::Kind::Io, utf8Path(absolute) + " not found as file or resource.");
            const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const std::string name = toUtf8(file.filename().wstring());
            QPDFObjectHandle params = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
            params.replaceKey("/ModDate", QPDFObjectHandle::newString(pdfDateNow()));
            params.replaceKey("/Size", QPDFObjectHandle::newInteger(static_cast<long long>(data.size())));
            QPDFObjectHandle stream = pdf.newStream(data);
            stream.getDict().replaceKey("/Type", QPDFObjectHandle::newName("/EmbeddedFile"));
            stream.getDict().replaceKey("/Params", params);
            QPDFObjectHandle ef = QPDFObjectHandle::newDictionary();
            ef.replaceKey("/F", stream);
            ef.replaceKey("/UF", stream);
            QPDFObjectHandle fs = QPDFObjectHandle::newDictionary();
            fs.replaceKey("/Type", QPDFObjectHandle::newName("/Filespec"));
            fs.replaceKey("/F", QPDFObjectHandle::newString(pdfDocBytes(name)));
            fs.replaceKey("/UF", QPDFObjectHandle::newString(pdfDocBytes(name)));
            fs.replaceKey("/EF", ef);
            fs.replaceKey("/Desc", QPDFObjectHandle::newUnicodeString(name));
            const std::string description = name.empty() ? "Unnamed" : name;
            std::string key = attachmentKey(description);
            for (int k = 1; added.count(key) != 0; ++k)
                key = attachmentKey(description + " " + std::to_string(k));
            added.emplace(key, pdf.makeIndirectObject(fs));
            addedOrder.push_back(key);
        }
        // PdfStamperImp.addFileAttachments: the new names join the existing tree, made unique
        QPDFObjectHandle names = catalog.getKey("/Names");
        if (!names.isDictionary()) {
            names = QPDFObjectHandle::newDictionary();
            catalog.replaceKey("/Names", names);
        }
        std::map<std::string, QPDFObjectHandle> old;
        std::vector<std::string> oldOrder;
        readNameTree(names.getKey("/EmbeddedFiles"), old, oldOrder);
        std::vector<std::string> utf16Keys;
        for (const std::string& key : addedOrder)
            utf16Keys.push_back(latin1ToUtf8(key));
        for (const size_t i : javaHashMapOrder(utf16Keys)) {
            std::string nn = addedOrder[i];
            for (int k = 1; old.count(nn) != 0; ++k)
                nn += " " + std::to_string(k);
            old.emplace(nn, added.at(addedOrder[i]));
        }
        // PdfNameTree.writeTree, with every name in one leaf
        QPDFObjectHandle array = QPDFObjectHandle::newArray();
        for (const auto& [key, value] : old) {
            array.appendItem(QPDFObjectHandle::newString(key));
            array.appendItem(value);
        }
        QPDFObjectHandle tree = QPDFObjectHandle::newDictionary();
        tree.replaceKey("/Names", array);
        names.replaceKey("/EmbeddedFiles", pdf.makeIndirectObject(tree));
    }
}

} // namespace pdftweak::pdf
