#pragma once

// The metrics of the non-embedded Helvetica in WinAnsiEncoding that watermarks and page numbers use,
// as iText's BaseFont.createFont("Helvetica", BaseFont.WINANSI, false) reports them.

#include <string_view>

namespace pdftweak::pdf::helvetica {

struct GlyphMetrics {
    int width; // in thousandths of the font size
    bool hasBox;
    int llx, lly, urx, ury;
};

// Generated from Adobe's Helvetica.afm by tools/gen-helvetica.ps1.
extern const GlyphMetrics kMetrics[256];

// BaseFont.getWidthPoint, getAscentPoint, and getDescentPoint for UTF-8 text at the font size. The
// width adds the widths of the WinAnsi bytes of the text. The ascent and descent are the highest top
// and the lowest bottom of the glyph boxes, and never below or above 0.
float widthPoint(std::string_view utf8, float size);
float ascentPoint(std::string_view utf8, float size);
float descentPoint(std::string_view utf8, float size);

} // namespace pdftweak::pdf::helvetica
