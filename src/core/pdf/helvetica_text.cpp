#include "core/pdf/encodings.h"
#include "core/pdf/helvetica.h"

#include "core/text.h"

#include <algorithm>

namespace pdftweak::pdf::helvetica {
namespace {

// BaseFont.getCharBBox for each character of the text: the box of its single WinAnsi byte, or
// none when the character has no byte or the glyph no box.
template <typename F> void forEachBox(std::string_view utf8, F f) {
    for (const char32_t c : codePoints(utf8)) {
        const std::string byte = winAnsiBytes(fromCodePoints(std::u32string(1, c)));
        if (byte.empty())
            continue;
        const GlyphMetrics& m = kMetrics[static_cast<unsigned char>(byte[0])];
        if (m.hasBox)
            f(m);
    }
}

} // namespace

float widthPoint(std::string_view utf8, float size) {
    int total = 0;
    for (const char byte : winAnsiBytes(utf8))
        total += kMetrics[static_cast<unsigned char>(byte)].width;
    return static_cast<float>(total) * 0.001f * size;
}

float ascentPoint(std::string_view utf8, float size) {
    int max = 0;
    forEachBox(utf8, [&](const GlyphMetrics& m) { max = std::max(max, m.ury); });
    return static_cast<float>(max) * 0.001f * size;
}

float descentPoint(std::string_view utf8, float size) {
    int min = 0;
    forEachBox(utf8, [&](const GlyphMetrics& m) { min = std::min(min, m.lly); });
    return static_cast<float>(min) * 0.001f * size;
}

} // namespace pdftweak::pdf::helvetica
