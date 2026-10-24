// Unit tests of the Java-compatible text helpers and the data formats, each pinned to what the Java
// version (or iText, for page sizes and Roman numerals) produces.

#include "runner.h"

#include "core/error.h"
#include "core/formats/bookmark.h"
#include "core/formats/integer_list.h"
#include "core/formats/page_dimension.h"
#include "core/formats/page_labels.h"
#include "core/formats/page_range.h"
#include "core/formats/shuffle_rule.h"
#include "core/formats/tables.h"
#include "core/formats/viewer_preferences.h"
#include "core/formats/xmp.h"
#include "core/java_text.h"
#include "core/text.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace pdftweak;
using namespace pdftweak::formats;

namespace {

// The message of the Error that action throws, or "no error".
template <typename F> std::string errorOf(F action) {
    try {
        action();
    } catch (const Error& e) {
        return e.what();
    }
    return "no error";
}

std::string join(const std::vector<int>& values) {
    std::string s = "[";
    for (size_t i = 0; i < values.size(); ++i)
        s += (i ? ", " : "") + std::to_string(values[i]);
    return s + "]";
}

std::string join(const std::vector<std::string>& values) {
    std::string s = "[";
    for (size_t i = 0; i < values.size(); ++i)
        s += (i ? ", " : "") + values[i];
    return s + "]";
}

} // namespace

TEST(unit_textConvertsBetweenEncodings) {
    CHECK_EQUAL(toUtf8(L"Report über"), std::string("Report \xc3\xbc"
                                                    "ber"));
    CHECK(fromUtf8("Report \xc3\xbc"
                   "ber") == L"Report über");
    // Java's getBytes("ISO-8859-1") replaces each character outside Latin-1 with one '?'
    CHECK_EQUAL(toLatin1("\xc3\xbc\xe2\x82\xac\xf0\x9f\x98\x80x"), std::string("\xfc??x"));
}

TEST(unit_javaFloatPrintsLikeJava) {
    CHECK_EQUAL(javaFloat(595.0f), std::string("595.0"));
    CHECK_EQUAL(javaFloat(842.25f), std::string("842.25"));
    CHECK_EQUAL(javaFloat(242.65f), std::string("242.65"));
    CHECK_EQUAL(javaFloat(0.0f), std::string("0.0"));
    CHECK_EQUAL(javaFloat(-0.0f), std::string("-0.0"));
    CHECK_EQUAL(javaFloat(1e-5f), std::string("1.0E-5"));
    CHECK_EQUAL(javaFloat(1e7f), std::string("1.0E7"));
    CHECK_EQUAL(javaFloat(9999999.0f), std::string("9999999.0"));
    CHECK_EQUAL(javaFloat(0.001f), std::string("0.001"));
    CHECK_EQUAL(javaDouble(0.707), std::string("0.707"));
    CHECK_EQUAL(javaDouble(1.0), std::string("1.0"));
    CHECK_EQUAL(javaDouble(-200.0), std::string("-200.0"));
    CHECK_EQUAL(javaDouble(0.17675), std::string("0.17675"));
    CHECK_EQUAL(javaDouble(0.1 + 0.2), std::string("0.30000000000000004"));
    CHECK_EQUAL(javaDouble(1.5e-4), std::string("1.5E-4"));
    CHECK_EQUAL(javaDouble(2e23), std::string("2.0E23"));
}

namespace {

std::string parseError(double (*parse)(std::string_view), std::string_view text) {
    try {
        parse(text);
    } catch (const Error& error) {
        return error.what();
    }
    return "no error";
}

double parseFloatAsDouble(std::string_view text) { return javaParseFloat(text); }

} // namespace

// Double.parseDouble and Float.parseFloat: Java's grammar of floating-point literals, with NaN and
// Infinity spelt as Java spells them, the f and d suffixes, hexadecimal significands, overflow to
// infinity, and floats rounded once from the decimal.
TEST(unit_javaParseDoubleFollowsJava) {
    CHECK_EQUAL(javaParseDouble(" 1e3 "), 1000.0);
    CHECK_EQUAL(javaParseDouble(".5"), 0.5);
    CHECK_EQUAL(javaParseDouble("1."), 1.0);
    CHECK_EQUAL(javaParseDouble("+2"), 2.0);
    CHECK_EQUAL(javaParseDouble("1.5f"), 1.5);
    CHECK_EQUAL(javaParseDouble("2D"), 2.0);
    CHECK_EQUAL(javaParseDouble("0x1p4"), 16.0);
    CHECK_EQUAL(javaParseDouble("-0X1.8P1d"), -3.0);
    CHECK(std::isnan(javaParseDouble("NaN")) && std::isnan(javaParseDouble("-NaN")));
    CHECK_EQUAL(javaParseDouble("-Infinity"), -INFINITY);
    CHECK_EQUAL(javaParseDouble("1e400"), INFINITY);
    CHECK_EQUAL(javaParseDouble("-1e-400"), 0.0);
    CHECK(std::signbit(javaParseDouble("-1e-400")));
    for (const char* text :
         {"inf", "nan", "infinity", "Infinityf", "1e", "e5", "1.5ff", "0x10", "0x", "1 2", "."})
        CHECK_EQUAL(parseError(javaParseDouble, text), "For input string: \"" + std::string(text) + "\"");
    CHECK_EQUAL(parseError(javaParseDouble, "  "), std::string("empty String"));
    CHECK_EQUAL(parseError(parseFloatAsDouble, "x"), std::string("For input string: \"x\""));
    CHECK_EQUAL(javaParseFloat("0.1"), 0.1f);
    CHECK_EQUAL(javaParseFloat("1e39"), INFINITY);
    // just below the midpoint of two floats whose upper one is even, where rounding through a double
    // would reach the midpoint and then round up
    CHECK_EQUAL(javaParseFloat("1.0000001788139343261718749"), std::nextafter(1.0f, 2.0f));
}

TEST(unit_javaFormatSubset) {
    const std::vector<FormatArg> args = {3LL, 7LL, 2LL, std::string("ii")};
    CHECK_EQUAL(javaFormat("%1$d/%2$d", args), std::string("3/7"));
    CHECK_EQUAL(javaFormat("Page %1$d of %2$d", args), std::string("Page 3 of 7"));
    CHECK_EQUAL(javaFormat("%4$s (%3$d)", args), std::string("ii (2)"));
    CHECK_EQUAL(javaFormat("%d-%d", args), std::string("3-7"));
    CHECK_EQUAL(javaFormat("%1$03d", args), std::string("003"));
    CHECK_EQUAL(javaFormat("%1$-4d|", args), std::string("3   |"));
    CHECK_EQUAL(javaFormat("%1$4d|", args), std::string("   3|"));
    CHECK_EQUAL(javaFormat("%1$+d", args), std::string("+3"));
    CHECK_EQUAL(javaFormat("%,d", std::vector<FormatArg>{12345LL}), std::string("12,345"));
    CHECK_EQUAL(javaFormat("%x %X %o", std::vector<FormatArg>{255LL, 255LL, 8LL}), std::string("ff FF 10"));
    CHECK_EQUAL(javaFormat("%#x", std::vector<FormatArg>{255LL}), std::string("0xff"));
    CHECK_EQUAL(javaFormat("100%%", args), std::string("100%"));
    CHECK_EQUAL(javaFormat("%4$5s|%4$-5s|%4$.1s", args), std::string("   ii|ii   |i"));
    CHECK_EQUAL(javaFormat("%4$S %1$s", args), std::string("II 3"));
    CHECK_EQUAL(javaFormat("%1$d %<d", args), std::string("3 3"));
    CHECK_EQUAL(javaFormat("a%nb", args), std::string("a\r\nb"));
    CHECK_EQUAL(errorOf([&] { javaFormat("%q", args); }),
                std::string("java.util.UnknownFormatConversionException: Conversion = 'q'"));
    CHECK_EQUAL(errorOf([&] { javaFormat("%5$d", args); }),
                std::string("java.util.MissingFormatArgumentException: Format specifier '%5$d'"));
    CHECK_EQUAL(errorOf([&] { javaFormat("%4$d", args); }),
                std::string("java.util.IllegalFormatConversionException: d != java.lang.String"));
    CHECK_EQUAL(errorOf([&] { javaFormat("%-d", args); }),
                std::string("java.util.MissingFormatWidthException: %-d"));
    CHECK_EQUAL(errorOf([&] { javaFormat("%1$.2d", args); }),
                std::string("java.util.IllegalFormatPrecisionException: 2"));
    CHECK_EQUAL(errorOf([&] { javaFormat("abc%", args); }),
                std::string("java.util.UnknownFormatConversionException: Conversion = '%'"));
}

TEST(unit_integerListParsesCommas) {
    CHECK_EQUAL(join(parseIntegerList("0")), std::string("[0]"));
    CHECK_EQUAL(join(parseIntegerList("5,6")), std::string("[5, 6]"));
    CHECK_EQUAL(formatIntegerList({5, 6}), std::string("5,6"));
    CHECK_EQUAL(errorOf([] { parseIntegerList("a"); }), std::string("For input string: \"a\""));
    CHECK_EQUAL(errorOf([] { parseIntegerList(""); }), std::string("For input string: \"\""));
    CHECK_EQUAL(errorOf([] { parseIntegerList("1,"); }), std::string("For input string: \"\""));
    CHECK_EQUAL(errorOf([] { parseIntegerList("-1"); }), std::string("Value may not be negative: -1"));
}

TEST(unit_pageRangesSelectReverseAndPad) {
    // The cases of OperationTests.main_pageRangesSelectReverseAndPad on a six-page file, with
    // from and to as the -i option gives them (negative counts from the end).
    struct Case {
        int from, to;
        bool odd, even;
        std::vector<int> empty;
        std::string expected;
    };
    const Case cases[] = {
        {3, 4, true, true, {0}, "[3, 4]"},
        {-2, -2, true, true, {0}, "[5]"},
        {5, -1, true, true, {0}, "[5, 6]"},
        {4, 2, true, true, {0}, "[4, 3, 2]"},
        {1, -1, false, true, {0}, "[2, 4, 6]"},
        {1, -1, true, false, {0}, "[1, 3, 5]"},
        {1, -1, true, true, {2}, "[-1, -1, 1, 2, 3, 4, 5, 6]"},
        {1, -3, true, false, {1}, "[-1, 1, 3]"},
    };
    for (const Case& c : cases) {
        const PageRange range = makePageRange(0, 6, c.from, c.to, c.odd, c.even, c.empty);
        CHECK_EQUAL(join(range.pages(0)), c.expected);
    }
    // The count of empty pages is chosen by the parity of the pages before the range
    const PageRange padded = makePageRange(0, 6, 1, 1, true, true, {5, 6});
    CHECK_EQUAL(padded.pages(0).size(), size_t(6));
    CHECK_EQUAL(padded.pages(1).size(), size_t(7));
}

TEST(unit_commonSizesMatchItext) {
    const auto& sizes = commonSizes();
    CHECK_EQUAL(sizes.size(), size_t(100));
    CHECK_EQUAL(sizes[0].name, std::string("A4 Portrait"));
    CHECK_EQUAL(sizes[0].width, 595.0f);
    CHECK_EQUAL(sizes[0].height, 842.0f);
    CHECK_EQUAL(sizes[3].name, std::string("Letter Landscape"));
    CHECK_EQUAL(sizes[3].width, 792.0f);
    CHECK_EQUAL(sizes[3].height, 612.0f);
    CHECK_EQUAL(sizes[95].name, std::string("Penguin large paperback Landscape"));
    CHECK_EQUAL(sizes[96].name, std::string("Ledger"));
    CHECK_EQUAL(sizes[97].name, std::string("ISO 7810 ID-1"));
    CHECK_EQUAL(sizes[97].width, 242.65f);
    CHECK_EQUAL(sizes[99].name, std::string("ISO 7810 ID-3"));
}

TEST(unit_shuffleRulesRoundTrip) {
    // Java's toString writes the scale and offsets as Double.toString does.
    const RuleSet twoUp = parseRuleSet("2:!+1L0.707+0%-100%,+2L.707+100%-100%F2");
    CHECK_EQUAL(twoUp.passLength, 2);
    CHECK_EQUAL(twoUp.blockSize, 0);
    CHECK_EQUAL(twoUp.rules.size(), size_t(2));
    CHECK_EQUAL(twoUp.rules[0].toString(), std::string("!+1L0.707+0.0%-100.0%"));
    CHECK_EQUAL(twoUp.rules[1].toString(), std::string("+2L0.707+100.0%-100.0%F2.0"));
    CHECK_EQUAL(twoUp.rules[1].rotateAngle(), 270);
    const RuleSet block = parseRuleSet("-4,4:!-4N1+0%+0%,!+1N1+0%+0%");
    CHECK_EQUAL(block.passLength, -4);
    CHECK_EQUAL(block.blockSize, 4);
    CHECK_EQUAL(block.rules[0].pageString(), std::string("-4"));
    CHECK(block.rules[0].base == ShuffleRule::Base::End);
    const RuleSet absolute = parseRuleSet("1:!1N1+0+50%");
    CHECK(absolute.rules[0].base == ShuffleRule::Base::Absolute);
    CHECK(!absolute.rules[0].offsetXPercent && absolute.rules[0].offsetYPercent);
    CHECK_EQUAL(presetRuleSets().size(), size_t(21));
    for (const auto& [name, rules] : presetRuleSets())
        CHECK(!parseRuleSet(rules).rules.empty());
    CHECK_EQUAL(presetRuleSets()[4].first, std::string("4-up"));
    CHECK_EQUAL(errorOf([] { parseRuleSet("1!+1N1+0+0"); }), std::string("No colon found"));
    CHECK_EQUAL(errorOf([] { parseRuleSet("1:+1N1+0+0"); }),
                std::string("First rule must have new page before"));
    CHECK_EQUAL(errorOf([] { parseRuleSet("1:!+1X1+0+0"); }), std::string("!+1X1+0+0"));
    CHECK_EQUAL(errorOf([] { parseRuleSet("x:!+1N1+0+0"); }), std::string("For input string: \"x\""));
}

TEST(unit_bookmarkLinesRoundTrip) {
    const char* lines[] = {
        "1;O;Part;1 Fit", "2;OBI;Chapter \\3B one;2 XYZ 0 800 0", "1;O;Chapter;1;B=\\221\\22 A=\\222\\22",
        "1;;No page;0",   "-1;I;Tab\\09 and quote \\27;3",
    };
    for (const char* line : lines)
        CHECK_EQUAL(parseBookmark(line).toString(), std::string(line));
    const Bookmark chapter = parseBookmark("2;BI;Chapter \\3B one;2 XYZ 0 800 0");
    CHECK_EQUAL(chapter.title, std::string("Chapter ; one"));
    CHECK_EQUAL(chapter.pagePosition, std::string("XYZ 0 800 0"));
    CHECK(chapter.bold && chapter.italic && !chapter.open);
    const Bookmark options = parseBookmark("1;O;Chapter;1;B=\\221\\22 A=\\222\\22");
    CHECK_EQUAL(options.more.size(), size_t(2));
    CHECK_EQUAL(options.more[0].first, std::string("B"));
    CHECK_EQUAL(options.more[1].second, std::string("2"));
    CHECK_EQUAL(makeOptionString({{"URI", "say \"hi\""}}), std::string("URI=\"say \"\"hi\"\"\""));
    CHECK_EQUAL(parseOptionString("URI=\"say \"\"hi\"\"\"")[0].second, std::string("say \"hi\""));
    CHECK_EQUAL(errorOf([] { parseBookmark("x;O;T;1"); }), std::string("Cannot parse bookmark"));
    CHECK_EQUAL(errorOf([] { parseBookmark("1;O;T;1;Bad"); }),
                std::string("Invalid option (no equals sign): Bad"));
}

TEST(unit_pageLabelStrings) {
    const std::vector<PageLabelFormat> formats = {parsePageLabel("1=Intro:1i"), parsePageLabel("3=1")};
    CHECK_EQUAL(formats[0].prefix, std::string("Intro"));
    CHECK(formats[0].style == LabelStyle::LowerRoman);
    CHECK_EQUAL(join(labelStrings(formats, 4)), std::string("[Introi, Introii, 1, 2]"));
    CHECK_EQUAL(join(labelStrings({parsePageLabel("1=1A"), parsePageLabel("2=5")}, 4)),
                std::string("[A, 5, 6, 7]"));
    CHECK_EQUAL(join(labelStrings({parsePageLabel("1=Appendix:100-")}, 2)),
                std::string("[Appendix, Appendix]"));
    CHECK_EQUAL(parsePageLabel("20=Chapter:with:colon:1I").prefix, std::string("Chapter:with:colon"));
    // ISO 32000 letters continue after Z with AA, BB, and so on, where iText wrote AA, AB
    CHECK_EQUAL(labelNumber(LabelStyle::UpperLetters, 26), std::string("Z"));
    CHECK_EQUAL(labelNumber(LabelStyle::UpperLetters, 27), std::string("AA"));
    CHECK_EQUAL(labelNumber(LabelStyle::LowerLetters, 28), std::string("bb"));
    CHECK_EQUAL(labelNumber(LabelStyle::LowerLetters, 53), std::string("aaa"));
    // Roman numerals as iText's RomanNumberFactory writes them
    CHECK_EQUAL(labelNumber(LabelStyle::UpperRoman, 1994), std::string("MCMXCIV"));
    CHECK_EQUAL(labelNumber(LabelStyle::LowerRoman, 4), std::string("iv"));
    CHECK_EQUAL(labelNumber(LabelStyle::LowerRoman, 3000), std::string("mmm"));
    CHECK_EQUAL(labelNumber(LabelStyle::UpperRoman, 4001), std::string("|IV|I"));
    CHECK_EQUAL(labelNumber(LabelStyle::Decimal, 42), std::string("42"));
    CHECK_EQUAL(labelNumber(LabelStyle::Empty, 42), std::string(""));
    CHECK_EQUAL(errorOf([] { parsePageLabel("x=1"); }), std::string("Cannot parse page label: x=1"));
}

TEST(unit_tablesAndViewerPreferences) {
    CHECK_EQUAL(transitionNames().size(), size_t(17));
    CHECK_EQUAL(transitionNames()[6], std::string("Horizontal Blinds"));
    CHECK_EQUAL(permissionNames()[7], std::string("HQPrinting"));
    CHECK_EQUAL(permissionBits()[7], 2048);
    CHECK_EQUAL(infoNames().size(), size_t(8));
    CHECK_EQUAL(infoNames()[7], std::string("ModDate"));
    CHECK_EQUAL(pageModeNames()[1], std::string("Outline"));
    CHECK_EQUAL(pageLayoutNames()[2], std::string("Two Columns Left"));
    const auto& prefs = viewerPreferences();
    CHECK_EQUAL(prefs.size(), size_t(16));
    CHECK_EQUAL(prefs[5].name, std::string("Display document title"));
    CHECK_EQUAL(prefs[5].key, std::string("/DisplayDocTitle"));
    CHECK_EQUAL(prefs[7].values[1], std::string("/R2L"));
    CHECK_EQUAL(prefs[15].values[1], std::string("2"));
}

namespace {

std::string decodedHex(const std::string& hex) {
    std::string bytes;
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        bytes += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
    std::string codes;
    for (const char32_t c : codePoints(javaUtf8Decode(bytes))) {
        char buffer[8];
        std::snprintf(buffer, sizeof buffer, "%04X ", static_cast<unsigned>(c));
        codes += buffer;
    }
    return codes;
}

std::string hashOrder(const std::vector<std::string>& keys) {
    std::string text;
    for (const size_t i : javaHashMapOrder(keys))
        text += (text.empty() ? "" : ",") + keys[i];
    return text;
}

} // namespace

TEST(unit_javaUtf8DecodeMatchesJava) {
    // Each expectation but the last is what new String(bytes) printed on OpenJDK 27.0.1. The last
    // is a valid four-byte sequence.
    CHECK_EQUAL(decodedHex("63616665E9"), std::string("0063 0061 0066 0065 FFFD "));
    CHECK_EQUAL(decodedHex("636166E965"), std::string("0063 0061 0066 FFFD 0065 "));
    CHECK_EQUAL(decodedHex("80"), std::string("FFFD "));
    CHECK_EQUAL(decodedHex("C3A9"), std::string("00E9 "));
    CHECK_EQUAL(decodedHex("E282"), std::string("FFFD "));
    CHECK_EQUAL(decodedHex("E28241"), std::string("FFFD 0041 "));
    CHECK_EQUAL(decodedHex("E241"), std::string("FFFD 0041 "));
    CHECK_EQUAL(decodedHex("F0908041"), std::string("FFFD 0041 "));
    CHECK_EQUAL(decodedHex("F09080"), std::string("FFFD "));
    CHECK_EQUAL(decodedHex("C0AF"), std::string("FFFD FFFD "));
    CHECK_EQUAL(decodedHex("EDA080"), std::string("FFFD "));
    CHECK_EQUAL(decodedHex("F4908080"), std::string("FFFD FFFD FFFD FFFD "));
    CHECK_EQUAL(decodedHex("E08080"), std::string("FFFD FFFD FFFD "));
    CHECK_EQUAL(decodedHex("FF"), std::string("FFFD "));
    CHECK_EQUAL(decodedHex("C341"), std::string("FFFD 0041 "));
    CHECK_EQUAL(decodedHex("F08041"), std::string("FFFD FFFD 0041 "));
    CHECK_EQUAL(decodedHex("E2"), std::string("FFFD "));
    CHECK_EQUAL(decodedHex("F0"), std::string("FFFD "));
    CHECK_EQUAL(decodedHex("F09041"), std::string("FFFD 0041 "));
    CHECK_EQUAL(decodedHex("F09F9880"), std::string("1F600 "));
}

TEST(unit_javaHashMapOrderMatchesJava) {
    // Each expectation is the keySet order a java.util.HashMap printed on OpenJDK 27.0.1.
    CHECK_EQUAL(javaStringHash("\u00DCber"), 6651443);
    CHECK_EQUAL(javaStringHash("\u65E5\u672C"), 835047);
    CHECK_EQUAL(javaStringHash("\U0001F600"), 1772899);
    CHECK_EQUAL(hashOrder({"Producer", "CreationDate", "ModDate", "Title", "Author"}),
                std::string("CreationDate,Producer,Title,Author,ModDate"));
    CHECK_EQUAL(hashOrder({"Author", "Title", "ModDate", "Creator", "Producer"}),
                std::string("Producer,Author,Title,ModDate,Creator"));
    CHECK_EQUAL(hashOrder({"B", "A"}), std::string("A,B"));
    CHECK_EQUAL(hashOrder({"Title", "Subject", "Keywords", "Author", "Creator", "Producer", "CreationDate",
                           "ModDate", "Trapped", "Custom1", "Custom2", "Custom3", "Custom4", "\u00DCber",
                           "\u65E5\u672C"}),
                std::string("CreationDate,Keywords,Title,Creator,\u65E5\u672C,Subject,Custom4,Custom3,"
                            "Custom2,Custom1,Producer,\u00DCber,Author,ModDate,Trapped"));
    CHECK_EQUAL(hashOrder({"Color", "Style", "URI", "Action", "Named", "File", "NewWindow", "Rect"}),
                std::string("Named,Action,Rect,NewWindow,Color,Style,URI,File"));
}

TEST(unit_hashMapCopiesKeepJavaOrder) {
    // new HashMap(map) of a SimpleBookmark map, after PdfBookmark.fillInBookmark removes Kids, Open,
    // Style, and Title, as OpenJDK 27.0.1 printed the key sets
    const auto copied = [](const std::vector<std::string>& keys) {
        std::vector<std::string> source;
        for (const size_t i : javaHashMapOrder(keys))
            source.push_back(keys[i]);
        std::string text;
        for (const size_t i : javaHashMapOrder(source, javaHashMapCopyCapacity(source.size()))) {
            const std::string& key = source[i];
            if (key == "Kids" || key == "Open" || key == "Style" || key == "Title")
                continue;
            text += (text.empty() ? "" : ",") + key;
        }
        return text;
    };
    CHECK_EQUAL(copied({"Title", "Color", "Style", "Open", "Named", "Action", "Kids"}),
                std::string("Named,Action,Color"));
    CHECK_EQUAL(copied({"Title", "Color", "Action", "URI"}), std::string("Action,URI,Color"));
    CHECK_EQUAL(copied({"Title", "Page", "Action", "File", "NewWindow"}),
                std::string("Action,Page,File,NewWindow"));
    CHECK_EQUAL(copied({"Title", "Open", "NamedN", "Action", "File", "NewWindow", "Kids"}),
                std::string("Action,NamedN,NewWindow,File"));
    CHECK_EQUAL(copied({"Title", "Color", "Style", "Open", "Action", "Code", "Kids", "X1", "X2", "X3", "X4",
                        "X5", "X6"}),
                std::string("Action,Color,Code,X1,X2,X3,X4,X5,X6"));
}

TEST(unit_itextNumbersMatchByteBuffer) {
    // each pair as iText 5.5.13.6's ByteBuffer.formatDouble printed it
    const std::pair<double, const char*> cases[] = {{0, "0"},
                                                    {0.5, "0.5"},
                                                    {1, "1"},
                                                    {0.05, "0.05"},
                                                    {12.345, "12.35"},
                                                    {0.000014, "0"},
                                                    {0.000016, "0.00002"},
                                                    {100000, "100000"},
                                                    {-2.5, "-2.5"},
                                                    {0.999999, "1"},
                                                    {32767.5, "32768"},
                                                    {32767, "32767"},
                                                    {0.12345, "0.12345"},
                                                    {0.123456, "0.12346"},
                                                    {595.2756, "595.28"},
                                                    {-0.3333, "-0.3333"},
                                                    {1.005, "1"},
                                                    {2.675, "2.68"},
                                                    {0.1f, "0.1"},
                                                    {0.7f, "0.7"},
                                                    {1e-7, "0"},
                                                    {-32768.4, "-32768"}};
    for (const auto& [value, expected] : cases)
        CHECK_EQUAL(itextNumber(value), std::string(expected));
}

TEST(unit_xmpPropertiesAreUpdatedInPlace) {
    const std::string date = "2026-10-24T09:30:00+08:00";
    const std::string element =
        "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF "
        "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
        "<rdf:Description rdf:about=\"\" xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\">"
        "<pdf:Producer>Acrobat</pdf:Producer><pdf:Keywords>k</pdf:Keywords></rdf:Description>"
        "<rdf:Description rdf:about=\"\" xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\">"
        "<xmp:ModifyDate>2001-01-01</xmp:ModifyDate><xmp:CreateDate>2001-01-01</xmp:CreateDate>"
        "</rdf:Description></rdf:RDF></x:xmpmeta>";
    const auto updated = updateXmpPacket(element, std::string("A & B"), date);
    CHECK(updated.has_value());
    CHECK(updated->find("<pdf:Producer>A &amp; B</pdf:Producer>") != std::string::npos);
    CHECK(updated->find("<xmp:ModifyDate>" + date + "</xmp:ModifyDate>") != std::string::npos);
    CHECK(updated->find("<xmp:CreateDate>2001-01-01</xmp:CreateDate>") != std::string::npos);
    CHECK(updated->find("MetadataDate") == std::string::npos); // never added

    // attribute form, with prefixes that are not the usual ones
    const std::string attributes =
        "<rdf:Description rdf:about=\"\" xmlns:p13=\"http://ns.adobe.com/pdf/1.3/\" "
        "xmlns:xap='http://ns.adobe.com/xap/1.0/' p13:Producer=\"Old\" xap:MetadataDate='1999' "
        "xap:ModifyDateX=\"keep\"/>";
    const auto changed = updateXmpPacket(attributes, std::string("Say \"hi\""), date);
    CHECK(changed.has_value());
    CHECK(changed->find("p13:Producer=\"Say &quot;hi&quot;\"") != std::string::npos);
    CHECK(changed->find("xap:MetadataDate='" + date + "'") != std::string::npos);
    CHECK(changed->find("xap:ModifyDateX=\"keep\"") != std::string::npos);

    // Producer left alone when the document information has none
    const auto noProducer = updateXmpPacket(attributes, std::nullopt, date);
    CHECK(noProducer.has_value() && noProducer->find("p13:Producer=\"Old\"") != std::string::npos);

    // a property in another form, and a packet that is not UTF-8, stay as they are
    const std::string nested =
        "<r xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\"><pdf:Producer><rdf:Alt><rdf:li "
        "xml:lang=\"x-default\">P</rdf:li></rdf:Alt></pdf:Producer></r>";
    CHECK(!updateXmpPacket(nested, std::string("New"), date).has_value());
    CHECK(!updateXmpPacket(
               "<r xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\"><pdf:Producer>\xE9</pdf:Producer></r>",
               std::string("New"), date)
               .has_value());
}
