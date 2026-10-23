#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace pdftweak::formats {

// The numbering styles of iText's PdfPageLabels, in the order of the letters "xIiAa-" that the
// command line uses for them.
enum class LabelStyle {
    Decimal = 0,
    UpperRoman = 1,
    LowerRoman = 2,
    UpperLetters = 3,
    LowerLetters = 4,
    Empty = 5
};

// PdfPageLabelFormat: from physical page physicalPage on, pages are labelled prefix followed by
// logicalPage, logicalPage + 1, and so on, in the given style.
struct PageLabelFormat {
    int physicalPage = 1;
    LabelStyle style = LabelStyle::Decimal;
    std::string prefix;
    int logicalPage = 1;
};

// PageNumberOption.parsePageLabel, the syntax "physical=[prefix:]logical[style]", where the style is
// one of a, A, i, I, or - and decimal when absent. Throws Error(Document, "Cannot parse page label: "
// followed by the text) for anything else.
PageLabelFormat parsePageLabel(std::string_view text);

// The inverse of parsePageLabel, as the PAGELABELS lines of the -info dump write it.
std::string formatPageLabel(const PageLabelFormat& format);

// One page number in a style. Roman numerals follow iText's RomanNumberFactory, which writes numbers
// above 3000 as |thousands|rest. Letters follow ISO 32000, where 27 is AA and 53 is AAA. Letters
// throw Error(Document) below 1, as iText's RomanAlphabetFactory does.
std::string labelNumber(LabelStyle style, int number);

// PdfPageLabels.getPageLabels: the label of each of pageCount pages under the formats, which act as
// the entries of a page label number tree. Before the first entry, pages are numbered 1, 2, and so
// on without a prefix, and a later entry for the same physical page replaces an earlier one.
std::vector<std::string> labelStrings(const std::vector<PageLabelFormat>& formats, int pageCount);

} // namespace pdftweak::formats
