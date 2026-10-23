#include "core/formats/xmp.h"

#include <algorithm>
#include <regex>
#include <vector>

namespace pdftweak::formats {
namespace {

const char kPdfNamespace[] = "http://ns.adobe.com/pdf/1.3/";
const char kXmpNamespace[] = "http://ns.adobe.com/xap/1.0/";

bool validUtf8(std::string_view text) {
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char b = static_cast<unsigned char>(text[i]);
        size_t length = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 0;
        if (length == 0 || i + length > text.size())
            return false;
        for (size_t k = 1; k < length; ++k)
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80)
                return false;
        i += length;
    }
    return true;
}

std::string escape(const std::string& value, char quote) {
    std::string text;
    for (const char c : value) {
        switch (c) {
        case '&': text += "&amp;"; break;
        case '<': text += "&lt;"; break;
        case '>': text += "&gt;"; break;
        case '"': text += quote == '"' ? "&quot;" : "\""; break;
        case '\'': text += quote == '\'' ? "&apos;" : "'"; break;
        default: text += c;
        }
    }
    return text;
}

bool isNameChar(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-' || c == '.' || c == ':' || static_cast<unsigned char>(c) >= 0x80;
}

struct Edit {
    size_t at;
    size_t length;
    std::string text;
};

// Collects the edits that set one property to value. Returns false when the property appears in a
// form other than a simple element or attribute.
bool collect(const std::string& packet, const std::string& name, const std::string& value,
             std::vector<Edit>& edits) {
    // element form
    const std::string open = "<" + name;
    for (size_t at = packet.find(open); at != std::string::npos; at = packet.find(open, at + 1)) {
        const size_t after = at + open.size();
        if (after < packet.size() && isNameChar(packet[after]))
            continue; // a longer name that starts with this one
        const size_t close = packet.find('>', after);
        if (close == std::string::npos)
            return false;
        for (size_t k = after; k < close; ++k)
            if (packet[k] != ' ' && packet[k] != '\t' && packet[k] != '\r' && packet[k] != '\n')
                return false; // attributes, such as rdf:parseType, or an empty element
        const size_t textStart = close + 1;
        const size_t end = packet.find("</" + name, textStart);
        if (end == std::string::npos)
            return false;
        if (packet.find('<', textStart) != end)
            return false; // nested content, such as rdf:Alt
        edits.push_back({textStart, end - textStart, escape(value, 0)});
    }
    // attribute form
    for (size_t at = packet.find(name); at != std::string::npos; at = packet.find(name, at + 1)) {
        if (at == 0 || !(packet[at - 1] == ' ' || packet[at - 1] == '\t' || packet[at - 1] == '\r' ||
                         packet[at - 1] == '\n'))
            continue;
        size_t k = at + name.size();
        if (k < packet.size() && isNameChar(packet[k]))
            continue;
        while (k < packet.size() &&
               (packet[k] == ' ' || packet[k] == '\t' || packet[k] == '\r' || packet[k] == '\n'))
            ++k;
        if (k >= packet.size() || packet[k] != '=')
            continue;
        ++k;
        while (k < packet.size() &&
               (packet[k] == ' ' || packet[k] == '\t' || packet[k] == '\r' || packet[k] == '\n'))
            ++k;
        if (k >= packet.size() || (packet[k] != '"' && packet[k] != '\''))
            return false;
        const char quote = packet[k];
        const size_t end = packet.find(quote, k + 1);
        if (end == std::string::npos)
            return false;
        edits.push_back({k + 1, end - k - 1, escape(value, quote)});
    }
    return true;
}

} // namespace

std::optional<std::string> updateXmpPacket(std::string_view text,
                                           const std::optional<std::string>& producer,
                                           const std::string& date) {
    if (!validUtf8(text))
        return std::nullopt;
    const std::string packet(text);
    std::vector<std::string> pdfPrefixes, xmpPrefixes;
    static const std::regex declaration(R"(xmlns:([A-Za-z_][A-Za-z0-9_.\-]*)\s*=\s*(["'])([^"']*)\2)");
    for (auto m = std::sregex_iterator(packet.begin(), packet.end(), declaration);
         m != std::sregex_iterator(); ++m) {
        if ((*m)[3] == kPdfNamespace)
            pdfPrefixes.push_back((*m)[1]);
        else if ((*m)[3] == kXmpNamespace)
            xmpPrefixes.push_back((*m)[1]);
    }
    std::vector<Edit> edits;
    for (const std::string& prefix : pdfPrefixes)
        if (producer && !collect(packet, prefix + ":Producer", *producer, edits))
            return std::nullopt;
    for (const std::string& prefix : xmpPrefixes) {
        if (!collect(packet, prefix + ":ModifyDate", date, edits) ||
            !collect(packet, prefix + ":MetadataDate", date, edits))
            return std::nullopt;
    }
    std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.at > b.at; });
    std::string result = packet;
    for (const Edit& e : edits)
        result.replace(e.at, e.length, e.text);
    return result;
}

} // namespace pdftweak::formats
