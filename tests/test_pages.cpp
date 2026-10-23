// Ports of the page operation tests of OperationTests.java and RegressionTests.java, including the
// tests of jPDF Tweak 1.3.0 for links and the catalog entries that rebuilt pages keep (decision 16).

#include "runner.h"

#include "cli_run.h"
#include "fixtures.h"
#include "inspect.h"

#include "core/formats/page_labels.h"
#include "core/pdf/input_file.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFNameTreeObjectHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <cmath>
#include <filesystem>
#include <map>

using namespace pdftweak::test;

namespace {

const std::pair<float, float> kA4{595.0f, 842.0f};
const std::pair<float, float> kLandscapeA4{842.0f, 595.0f};

bool approx(float a, float b) { return std::fabs(a - b) < 0.01f; }

bool sameMatrix(const std::vector<float>& expected, const std::array<float, 6>& actual) {
    for (size_t i = 0; i < 6; ++i)
        if (!approx(expected[i], actual[i]))
            return false;
    return true;
}

bool boxIs(const std::filesystem::path& pdf, int page, float width, float height) {
    const auto box = mediaBox(pdf, page);
    return approx(box[2] - box[0], width) && approx(box[3] - box[1], height);
}

// RegressionTests.firstPlacement: the first "cm" operator of page 1, as written.
std::string firstPlacement(const std::filesystem::path& pdf) {
    const std::string text = content(pdf, 1);
    const size_t cm = text.find(" cm");
    size_t start = cm;
    for (int fields = 0; fields < 6 && start != std::string::npos; ++fields)
        start = text.rfind(' ', start - 1);
    return text.substr(start + 1, cm + 3 - start - 1);
}

int pageNumberOf(QPDF& pdf, QPDFObjectHandle page) {
    int n = 1;
    for (QPDFPageObjectHelper& p : QPDFPageDocumentHelper(pdf).getAllPages()) {
        if (p.getObjectHandle().getObjGen() == page.getObjGen())
            return n;
        ++n;
    }
    return 0;
}

// OperationTests.destination: a destination as its page number and the rest of its array.
std::string destination(QPDF& pdf, QPDFObjectHandle dest) {
    std::string s = std::to_string(pageNumberOf(pdf, dest.getArrayItem(0)));
    for (int i = 1; i < dest.getArrayNItems(); ++i)
        s += " " + dest.getArrayItem(i).unparse();
    return s;
}

// OperationTests.target: a destination, or what holds one in /D, or "-".
std::string target(QPDF& pdf, QPDFObjectHandle object) {
    if (object.isDictionary())
        object = object.getKey("/D");
    if (!object.isInitialized() || object.isNull())
        return "-";
    if (!object.isArray())
        return "named " + object.unparse();
    return destination(pdf, object);
}

// OperationTests.linkFixture: three A4 pages, the first with one link for each destination, which
// is given without its page.
std::filesystem::path linkFixture(const std::string& name, int targetPage,
                                  const std::vector<std::string>& dests) {
    const auto plain = pdfOfSizes(".", "plain-" + name, {kA4, kA4, kA4});
    QPDF pdf;
    pdf.processFile(plain.string().c_str());
    const auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
    QPDFObjectHandle annots = QPDFObjectHandle::newArray();
    for (size_t i = 0; i < dests.size(); ++i) {
        const int y = 50 + 60 * static_cast<int>(i);
        QPDFObjectHandle link = QPDFObjectHandle::parse(
            "<< /Type /Annot /Subtype /Link /Rect [50 " + std::to_string(y) + " 150 " +
            std::to_string(y + 50) + "] /Dest [null " + dests[i] + "] >>");
        link.getKey("/Dest").setArrayItem(0, pages[static_cast<size_t>(targetPage - 1)].getObjectHandle());
        annots.appendItem(pdf.makeIndirectObject(link));
    }
    QPDFObjectHandle first = pages[0].getObjectHandle();
    first.replaceKey("/Annots", annots);
    QPDFWriter writer(pdf, name.c_str());
    writer.write();
    return name;
}

// OperationTests.linkDestinations: the destinations of the links on a page.
std::string linkDestinations(const std::filesystem::path& file, int page) {
    const auto pdf = openPdf(file);
    QPDFObjectHandle annots =
        QPDFPageDocumentHelper(*pdf).getAllPages()[static_cast<size_t>(page - 1)].getObjectHandle().getKey(
            "/Annots");
    std::string list = "[";
    for (int i = 0; annots.isArray() && i < annots.getArrayNItems(); ++i)
        list += (i ? ", " : "") + destination(*pdf, annots.getArrayItem(i).getKey("/Dest"));
    return list + "]";
}

// OperationTests.catalogFixture: three A4 pages with bookmarks to /XYZ on page 2 (by /Dest), /FitH on
// page 3 (by a GoTo action), and /FitR on page 1, named destinations in the name tree and in /Dests,
// an attachment, viewer settings, an open action, a language, page labels, and a layer on page 1
// that is hidden by default.
std::filesystem::path catalogFixture() {
    const auto plain = pdfOfSizes(".", "catalog-plain.pdf", {kA4, kA4, kA4});
    QPDF pdf;
    pdf.processFile(plain.string().c_str());
    const auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
    const auto page = [&](int n) { return pages[static_cast<size_t>(n - 1)].getObjectHandle(); };
    QPDFObjectHandle root = pdf.getRoot();
    // the hidden layer, drawn on page 1
    QPDFObjectHandle layer =
        pdf.makeIndirectObject(QPDFObjectHandle::parse("<< /Type /OCG /Name (Hidden) >>"));
    QPDFObjectHandle resources = page(1).getKey("/Resources");
    resources.replaceKey("/Properties", QPDFObjectHandle::parse("<< >>"));
    resources.getKey("/Properties").replaceKey("/OC1", layer);
    page(1).addPageContents(pdf.newStream("/OC /OC1 BDC 100 100 50 50 re f EMC\n"), false);
    QPDFObjectHandle properties =
        QPDFObjectHandle::parse("<< /OCGs [null] /D << /OFF [null] /Order [null] >> >>");
    properties.getKey("/OCGs").setArrayItem(0, layer);
    properties.getKey("/D").getKey("/OFF").setArrayItem(0, layer);
    properties.getKey("/D").getKey("/Order").setArrayItem(0, layer);
    root.replaceKey("/OCProperties", properties);
    // bookmarks
    QPDFObjectHandle outlines =
        pdf.makeIndirectObject(QPDFObjectHandle::parse("<< /Type /Outlines /Count 3 >>"));
    std::vector<QPDFObjectHandle> items;
    for (const char* text : {"<< /Title (XYZ) /Dest [null /XYZ 100 700 0] >>",
                             "<< /Title (FitH) /A << /S /GoTo /D [null /FitH 500] >> >>",
                             "<< /Title (FitR) /Dest [null /FitR 100 200 300 400] >> "})
        items.push_back(pdf.makeIndirectObject(QPDFObjectHandle::parse(text)));
    items[0].getKey("/Dest").setArrayItem(0, page(2));
    items[1].getKey("/A").getKey("/D").setArrayItem(0, page(3));
    items[2].getKey("/Dest").setArrayItem(0, page(1));
    for (size_t i = 0; i < items.size(); ++i) {
        items[i].replaceKey("/Parent", outlines);
        if (i > 0)
            items[i].replaceKey("/Prev", items[i - 1]);
        if (i + 1 < items.size())
            items[i].replaceKey("/Next", items[i + 1]);
    }
    outlines.replaceKey("/First", items.front());
    outlines.replaceKey("/Last", items.back());
    root.replaceKey("/Outlines", outlines);
    // named destinations, an attachment
    QPDFObjectHandle named = QPDFObjectHandle::parse("[null /XYZ 50 400 0]");
    named.setArrayItem(0, page(3));
    QPDFNameTreeObjectHelper tree = QPDFNameTreeObjectHelper::newEmpty(pdf);
    tree.insert("named", named);
    QPDFObjectHandle file = pdf.newStream("Attached");
    file.getDict().replaceKey("/Type", QPDFObjectHandle::newName("/EmbeddedFile"));
    QPDFObjectHandle spec = pdf.makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Filespec /F (note.txt) /UF (note.txt) /EF << >> >>"));
    spec.getKey("/EF").replaceKey("/F", file);
    QPDFNameTreeObjectHelper files = QPDFNameTreeObjectHelper::newEmpty(pdf);
    files.insert("Note", spec);
    QPDFObjectHandle names = QPDFObjectHandle::newDictionary();
    names.replaceKey("/Dests", tree.getObjectHandle());
    names.replaceKey("/EmbeddedFiles", files.getObjectHandle());
    root.replaceKey("/Names", names);
    QPDFObjectHandle dests = QPDFObjectHandle::parse("<< /old << /D [null /Fit] >> >>");
    dests.getKey("/old").getKey("/D").setArrayItem(0, page(1));
    root.replaceKey("/Dests", dests);
    // viewer settings, open action, language, page labels
    root.replaceKey("/PageMode", QPDFObjectHandle::newName("/UseOutlines"));
    root.replaceKey("/PageLayout", QPDFObjectHandle::newName("/TwoColumnLeft"));
    root.replaceKey("/ViewerPreferences", QPDFObjectHandle::parse("<< /HideToolbar true >>"));
    QPDFObjectHandle open = QPDFObjectHandle::parse("<< /S /GoTo /D [null /FitV 200] >>");
    open.getKey("/D").setArrayItem(0, page(2));
    root.replaceKey("/OpenAction", open);
    root.replaceKey("/Lang", QPDFObjectHandle::newString("en-GB"));
    root.replaceKey("/PageLabels", QPDFObjectHandle::parse("<< /Nums [0 << /S /r >>] >>"));
    QPDFWriter writer(pdf, "catalog.pdf");
    writer.write();
    return "catalog.pdf";
}

// Whether resources, or the resources of a form they hold, name the object as a property list.
bool usesObject(QPDFObjectHandle resources, QPDFObjGen og) {
    if (!resources.isDictionary())
        return false;
    QPDFObjectHandle properties = resources.getKey("/Properties");
    if (properties.isDictionary())
        for (const std::string& key : properties.getKeys())
            if (properties.getKey(key).isIndirect() && properties.getKey(key).getObjGen() == og)
                return true;
    QPDFObjectHandle forms = resources.getKey("/XObject");
    if (forms.isDictionary())
        for (const std::string& key : forms.getKeys())
            if (forms.getKey(key).isStream() &&
                usesObject(forms.getKey(key).getDict().getKey("/Resources"), og))
                return true;
    return false;
}

// OperationTests.catalogSummary: the catalog entries of catalogFixture, one per line.
std::string catalogSummary(const std::filesystem::path& file) {
    const auto pdf = openPdf(file);
    QPDFObjectHandle catalog = pdf->getRoot();
    std::string s;
    QPDFObjectHandle outlines = catalog.getKey("/Outlines");
    for (QPDFObjectHandle item = outlines.isDictionary() ? outlines.getKey("/First")
                                                         : QPDFObjectHandle::newNull();
         item.isDictionary(); item = item.getKey("/Next")) {
        QPDFObjectHandle t = item.hasKey("/Dest") ? item.getKey("/Dest") : item.getKey("/A");
        s += "bookmark " + item.getKey("/Title").getUTF8Value() + ": " + target(*pdf, t) + "\n";
    }
    QPDFObjectHandle names = catalog.getKey("/Names");
    if (names.isDictionary() && names.getKey("/Dests").isDictionary()) {
        std::map<std::string, QPDFObjectHandle> sorted;
        for (const auto& [key, value] : QPDFNameTreeObjectHelper(names.getKey("/Dests"), *pdf))
            sorted[key] = value;
        for (const auto& [key, value] : sorted)
            s += "named " + key + ": " + target(*pdf, value) + "\n";
    }
    QPDFObjectHandle dests = catalog.getKey("/Dests");
    if (dests.isDictionary())
        for (const std::string& key : dests.getKeys())
            s += "dests " + key + ": " + target(*pdf, dests.getKey(key)) + "\n";
    s += "open: " + target(*pdf, catalog.getKey("/OpenAction")) + "\n";
    if (names.isDictionary() && names.getKey("/EmbeddedFiles").isDictionary()) {
        for (const auto& [key, value] : QPDFNameTreeObjectHelper(names.getKey("/EmbeddedFiles"), *pdf)) {
            const auto data = value.getKey("/EF").getKey("/F").getStreamData(qpdf_dl_generalized);
            s += "attachment " + key + ": " +
                 std::string(reinterpret_cast<const char*>(data->getBuffer()), data->getSize()) + "\n";
        }
    }
    s += "/PageMode: " + catalog.getKey("/PageMode").unparse() + "\n";
    s += "/PageLayout: " + catalog.getKey("/PageLayout").unparse() + "\n";
    s +=
        "/Lang: " + (catalog.getKey("/Lang").isString() ? catalog.getKey("/Lang").getUTF8Value() : "null") +
        "\n";
    QPDFObjectHandle preferences = catalog.getKey("/ViewerPreferences");
    s += "HideToolbar: " +
         (preferences.isDictionary() ? preferences.getKey("/HideToolbar").unparse() : "null") + "\n";
    const auto formats = pdftweak::pdf::readPageLabels(*pdf);
    std::string labels = "null";
    if (formats) {
        labels = "[";
        const auto strings = pdftweak::formats::labelStrings(
            *formats, static_cast<int>(QPDFPageDocumentHelper(*pdf).getAllPages().size()));
        for (size_t i = 0; i < strings.size(); ++i)
            labels += (i ? ", " : "") + strings[i];
        labels += "]";
    }
    s += "labels: " + labels + "\n";
    bool shared = false;
    QPDFObjectHandle properties = catalog.getKey("/OCProperties");
    if (properties.isDictionary()) {
        QPDFObjectHandle off = properties.getKey("/D").getKey("/OFF").getArrayItem(0);
        shared =
            usesObject(QPDFPageDocumentHelper(*pdf).getAllPages()[0].getObjectHandle().getKey("/Resources"),
                       off.getObjGen());
    }
    s += std::string("hidden layer shared: ") + (shared ? "true" : "false") + "\n";
    return s;
}

} // namespace

TEST(main_cropUsesTheBoxOrItsFallback) {
    const auto plain = pdfOfSizes(".", "plain.pdf", {kA4});
    const auto in =
        setPageEntries(plain, "in.pdf", 1, {{"/CropBox", "[50 100 545 742]"}, {"/Rotate", "90"}});
    // TrimBox and ArtBox fall back to the CropBox, as neither is set
    for (const std::string box : {"CropBox", "TrimBox", "ArtBox", "MediaBox"}) {
        const std::filesystem::path out = box + ".pdf";
        runCli({in, "-crop", box, out});
        if (box == "MediaBox")
            CHECK(boxIs(out, 1, 595, 842));
        else
            CHECK(boxIs(out, 1, 495, 642));
        CHECK_EQUAL(rotation(out, 1), 90);
    }
    CHECK(sameMatrix({1, 0, 0, 1, -50, -100}, placements("CropBox.pdf", 1)[0]));
}

TEST(main_scaleFitsCentresOrStretches) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4});
    const float f = 595.0f / 842.0f;
    const std::pair<std::string, std::vector<float>> cases[] = {
        {"842,595", {f, 0, 0, f, (842 - 595 * f) / 2, 0}}, // fit, keeping the aspect ratio
        {"1190,1684,c", {1, 0, 0, 1, 297.5f, 421}},        // centre instead of enlarging
        {"1190,842,r", {2, 0, 0, 1, 0, 0}},                // stretch
        {"1190,1684", {2, 0, 0, 2, 0, 0}}};                // enlarge
    int i = 0;
    for (const auto& [value, matrix] : cases) {
        const std::filesystem::path out = "scale" + std::to_string(i++) + ".pdf";
        runCli({in, "-scale", value, out});
        const float width = std::stof(value.substr(0, value.find(',')));
        const float height = std::stof(value.substr(value.find(',') + 1));
        CHECK(boxIs(out, 1, width, height));
        CHECK(sameMatrix(matrix, placements(out, 1)[0]));
    }
}

TEST(main_rotateTurnsPortraitAndLandscapePagesSeparately) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4, kLandscapeA4, kA4});
    runCli({in, "-rotate", "KL", "implicit.pdf"});
    const int expected[] = {0, 270, 0};
    for (int page = 1; page <= 3; ++page)
        CHECK_EQUAL(rotation("implicit.pdf", page), expected[page - 1]);
    runCli({in, "-rotexp", "RK", "explicit.pdf"});
    for (int page = 1; page <= 3; ++page) {
        CHECK_EQUAL(rotation("explicit.pdf", page), 0);
        CHECK(boxIs("explicit.pdf", page, 842, 595));
    }
}

TEST(main_shufflePlacesPagesForTwoUpAndBooklets) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4, kA4, kA4, kA4});
    runCli({in, "-shuffle", "2:!+1L0.707+0%-100%,+2L.707+100%-100%F2", "two-up.pdf"});
    CHECK_EQUAL(pageCount("two-up.pdf"), 2);
    const auto m = placements("two-up.pdf", 1);
    CHECK_EQUAL(m.size(), size_t(2));
    CHECK(sameMatrix({0, .707f, -.707f, 0, 842 * .707f, 0}, m[0]));
    CHECK(sameMatrix({0, .707f, -.707f, 0, 842 * .707f, 595 * .707f}, m[1]));
    CHECK(text("two-up.pdf", 1).find("Page 1") != std::string::npos);
    CHECK(text("two-up.pdf", 1).find("Page 2") != std::string::npos);
    CHECK(text("two-up.pdf", 2).find("Page 3") != std::string::npos);
    CHECK(text("two-up.pdf", 2).find("Page 4") != std::string::npos);
    CHECK(content("two-up.pdf", 1).find("2 w") != std::string::npos);

    runCli({in, "-shuffle", "-4:!-4N1+0%+0%,!+1N1+0%+0%,!+2N1+0%+0%,!-3N1+0%+0%", "booklet.pdf"});
    const int order[] = {4, 1, 2, 3};
    for (int page = 1; page <= 4; ++page)
        CHECK(text("booklet.pdf", page).find("Page " + std::to_string(order[page - 1])) !=
              std::string::npos);
}

TEST(main_preserveLinksMovesThemWithTheCrop) {
    // The link covers 50,50 to 150,100 and points at 0,842 on its own page.
    const auto plain = fixture(".", "plain.pdf", 1, 1, 1);
    const auto in = setPageEntries(plain, "in.pdf", 1, {{"/CropBox", "[50 100 545 742]"}});
    runCli({in, "-preserveLinks", "On", "-crop", "CropBox", "out.pdf"});
    {
        const auto pdf = openPdf("out.pdf");
        QPDFObjectHandle annots =
            QPDFPageDocumentHelper(*pdf).getAllPages()[0].getObjectHandle().getKey("/Annots");
        CHECK(annots.isArray() && annots.getArrayNItems() == 1);
        const auto rect = pdftweak::pdf::rectangle(annots.getArrayItem(0).getKey("/Rect"));
        CHECK(approx(rect.llx, 0) && approx(rect.lly, -50) && approx(rect.urx, 100) && approx(rect.ury, 0));
        QPDFObjectHandle dest = annots.getArrayItem(0).getKey("/Dest");
        CHECK(approx(static_cast<float>(dest.getArrayItem(2).getNumericValue()), -50));
        CHECK(approx(static_cast<float>(dest.getArrayItem(3).getNumericValue()), 742));
    }
    runCli({in, "-crop", "CropBox", "dropped.pdf"});
    const auto pdf = openPdf("dropped.pdf");
    CHECK(!QPDFPageDocumentHelper(*pdf).getAllPages()[0].getObjectHandle().getKey("/Annots").isArray());
}

TEST(main_linkDestinationsFollowTheirPage) {
    const auto in = linkFixture("in.pdf", 2, {"/FitH 500", "/FitR 100 200 300 400"});
    runCli({in, "-preserveLinks", "On", "-scale", "1190,1684", "scaled.pdf"});
    CHECK_EQUAL(linkDestinations("scaled.pdf", 1), std::string("[2 /FitH 1000, 2 /FitR 200 400 600 800]"));
    // A page turned by 90 degrees and then drawn upright: x' = y, y' = 595 - x
    runCli({in, "-preserveLinks", "On", "-rotexp", "R", "turned.pdf"});
    CHECK_EQUAL(linkDestinations("turned.pdf", 1), std::string("[2 /FitV 500, 2 /FitR 200 295 400 495]"));
    // Each page shown at full size, then at half size
    runCli({in, "-preserveLinks", "On", "-shuffle", "1:!+1N1+0%+0%,!+1N0.5+0%+0%", "twice.pdf"});
    CHECK_EQUAL(linkDestinations("twice.pdf", 1), std::string("[3 /FitH 500, 3 /FitR 100 200 300 400]"));
    CHECK_EQUAL(linkDestinations("twice.pdf", 2), std::string("[3 /FitH 500, 3 /FitR 100 200 300 400]"));
}

TEST(main_linksWithNullXyzCoordinatesKeepThem) {
    // jPDF Tweak 1.2.2 failed on these under -preserveLinks; a null input gives a null output
    const auto in = linkFixture("in.pdf", 2, {"/XYZ null 700 0", "/XYZ 100 null null"});
    runCli({in, "-preserveLinks", "On", "-scale", "1190,1684", "scaled.pdf"});
    CHECK_EQUAL(linkDestinations("scaled.pdf", 1),
                std::string("[2 /XYZ null 1400 0, 2 /XYZ 200 null null]"));
    runCli({in, "-preserveLinks", "On", "-rotexp", "R", "turned.pdf"});
    CHECK_EQUAL(linkDestinations("turned.pdf", 1),
                std::string("[2 /XYZ 700 null 0, 2 /XYZ null 495 null]"));
}

TEST(main_rebuiltPagesKeepTheCatalog) {
    const auto in = catalogFixture();
    const std::string common =
        "attachment Note: Attached\n"
        "/PageMode: /UseOutlines\n/PageLayout: /TwoColumnLeft\n/Lang: en-GB\nHideToolbar: true\n";
    CHECK_EQUAL(catalogSummary(in),
                "bookmark XYZ: 2 /XYZ 100 700 0\nbookmark FitH: 3 /FitH 500\nbookmark FitR: 1 /FitR 100 "
                "200 300 400\n"
                "named named: 3 /XYZ 50 400 0\ndests /old: 1 /Fit\nopen: 2 /FitV 200\n" +
                    common + "labels: [i, ii, iii]\nhidden layer shared: true\n");
    // Pages 2 and 3 cropped by 50 from the left and 100 from the bottom
    setPageEntries(in, "boxed2.pdf", 2, {{"/CropBox", "[50 100 545 742]"}});
    setPageEntries("boxed2.pdf", "boxed.pdf", 3, {{"/CropBox", "[50 100 545 742]"}});
    runCli({"boxed.pdf", "-crop", "CropBox", "cropped.pdf"});
    CHECK_EQUAL(catalogSummary("cropped.pdf"),
                "bookmark XYZ: 2 /XYZ 50 600 0\nbookmark FitH: 3 /FitH 400\nbookmark FitR: 1 /FitR 100 200 "
                "300 400\n"
                "named named: 3 /XYZ 0 300 0\ndests /old: 1 /Fit\nopen: 2 /FitV 150\n" +
                    common + "labels: [i, ii, iii]\nhidden layer shared: true\n");
    runCli({in, "-scale", "1190,1684", "scaled.pdf"});
    CHECK_EQUAL(catalogSummary("scaled.pdf"),
                "bookmark XYZ: 2 /XYZ 200 1400 0\nbookmark FitH: 3 /FitH 1000\nbookmark FitR: 1 /FitR 200 "
                "400 600 800\n"
                "named named: 3 /XYZ 100 800 0\ndests /old: 1 /Fit\nopen: 2 /FitV 400\n" +
                    common + "labels: [i, ii, iii]\nhidden layer shared: true\n");
    // Turned by 90 degrees and drawn upright: x' = y, y' = 595 - x
    runCli({in, "-rotexp", "R", "turned.pdf"});
    CHECK_EQUAL(catalogSummary("turned.pdf"),
                "bookmark XYZ: 2 /XYZ 700 495 0\nbookmark FitH: 3 /FitV 500\nbookmark FitR: 1 /FitR 200 "
                "295 400 495\n"
                "named named: 3 /XYZ 400 545 0\ndests /old: 1 /Fit\nopen: 2 /FitH 395\n" +
                    common + "labels: [i, ii, iii]\nhidden layer shared: true\n");
    // Each page shown at full size, then at half size: destinations go to the first
    runCli({in, "-shuffle", "1:!+1N1+0%+0%,!+1N0.5+0%+0%", "twice.pdf"});
    CHECK_EQUAL(catalogSummary("twice.pdf"),
                "bookmark XYZ: 3 /XYZ 100 700 0\nbookmark FitH: 5 /FitH 500\nbookmark FitR: 1 /FitR 100 "
                "200 300 400\n"
                "named named: 5 /XYZ 50 400 0\ndests /old: 1 /Fit\nopen: 3 /FitV 200\n" +
                    common + "labels: null\nhidden layer shared: true\n");
    // Pages 1 and 3 only: what points at page 2 goes, the bookmark stays as a label
    runCli({in, "-shuffle", "2:!+1N1+0%+0%", "odd.pdf"});
    CHECK_EQUAL(catalogSummary("odd.pdf"),
                "bookmark XYZ: -\nbookmark FitH: 2 /FitH 500\nbookmark FitR: 1 /FitR 100 200 300 400\n"
                "named named: 2 /XYZ 50 400 0\ndests /old: 1 /Fit\nopen: -\n" +
                    common + "labels: null\nhidden layer shared: true\n");
}

TEST(main_shuffleRepeatsPerBlock) {
    const auto in = pdfOfSizes(".", "in.pdf", std::vector<std::pair<float, float>>(8, kA4));
    runCli({in, "-shuffle", "-4,4:!-4N1+0%+0%,!+1N1+0%+0%,!+2N1+0%+0%,!-3N1+0%+0%", "out.pdf"});
    CHECK_EQUAL(pageNumbers("out.pdf"), std::string("[4, 1, 2, 3, 8, 5, 6, 7]"));
}

TEST(main_shuffleScalesEachOffsetByItsOwnPercentFlag) {
    // An A4 page whose link points at itself, at the top-left corner.
    const auto in = fixture(".", "in.pdf", 1, 1, 1);
    runCli({in, "-preserveLinks", "On", "-shuffle", "1:!+1N1+0+50%", "absx-percenty.pdf"});
    CHECK_EQUAL(firstPlacement("absx-percenty.pdf"), std::string("1 0 0 1 0 421 cm"));
    const auto pdf = openPdf("absx-percenty.pdf");
    QPDFObjectHandle dest = QPDFPageDocumentHelper(*pdf)
                                .getAllPages()[0]
                                .getObjectHandle()
                                .getKey("/Annots")
                                .getArrayItem(0)
                                .getKey("/Dest");
    CHECK(approx(static_cast<float>(dest.getArrayItem(3).getNumericValue()), 842 + 421));
    runCli({in, "-shuffle", "1:!+1N1+50%+10", "percentx-absy.pdf"});
    CHECK_EQUAL(firstPlacement("percentx-absy.pdf"), std::string("1 0 0 1 297.5 10 cm"));
}

TEST(main_shuffleDropsLinksToPagesItLeavesOut) {
    // Page 1 links to page 2; the rule keeps the first page of every pair.
    const auto in = fixture(".", "in.pdf", 2, 1, 2);
    runCli({in, "-preserveLinks", "On", "-shuffle", "2:!+1N1+0%+0%", "out.pdf"});
    CHECK_EQUAL(pageCount("out.pdf"), 1);
    const auto pdf = openPdf("out.pdf");
    CHECK(!QPDFPageDocumentHelper(*pdf).getAllPages()[0].getObjectHandle().getKey("/Annots").isArray());
}

TEST(main_rotateAcceptsEveryDocumentedSpelling) {
    const auto in = fixture(".", "in.pdf", 1);
    for (const std::string option :
         {"-rot", "-rotate", "-rotexp", "-rotateexp", "-rotexplicitly", "-rotateexplicitly"}) {
        const std::filesystem::path out = option.substr(1) + ".pdf";
        runCli({in, option, "R", out});
        CHECK(std::filesystem::is_regular_file(out));
        const auto pdf = openPdf(out);
        QPDFObjectHandle page = QPDFPageDocumentHelper(*pdf).getAllPages()[0].getObjectHandle();
        CHECK_EQUAL(pdftweak::pdf::pageRotation(page), option.find("exp") != std::string::npos ? 0 : 90);
        const auto size = pdftweak::pdf::pageSizeWithRotation(page);
        CHECK(size.width() > size.height());
    }
}

TEST(main_rotationSurvivesLaterStepsOnOnePage) {
    // Each of these removes the /Rotate entry that -rot sets, keeping the appearance.
    const auto in = fixture(".", "in.pdf", 1);
    const std::vector<std::vector<Arg>> steps = {
        {"-rotexp", "R"}, {"-rot", "R", "-shuffle", "1:!+1N1+0%+0%"}, {"-rot", "R", "--", "-rotexp", "K"}};
    int i = 0;
    for (const auto& step : steps) {
        const std::filesystem::path out = "out" + std::to_string(i++) + ".pdf";
        std::vector<Arg> args{in};
        args.insert(args.end(), step.begin(), step.end());
        args.push_back(out);
        runCli(args);
        const auto pdf = openPdf(out);
        QPDFObjectHandle page = QPDFPageDocumentHelper(*pdf).getAllPages()[0].getObjectHandle();
        CHECK_EQUAL(pdftweak::pdf::pageRotation(page), 0);
        const auto size = pdftweak::pdf::pageSizeWithRotation(page);
        CHECK(size.width() > size.height());
    }
}

TEST(main_pageOperationsKeepInfoXmpAndFormsAfterACrop) {
    FixtureOptions options;
    options.info = {{"Title", "Kept title"}};
    options.xmp = true;
    options.textField = true;
    const auto in = writePdf(".", "in.pdf", {{595, 842, "Page 1"}}, options);
    runCli({in, "-crop", "MediaBox", "cropped.pdf"});
    const auto pdf = openPdf("cropped.pdf");
    CHECK_EQUAL(pdf->getTrailer().getKey("/Info").getKey("/Title").getUTF8Value(),
                std::string("Kept title"));
    CHECK(pdf->getRoot().getKey("/Metadata").isStream());
}

TEST(main_outputFlagsWithTemporaryFiles) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4, kA4});
    // Each step replaces the temporary file of the step before it
    runCli({in, "-crop", "MediaBox", "-rotexp", "R", "-ot", "temp.pdf"});
    CHECK_EQUAL(pageNumbers("temp.pdf"), std::string("[1, 2]"));
    CHECK(boxIs("temp.pdf", 2, 842, 595));
}
