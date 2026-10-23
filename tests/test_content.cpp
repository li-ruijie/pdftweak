// Ports of the tests of OperationTests.java and RegressionTests.java for the steps that change pages
// in place (background, text watermark, page numbers, page labels, and bookmarks) and for the
// settings of the output (attachments, viewer preferences, and transitions).

#include "runner.h"

#include "cli_run.h"
#include "fixtures.h"
#include "inspect.h"

#include "core/formats/page_labels.h"
#include "core/pdf/encodings.h"
#include "core/pdf/helvetica.h"
#include "core/pdf/input_file.h"
#include "core/pdf/outline.h"
#include "core/pdf/tweak.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <windows.h>

#include <cmath>
#include <filesystem>

using namespace pdftweak::test;

namespace {

const std::pair<float, float> kA4{595.0f, 842.0f};

bool approx(float a, float b) { return std::fabs(a - b) < 0.01f; }

// OperationTests.assertTextAt
void checkTextAt(const std::string& text, float x, float y, const std::filesystem::path& pdf, int page) {
    std::string seen;
    for (const TextRun& run : textPositions(pdf, page)) {
        if (run.text == text) {
            if (!approx(run.x, x) || !approx(run.y, y))
                fail(__FILE__, __LINE__,
                     "\"" + text + "\" on page " + std::to_string(page) + " at " + std::to_string(run.x) +
                         ", " + std::to_string(run.y) + " instead of " + std::to_string(x) + ", " +
                         std::to_string(y));
            return;
        }
        seen += " \"" + run.text + "\"";
    }
    fail(__FILE__, __LINE__, "\"" + text + "\" not on page " + std::to_string(page) + ", found" + seen);
}

// OperationTests.pageLabels: Arrays.toString(PdfPageLabels.getPageLabels(reader))
std::string pageLabels(const std::filesystem::path& pdf) {
    const auto qpdf = openPdf(pdf);
    const auto formats = pdftweak::pdf::readPageLabels(*qpdf);
    if (!formats)
        return "null";
    std::string result = "[";
    const int count = pageCount(pdf);
    const std::vector<std::string> labels = pdftweak::formats::labelStrings(*formats, count);
    for (size_t i = 0; i < labels.size(); ++i)
        result += (i == 0 ? "" : ", ") + labels[i];
    return result + "]";
}

std::string pageDestination(QPDF& pdf, QPDFObjectHandle dest) {
    std::string result;
    int number = 1;
    for (QPDFObjectHandle page : pdf.getAllPages()) {
        if (page.getObjGen() == dest.getArrayItem(0).getObjGen())
            result = std::to_string(number);
        ++number;
    }
    for (int i = 1; i < dest.getArrayNItems(); ++i)
        result += " " + dest.getArrayItem(i).unparse();
    return result;
}

// RegressionTests.isOpen, by renaming the file, which Windows refuses for an open file
bool isOpen(const std::filesystem::path& file) {
    const std::filesystem::path moved = file.wstring() + L".moved";
    if (!MoveFileW(file.c_str(), moved.c_str()))
        return true;
    MoveFileW(moved.c_str(), file.c_str());
    return false;
}

} // namespace

TEST(unit_helveticaMeasuresAsIText) {
    using namespace pdftweak::pdf::helvetica;
    // P a g e space 1: 667 + 556 + 556 + 556 + 278 + 556 thousandths
    CHECK(approx(widthPoint("Page 1", 12), 38.028f));
    CHECK(approx(ascentPoint("Page 1", 12), 8.616f));     // the top of P at 718
    CHECK(approx(descentPoint("Page 1", 12), -2.64f));    // the bottom of g at -220
    CHECK(approx(descentPoint("EFH", 10), 0.0f));         // never above 0
    CHECK(approx(widthPoint("\xE2\x82\xAC", 10), 5.56f)); // the euro sign, byte 128
    CHECK(approx(widthPoint("\xE4\xB8\xAD", 10), 0.0f));  // a character WinAnsi lacks is dropped
}

TEST(unit_encodingsConvertAsIText) {
    using namespace pdftweak::pdf;
    // euro, u umlaut, em dash, U+0080, x: the C1 control has no WinAnsi byte
    CHECK_EQUAL(winAnsiBytes("\xE2\x82\xAC\xC3\xBC\xE2\x80\x94\xC2\x80x"), std::string("\x80\xFC\x97x"));
    CHECK_EQUAL(winAnsiBytes(std::string("a\0b", 3)), std::string("ab"));
    CHECK_EQUAL(winAnsiBytes("\xC2\xA0"), std::string("\xA0"));
    // euro and em dash in PDFDocEncoding
    CHECK_EQUAL(pdfDocBytes("\xE2\x82\xAC\xE2\x80\x94"), std::string("\xA0\x84"));
    CHECK_EQUAL(lowBytes("\xC4\x80"
                         "A"),
                std::string("\x00"
                            "A",
                            2));
}

// ---- page content ----

TEST(main_backgroundPutsTheFirstPageUnderEveryPage) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4, kA4});
    const auto background = pdfWithText(".", "background.pdf", "Background");
    runCli({in, "-background", background, "out.pdf"});
    for (int page = 1; page <= 2; ++page) {
        const std::string pageText = text("out.pdf", page);
        const std::string mine = "Page " + std::to_string(page);
        CHECK(pageText.find("Background") != std::string::npos && pageText.find(mine) != std::string::npos);
        CHECK(pageText.find("Background") < pageText.find(mine));
    }
}

TEST(main_textWatermarkIsDiagonalTransparentHelvetica) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4});
    runCli({in, "-watermark", "Confidential,40,0.25,#FF0000", "out.pdf"});
    const TextRun* mark = nullptr;
    const std::vector<TextRun> texts = textPositions("out.pdf", 1);
    for (const TextRun& t : texts)
        if (t.text == "Confidential")
            mark = &t;
    CHECK(mark != nullptr);
    if (mark != nullptr) {
        // From top left to bottom right, with the middle of the text (half the font size above the
        // baseline) in the middle of the page
        const double angle = std::atan(842.0 / 595.0);
        CHECK(approx(mark->x, static_cast<float>(297.5 - std::sin(angle) * 20)));
        CHECK(approx(mark->y, static_cast<float>(421 - std::cos(angle) * 20)));
        CHECK(approx(mark->angle, static_cast<float>(-angle * 180 / 3.14159265358979323846)));
    }
    const auto qpdf = openPdf("out.pdf");
    QPDFObjectHandle resources = qpdf->getAllPages().at(0).getKey("/Resources");
    bool transparent = false;
    QPDFObjectHandle states = resources.getKey("/ExtGState");
    for (const std::string& key : states.getKeys())
        transparent |= states.getKey(key).getKey("/ca").getNumericValue() == 0.25;
    CHECK(transparent);
    bool helvetica = false;
    QPDFObjectHandle fonts = resources.getKey("/Font");
    for (const std::string& key : fonts.getKeys())
        helvetica |= fonts.getKey(key).getKey("/BaseFont").isNameAndEquals("/Helvetica");
    CHECK(helvetica);
    CHECK(content("out.pdf", 1).find("1 0 0 rg") != std::string::npos);
}

TEST(main_pageNumbersFollowPositionMaskAndLabels) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4, kA4});
    // RB: 30 points in from the right edge, 20 up from the bottom
    runCli({in, "-numbers", "RB,10,30,20,%1$d/%2$d", "right.pdf"});
    for (int page = 1; page <= 2; ++page)
        checkTextAt(std::to_string(page) + "/2", 565, 20, "right.pdf", page);
    // OB: on the outer edge, so right on odd pages and left on even ones
    runCli({in, "-numbers", "OB,10,30,20", "outer.pdf"});
    checkTextAt("1", 565, 20, "outer.pdf", 1);
    checkTextAt("2", 30, 20, "outer.pdf", 2);
    // T: top centre, printing page labels set in an earlier block
    runCli({in, "-pagenumbers", "1=1i", "--", "-numbers", "T,10,0,20,%4$s (%3$d)", "labels.pdf"});
    checkTextAt("i (1)", 297.5f, 822, "labels.pdf", 1);
    checkTextAt("ii (2)", 297.5f, 822, "labels.pdf", 2);
    const std::string error = cliError({in, "-numbers", "L,10,1,20", "outside.pdf"});
    CHECK(error.find("not within page bounding box") != std::string::npos);
}

// ---- document structure ----

TEST(main_pageLabelsAreSetRemovedAndLoaded) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4, kA4, kA4, kA4});
    runCli({in, "-pagenumbers", "1=Intro:1i", "-pagenumbers", "3=1", "labelled.pdf"});
    CHECK_EQUAL(pageLabels("labelled.pdf"), std::string("[Introi, Introii, 1, 2]"));
    const std::string infoText = info("labelled.pdf", "-info");
    CHECK(infoText.find("PAGELABELS 1=Intro:1i") != std::string::npos &&
          infoText.find("PAGELABELS 3=1") != std::string::npos);
    runCli({"labelled.pdf", "-pagenumbers", "-", "removed.pdf"});
    // iText's PdfPageLabels always starts with a decimal format at page 1
    CHECK_EQUAL(pageLabels("removed.pdf"), std::string("[1, 2, 3, 4]"));
    writeFile("labels.txt", "1=1A\n2=5\n");
    runCli({in, "-loadpagenumbers", "labels.txt", "loaded.pdf"});
    CHECK_EQUAL(pageLabels("loaded.pdf"), std::string("[A, 5, 6, 7]"));
    CHECK_EQUAL(cliError({in, "-pagenumbers", "0=1", "zero.pdf"}),
                std::string("In a page label the page numbers must be greater or equal to 1."));
}

TEST(main_bookmarksAreAddedSavedLoadedAndRemoved) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4, kA4});
    const std::string part = "1;O;Part;1 Fit";
    const std::string chapter = "2;BI;Chapter \\3B one;2 XYZ 0 800 0";
    runCli({in, "-bookmark", part, "-bookmark", chapter, "marked.pdf"});
    {
        const auto qpdf = openPdf("marked.pdf");
        const auto outline = pdftweak::pdf::readBookmarks(*qpdf, 1);
        CHECK_EQUAL(outline.size(), size_t(2));
        CHECK_EQUAL(outline.at(0).depth, 1);
        CHECK_EQUAL(outline.at(0).title, std::string("Part"));
        CHECK_EQUAL(outline.at(1).depth, 2);
        CHECK_EQUAL(outline.at(1).title, std::string("Chapter ; one"));
        CHECK(outline.at(1).bold && outline.at(1).italic);
    }
    runCli({"marked.pdf", "-savebookmarks", "saved.txt"});
    // A PDF keeps "open" only for bookmarks with children, so the chapter comes back open
    std::string expected = part + "\r\n" + "2;OBI;Chapter \\3B one;2 XYZ 0 800 0" + "\r\n";
    CHECK_EQUAL(readFile("saved.txt"), expected);
    runCli({in, "-loadbookmarks", "saved.txt", "loaded.pdf"});
    runCli({"loaded.pdf", "-savebookmarks", "saved-again.txt"});
    CHECK_EQUAL(readFile("saved-again.txt"), readFile("saved.txt"));
    runCli({"marked.pdf", "-bookmark", "-", "removed.pdf"});
    const auto removed = openPdf("removed.pdf");
    CHECK(pdftweak::pdf::readBookmarks(*removed, 1).empty());
    CHECK(!removed->getRoot().hasKey("/Outlines"));
    CHECK_EQUAL(cliError({in, "-bookmark", "2;;Too deep;1", "deep.pdf"}),
                std::string("Invalid depth value"));
}

TEST(main_bookmarkOptionsKeepTheirOrder) {
    // B="1" A="2", with the quotes escaped as in a bookmark file. A HashMap iterates "A" before "B",
    // the opposite of this line.
    const std::string line = "1;O;Chapter;1;B=\\221\\22 A=\\222\\22";
    CHECK_EQUAL(pdftweak::formats::parseBookmark(line).toString(), line);
    const auto in = fixture(".", "in.pdf", 1);
    runCli({in, "-bookmark", line, "out.pdf"});
    const auto qpdf = openPdf("out.pdf");
    const auto outline = pdftweak::pdf::readBookmarks(*qpdf, 1);
    CHECK_EQUAL(outline.size(), size_t(1));
    CHECK_EQUAL(outline.at(0).title, std::string("Chapter"));
}

// PdfBookmark.buildBookmarks, which the Bookmarks tab loads for multiple input files: each range gets
// a bookmark named after its file, over the file's own bookmarks moved to where the range puts them.
// Bookmarks without children read back open, as iText reads them.
TEST(main_bookmarksOfPageRangesFollowTheRanges) {
    const auto a = pdfOfSizes(".", "a.pdf", {kA4, kA4, kA4});
    const auto b = pdfOfSizes(".", "b.pdf", {kA4, kA4});
    runCli({a, "-bookmark", "1;;A1;2", "-bookmark", "2;;A1x;3", "a-marked.pdf"});
    runCli({b, "-bookmark", "1;;B;1", "b-marked.pdf"});
    const auto fileA = std::make_shared<pdftweak::pdf::InputFile>("a-marked.pdf", "");
    const auto fileB = std::make_shared<pdftweak::pdf::InputFile>("b-marked.pdf", "");
    const std::vector<pdftweak::pdf::InputRange> ranges{
        {fileA, pdftweak::formats::makePageRange(0, 3, 3, 1, true, true, {0})},
        {fileB, pdftweak::formats::makePageRange(1, 2, 1, -1, true, true, {1})}};
    std::vector<std::string> lines;
    for (const auto& bookmark : pdftweak::pdf::buildBookmarks(ranges))
        lines.push_back(std::to_string(bookmark.depth) + " " + bookmark.title + " " +
                        std::to_string(bookmark.page) + (bookmark.open ? " open" : ""));
    CHECK(lines == (std::vector<std::string>{"1 a-marked.pdf 1 open", "2 A1 2", "3 A1x 1 open",
                                             "1 b-marked.pdf 5 open", "2 B 5 open"}));
}

// PdfInputFile.reopen reads the file again, so that a run starts from the file and not from the
// document an earlier run changed.
TEST(main_inputFileReopenReadsTheFileAgain) {
    pdftweak::pdf::InputFile file(pdfOfSizes(".", "in.pdf", {kA4}), "");
    file.qpdf()->getRoot().replaceKey("/Changed", QPDFObjectHandle::newInteger(1));
    file.reopen();
    CHECK(!file.qpdf()->getRoot().hasKey("/Changed"));
    CHECK_EQUAL(file.pageCount(), 1);
}

TEST(main_bookmarkActionsAndColours) {
    // the keys SimpleBookmark writes from the options of a line: a colour, a URI action, and a
    // GoTo to a page that does not exist, which leaves the item without a destination
    const auto in = fixture(".", "in.pdf", 2);
    runCli({in, "-bookmark",
            "1;;Web;0;Action=\\22URI\\22 URI=\\22https://example.com/\\22 Color=\\220 0.5 2\\22",
            "-bookmark", "1;;Missing;9 Fit", "-bookmark", "1;B;Second;2", "out.pdf"});
    const auto qpdf = openPdf("out.pdf");
    QPDFObjectHandle outlines = qpdf->getRoot().getKey("/Outlines");
    CHECK_EQUAL(outlines.getKey("/Count").getIntValue(), 3LL);
    QPDFObjectHandle web = outlines.getKey("/First");
    CHECK_EQUAL(web.getKey("/A").getKey("/URI").getStringValue(), std::string("https://example.com/"));
    CHECK_EQUAL(web.getKey("/C").unparse(), std::string("[ 0 0.5 1 ]"));
    QPDFObjectHandle missing = web.getKey("/Next");
    CHECK(!missing.hasKey("/Dest") && !missing.hasKey("/A"));
    QPDFObjectHandle second = missing.getKey("/Next");
    CHECK_EQUAL(pageDestination(*qpdf, second.getKey("/Dest")), std::string("2 /XYZ 0 10000 0"));
    CHECK_EQUAL(second.getKey("/F").getIntValue(), 2LL);
    CHECK(second.getKey("/Prev").getObjGen() == missing.getObjGen());
}

TEST(main_attachmentIsEmbedded) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4});
    writeFile("note.txt", "hello");
    runCli({in, "-attach", "note.txt", "out.pdf"});
    const auto qpdf = openPdf("out.pdf");
    QPDFObjectHandle names = qpdf->getRoot().getKey("/Names").getKey("/EmbeddedFiles").getKey("/Names");
    CHECK_EQUAL(names.getArrayNItems(), 2);
    QPDFObjectHandle spec = names.getArrayItem(1);
    CHECK_EQUAL(spec.getKey("/F").getUTF8Value(), std::string("note.txt"));
    const auto data = spec.getKey("/EF").getKey("/F").getStreamData(qpdf_dl_generalized);
    CHECK_EQUAL(std::string(reinterpret_cast<const char*>(data->getBuffer()), data->getSize()),
                std::string("hello"));
    // a second run adds to the tree under a name of its own
    runCli({"out.pdf", "-attach", "note.txt", "twice.pdf"});
    const auto twice = openPdf("twice.pdf");
    QPDFObjectHandle both = twice->getRoot().getKey("/Names").getKey("/EmbeddedFiles").getKey("/Names");
    CHECK_EQUAL(both.getArrayNItems(), 4);
    CHECK_EQUAL(both.getArrayItem(0).getStringValue(), std::string("note.txt"));
    CHECK_EQUAL(both.getArrayItem(2).getStringValue(), std::string("note.txt 1"));
    CHECK(cliError({in, "-attach", "missing.txt", "none.pdf"})
              .find("missing.txt not found as file or resource.") != std::string::npos);
}

TEST(main_viewerPreferencesPageModeAndLayout) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4});
    // Values as the GUI shows them, names with their slash
    runCli({in, "-pagemode", "Outline", "-pagelayout", "Two_Columns_Left", "-viewpref", "Hide_Toolbar=true",
            "-viewpref", "Direction=/R2L", "-viewpref", "Number_of_copies=2", "out.pdf"});
    const auto qpdf = openPdf("out.pdf");
    QPDFObjectHandle catalog = qpdf->getRoot();
    CHECK(catalog.getKey("/PageMode").isNameAndEquals("/UseOutlines"));
    CHECK(catalog.getKey("/PageLayout").isNameAndEquals("/TwoColumnLeft"));
    QPDFObjectHandle prefs = catalog.getKey("/ViewerPreferences");
    CHECK(prefs.getKey("/HideToolbar").isBool() && prefs.getKey("/HideToolbar").getBoolValue());
    CHECK(prefs.getKey("/Direction").isNameAndEquals("/R2L"));
    CHECK_EQUAL(prefs.getKey("/NumCopies").getIntValue(), 2LL);
    const CliResult bad = runCliCapture({in, "-pagemode", "Sideways", "bad.pdf"});
    CHECK_EQUAL(bad.exitCode, 1);
    CHECK_EQUAL(bad.err, std::string("Invalid page mode: Sideways\r\n"));
}

TEST(main_viewerPreferenceDisplayDocumentTitle) {
    const auto in = fixture(".", "in.pdf", 1);
    // The corrected name, and the misspelt one that scripts may still use
    for (const std::string name : {"Display_document_title", "Display_document_tile"}) {
        runCli({in, "-viewpref", name + "=true", name + ".pdf"});
        const auto qpdf = openPdf(name + ".pdf");
        QPDFObjectHandle prefs = qpdf->getRoot().getKey("/ViewerPreferences");
        CHECK(prefs.isDictionary() && prefs.getKey("/DisplayDocTitle").isBool() &&
              prefs.getKey("/DisplayDocTitle").getBoolValue());
    }
}

TEST(main_transitionHorizontalBlindsIsSelectable) {
    const auto in = fixture(".", "in.pdf", 2);
    runCli({in, "-trans", "1:Vertical_Blinds,1", "-trans", "2:Horizontal_Blinds,1,5", "out.pdf"});
    const auto qpdf = openPdf("out.pdf");
    const auto pages = qpdf->getAllPages();
    for (int page = 1; page <= 2; ++page) {
        QPDFObjectHandle trans = pages.at(static_cast<size_t>(page - 1)).getKey("/Trans");
        CHECK(trans.isDictionary());
        CHECK(trans.getKey("/S").isNameAndEquals("/Blinds"));
        CHECK(trans.getKey("/Dm").isNameAndEquals(page == 1 ? "/V" : "/H"));
    }
    CHECK(!pages.at(0).hasKey("/Dur"));
    CHECK_EQUAL(pages.at(1).getKey("/Dur").getIntValue(), 5LL);
}

TEST(main_transitionAcceptsMultiDigitPages) {
    const auto in = fixture(".", "in.pdf", 12);
    runCli({in, "-trans", "10-~1:Dissolve,1", "out.pdf"});
    const auto qpdf = openPdf("out.pdf");
    const auto pages = qpdf->getAllPages();
    for (int page = 9; page <= 12; ++page) {
        QPDFObjectHandle trans = pages.at(static_cast<size_t>(page - 1)).getKey("/Trans");
        if (page == 9) {
            CHECK(trans.isNull());
        } else {
            CHECK(trans.isDictionary() && trans.getKey("/S").isNameAndEquals("/Dissolve"));
        }
    }
    const CliResult unknown = runCliCapture({in, "-trans", "1:Spin,1", "-trans", "x", "warned.pdf"});
    CHECK_EQUAL(unknown.exitCode, 0);
    CHECK(unknown.err.find("Skipping unknown transition name: Spin\r\n") != std::string::npos);
    CHECK(unknown.err.find("Skipping unparsable transition: x\r\n") != std::string::npos);
}

TEST(main_backgroundFileIsClosedAfterUse) {
    const auto in = fixture(".", "in.pdf", 1);
    const auto background = fixture(".", "background.pdf", 1);
    runCli({in, "-background", background, "out.pdf"});
    CHECK(!isOpen(background));
}

TEST(main_backgroundKeepsTheLinksOfThePage) {
    // With -preserveLinks, jPDF Tweak 1.3.0 added the external links of the page again, which
    // PdfStamper refuses. Version 1.3.1 and the port keep the links of the page as they are.
    pdfOfSizes(".", "plain.pdf", {kA4});
    {
        QPDF pdf;
        pdf.processFile("plain.pdf");
        QPDFObjectHandle page = pdf.getAllPages().at(0);
        QPDFObjectHandle uri = pdf.makeIndirectObject(
            QPDFObjectHandle::parse("<< /Type /Annot /Subtype /Link /Rect [50 50 150 100] /H /I "
                                    "/A << /S /URI /URI (https://example.com/) >> >>"));
        QPDFObjectHandle internal = pdf.makeIndirectObject(
            QPDFObjectHandle::parse("<< /Type /Annot /Subtype /Link /Rect [50 150 150 200] >>"));
        QPDFObjectHandle dest = QPDFObjectHandle::newArray();
        dest.appendItem(page);
        dest.appendItem(QPDFObjectHandle::parse("/XYZ"));
        for (const int n : {0, 842, 0})
            dest.appendItem(QPDFObjectHandle::newInteger(n));
        internal.replaceKey("/Dest", dest);
        page.replaceKey("/Annots", QPDFObjectHandle::newArray({uri, internal}));
        QPDFWriter writer(pdf, "in.pdf");
        writer.write();
    }
    const auto background = pdfWithText(".", "background.pdf", "Background");
    runCli({"in.pdf", "-preserveLinks", "On", "-background", background, "out.pdf"});
    const auto qpdf = openPdf("out.pdf");
    QPDFObjectHandle annots = qpdf->getAllPages().at(0).getKey("/Annots");
    std::string links = "[";
    for (int i = 0; annots.isArray() && i < annots.getArrayNItems(); ++i) {
        QPDFObjectHandle link = annots.getArrayItem(i);
        QPDFObjectHandle action = link.getKey("/A");
        links += (i == 0 ? "" : ", ") + link.getKey("/Rect").unparse() + " " +
                 (action.isDictionary() ? action.getKey("/URI").getUTF8Value()
                                        : pageDestination(*qpdf, link.getKey("/Dest")));
    }
    links += "]";
    CHECK_EQUAL(links,
                std::string("[[ 50 50 150 100 ] https://example.com/, [ 50 150 150 200 ] 1 /XYZ 0 842 0]"));
    CHECK(text("out.pdf", 1).find("Background") != std::string::npos);
}
