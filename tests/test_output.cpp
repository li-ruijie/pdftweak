// Tests of the command line and output decisions of the M2 plan (decisions 4 and 9 to 15), which
// have no counterpart in the Java tests.

#include "runner.h"

#include "cli_run.h"
#include "fixtures.h"
#include "inspect.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <filesystem>
#include <set>

using namespace pdftweak::test;

namespace {

const char kProducerNote[] = "; modified by PDF Tweak 2.0.0 (based on qpdf 12.4.2 and MuPDF 1.28.5)";
const char kProducer[] = "PDF Tweak 2.0.0 (based on qpdf 12.4.2 and MuPDF 1.28.5)";

// A one-page PDF written by hand, so that its Info keys come in the order given.
std::filesystem::path handWrittenPdf(const std::string& name, const std::string& info) {
    std::string pdf = "%PDF-1.4\n";
    std::vector<size_t> offsets;
    const std::string objects[] = {"<< /Type /Catalog /Pages 2 0 R >>",
                                   "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                                   "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] >>", info};
    for (size_t i = 0; i < 4; ++i) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const size_t xref = pdf.size();
    pdf += "xref\n0 5\n0000000000 65535 f \n";
    for (const size_t offset : offsets) {
        char line[24];
        std::snprintf(line, sizeof line, "%010zu 00000 n \n", offset);
        pdf += line;
    }
    pdf +=
        "trailer\n<< /Size 5 /Root 1 0 R /Info 4 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    writeFile(name, pdf);
    return name;
}

// The same with the Info dictionary in an object stream, which needs a cross-reference stream.
std::filesystem::path handWrittenStreamPdf(const std::string& name, const std::string& info) {
    std::string pdf = "%PDF-1.5\n";
    std::vector<size_t> offsets;
    const std::string objects[] = {"<< /Type /Catalog /Pages 2 0 R >>",
                                   "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                                   "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] >>"};
    for (size_t i = 0; i < 3; ++i) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const std::string header = "4 0 ";
    const std::string content = header + info;
    offsets.push_back(pdf.size()); // object 5, the object stream
    pdf += "5 0 obj\n<< /Type /ObjStm /N 1 /First " + std::to_string(header.size()) + " /Length " +
           std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream\nendobj\n";
    const size_t xref = pdf.size();
    // entries of 1, 4, and 2 bytes: type, offset or object stream, generation or index
    std::string entries;
    const auto entry = [&](int type, size_t field, int index) {
        entries += static_cast<char>(type);
        for (int shift = 24; shift >= 0; shift -= 8)
            entries += static_cast<char>((field >> shift) & 0xFF);
        entries += static_cast<char>((index >> 8) & 0xFF);
        entries += static_cast<char>(index & 0xFF);
    };
    entry(0, 0, 65535);
    entry(1, offsets[0], 0);
    entry(1, offsets[1], 0);
    entry(1, offsets[2], 0);
    entry(2, 5, 0);
    entry(1, offsets[3], 0);
    entry(1, xref, 0);
    pdf += "6 0 obj\n<< /Type /XRef /Size 7 /W [1 4 2] /Root 1 0 R /Info 4 0 R /Length " +
           std::to_string(entries.size()) + " >>\nstream\n" + entries + "\nendstream\nendobj\nstartxref\n" +
           std::to_string(xref) + "\n%%EOF\n";
    writeFile(name, pdf);
    return name;
}

std::string infoValue(const std::filesystem::path& pdf, const std::string& key) {
    QPDFObjectHandle value = openPdf(pdf)->getTrailer().getKey("/Info").getKey(key);
    return value.isString() ? value.getUTF8Value() : std::string("(none)");
}

} // namespace

TEST(main_queryRunsWithoutOutputExitZero) {
    const auto in = fixture(".", "in.pdf", 1);
    // decision 14
    CliResult result = runCliCapture({in, "-info", "-"});
    CHECK_EQUAL(result.exitCode, 0);
    CHECK(result.out.starts_with("PAGECOUNT 1\r\nENCRYPTION -1\r\n"));
    CHECK_EQUAL(result.err, std::string("Cannot write PDF: No output file.\r\n"));
    result = runCliCapture({in, "-docinfo", "Title=x"});
    CHECK_EQUAL(result.exitCode, 1);
    CHECK_EQUAL(result.err, std::string("Cannot write PDF: No output file.\r\n"));
    result = runCliCapture({in, "-nosuchoption", "x", "out.pdf"});
    CHECK_EQUAL(result.exitCode, 1);
    CHECK_EQUAL(result.err, std::string("Unknown option: -nosuchoption\r\n"));
    result = runCliCapture({in, "-docinfo"});
    CHECK_EQUAL(result.exitCode, 1);
    CHECK_EQUAL(result.err, std::string("Option without parameters: -docinfo\r\n"));
    result = runCliCapture({in, "-docinfo", "Title", "out.pdf"});
    CHECK_EQUAL(result.exitCode, 1);
    CHECK_EQUAL(result.err, std::string("Missing equals sign in -docinfo value: Title\r\n"));
    result = runCliCapture({"-o", "out.pdf"});
    CHECK_EQUAL(result.exitCode, 1);
    CHECK_EQUAL(result.err, std::string("No input files.\r\n"));
    // decision 9
    result = runCliCapture({"missing.pdf", "out.pdf"});
    CHECK_EQUAL(result.exitCode, 1);
    CHECK(result.err.starts_with("Error: ") &&
          result.err.ends_with("\\missing.pdf not found as file or resource.\r\n"));
    result = runCliCapture({in, "out.pdf"});
    CHECK_EQUAL(result.exitCode, 0);
    CHECK_EQUAL(result.err, std::string("Output file written successfully.\r\n"));
    result = runCliCapture({in, "-permissions", "Printing,Flying", "-ownerpw", "o", "perm.pdf"});
    CHECK_EQUAL(result.exitCode, 0);
    CHECK_EQUAL(
        result.err,
        std::string("Skipping invalid permission: Flying\r\nOutput file written successfully.\r\n"));
    CHECK_EQUAL(runCliCapture({"-version"}).out, std::string("PDF Tweak 2.0.0\r\n"));
}

TEST(main_infoPrintsKeysInJavaOrderWithTheProducerNote) {
    // decision 12: the order of the file decides between keys of one HashMap bucket
    const auto titleFirst = handWrittenPdf("title-first.pdf", "<< /Title (T) /Author (A) /Producer (P) >>");
    CliResult result = runCliCapture({titleFirst, "-info", "-"});
    CHECK(result.out.find(std::string("DOCINFO Producer=P") + kProducerNote +
                          "\r\nDOCINFO Title=T\r\nDOCINFO Author=A\r\n") != std::string::npos);
    const auto authorFirst = handWrittenPdf("author-first.pdf", "<< /Author (A) /Title (T) >>");
    result = runCliCapture({authorFirst, "-info", "-"});
    CHECK(result.out.find("DOCINFO Author=A\r\nDOCINFO Title=T\r\n") != std::string::npos);
    // decision 10: the note is added once, and nothing is added without a Producer
    CHECK(result.out.find("Producer") == std::string::npos);
    const auto marked =
        handWrittenPdf("marked.pdf", std::string("<< /Producer (P") + kProducerNote + ") >>");
    result = runCliCapture({marked, "-info", "-"});
    CHECK(result.out.find(std::string("DOCINFO Producer=P") + kProducerNote + "\r\n") != std::string::npos);
    // the order comes from an Info dictionary in an object stream too
    const auto streams = handWrittenStreamPdf("streams.pdf", "<< /Title (T) /Author (A) >>");
    CHECK(openPdf(streams)->getXRefTable().at(QPDFObjGen(4, 0)).getType() == 2);
    result = runCliCapture({streams, "-info", "-"});
    CHECK(result.out.find("DOCINFO Title=T\r\nDOCINFO Author=A\r\n") != std::string::npos);
}

TEST(main_outputSetsModDateAndSuppliesAMissingProducer) {
    const auto in = fixture(".", "in.pdf", 2);
    runCli({in, "out.pdf"});
    // decision 10: Producer kept, ModDate renewed, CreationDate kept
    CHECK(infoValue("out.pdf", "/Producer").starts_with("iText"));
    CHECK(infoValue("out.pdf", "/ModDate") != std::string("D:20261023120000+08'00'"));
    CHECK_EQUAL(infoValue("out.pdf", "/CreationDate"), std::string("D:20261023120000+08'00'"));
    const auto bare = handWrittenPdf("bare.pdf", "<< /Title (T) >>");
    runCli({bare, "bare-out.pdf"});
    CHECK_EQUAL(infoValue("bare-out.pdf", "/Producer"), std::string(kProducer));
    CHECK_EQUAL(infoValue("bare-out.pdf", "/CreationDate"), std::string("(none)"));
    // a new document gets a CreationDate
    runCli({"-o", "merged.pdf", bare});
    CHECK(infoValue("merged.pdf", "/CreationDate").starts_with("D:"));
    CHECK_EQUAL(infoValue("merged.pdf", "/Title"), std::string("T"));
    // burst pages carry Producer, CreationDate, and ModDate only, without the source's XMP
    FixtureOptions options;
    options.info = {{"Title", "Gone"}};
    options.xmp = true;
    const auto rich = writePdf(".", "rich.pdf", {{595, 842, "Page 1"}}, options);
    runCli({rich, "-ob", std::filesystem::absolute("burst*.pdf")});
    const auto page = openPdf("burst1.pdf");
    QPDFObjectHandle info = page->getTrailer().getKey("/Info");
    std::set<std::string> keys;
    for (const std::string& key : info.getKeys())
        keys.insert(key);
    CHECK(keys == (std::set<std::string>{"/CreationDate", "/ModDate", "/Producer"}));
    CHECK_EQUAL(info.getKey("/Producer").getUTF8Value(), std::string(kProducer));
    CHECK(!page->getRoot().hasKey("/Metadata"));
}

TEST(main_outputUpdatesExistingXmpProperties) {
    FixtureOptions options;
    options.xmp = true;
    options.xmpPacket = "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF "
                        "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"><rdf:Description "
                        "rdf:about=\"\" xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\" "
                        "xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"><pdf:Producer>old</pdf:Producer>"
                        "<xmp:ModifyDate>2001-01-01T00:00:00Z</xmp:ModifyDate></rdf:Description></rdf:RDF>"
                        "</x:xmpmeta>";
    const auto in = writePdf(".", "in.pdf", {{595, 842, "Page 1"}}, options);
    runCli({in, "out.pdf"});
    const auto pdf = openPdf("out.pdf");
    const std::shared_ptr<Buffer> data =
        pdf->getRoot().getKey("/Metadata").getStreamData(qpdf_dl_generalized);
    const std::string packet(reinterpret_cast<const char*>(data->getBuffer()), data->getSize());
    CHECK(packet.find("<pdf:Producer>iText") != std::string::npos);
    CHECK(packet.find("2001-01-01") == std::string::npos);
    const std::string modDate = infoValue("out.pdf", "/ModDate");
    // D:YYYYMMDDHHmmSS... becomes YYYY-MM-DDTHH:mm:SS
    const std::string w3c = modDate.substr(2, 4) + "-" + modDate.substr(6, 2) + "-" + modDate.substr(8, 2) +
                            "T" + modDate.substr(10, 2) + ":" + modDate.substr(12, 2) + ":" +
                            modDate.substr(14, 2);
    CHECK(packet.find("<xmp:ModifyDate>" + w3c) != std::string::npos);
}

TEST(main_userPasswordPrintsAsJavaDecodesIt) {
    const auto in = fixture(".", "in.pdf", 1);
    // decision 15: Latin-1 bytes decoded as UTF-8 with Java's replacement rules
    runCli({in, "-ownerpw", "owner", "-userpw", "café", "-encmode", "1", "latin.pdf"});
    CHECK(
        runCliCapture({"-password", "owner", "latin.pdf", "-info", "-"}).out.find("    USERPW caf�\r\n") !=
        std::string::npos);
    runCli({in, "-ownerpw", "owner", "-userpw", "Ã©", "-encmode", "1", "utf8.pdf"});
    CHECK(runCliCapture({"-password", "owner", "utf8.pdf", "-info", "-"}).out.find("    USERPW é\r\n") !=
          std::string::npos);
}

TEST(main_namedDestinationsAreConsolidated) {
    // decision 11: links and bookmarks to named destinations point to pages after opening
    const auto plain = fixture(".", "plain.pdf", 2);
    {
        QPDF pdf;
        pdf.processFile(plain.string().c_str());
        const auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
        QPDFObjectHandle second = pages[1].getObjectHandle();
        QPDFObjectHandle root = pdf.getRoot();
        root.replaceKey("/Dests", QPDFObjectHandle::parse("<< /byname [null /Fit] >>"));
        root.getKey("/Dests").getKey("/byname").setArrayItem(0, second);
        QPDFObjectHandle tree =
            QPDFObjectHandle::parse("<< /Names [(bystring) << /D [null /XYZ 0 800 0] >>] >>");
        tree.getKey("/Names").getArrayItem(1).getKey("/D").setArrayItem(0, second);
        root.replaceKey("/Names", QPDFObjectHandle::parse("<< >>"));
        root.getKey("/Names").replaceKey("/Dests", tree);
        QPDFObjectHandle first = pages[0].getObjectHandle();
        first.replaceKey(
            "/Annots",
            QPDFObjectHandle::parse("[<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /Dest /byname >> "
                                    "<< /Type /Annot /Subtype /Link /Rect [0 20 10 30] "
                                    "/A << /S /GoTo /D (bystring) >> >> << /Type /Annot /Subtype /Link "
                                    "/Rect [0 40 10 50] /Dest /unknown >>]"));
        QPDFObjectHandle item =
            pdf.makeIndirectObject(QPDFObjectHandle::parse("<< /Title (Bookmark) /Dest /byname >>"));
        QPDFObjectHandle outlines =
            pdf.makeIndirectObject(QPDFObjectHandle::parse("<< /Type /Outlines /Count 1 >>"));
        outlines.replaceKey("/First", item);
        outlines.replaceKey("/Last", item);
        item.replaceKey("/Parent", outlines);
        root.replaceKey("/Outlines", outlines);
        QPDFWriter writer(pdf, "named.pdf");
        writer.write();
    }
    CHECK_EQUAL(runCliCapture({"named.pdf", "-savebookmarks", "-"}).out,
                std::string("1;O;Bookmark;2 Fit\r\n"));
    // the merge route keeps no /Dests or /Names, so the links survive only with explicit destinations
    runCli({"-o", "merged.pdf", "named.pdf"});
    const auto merged = openPdf("merged.pdf");
    const auto pages = QPDFPageDocumentHelper(*merged).getAllPages();
    QPDFObjectHandle annots = pages[0].getObjectHandle().getKey("/Annots");
    CHECK(annots.getArrayItem(0).getKey("/Dest").getArrayItem(0).getObjGen() ==
          pages[1].getObjectHandle().getObjGen());
    QPDFObjectHandle goTo = annots.getArrayItem(1).getKey("/A").getKey("/D");
    CHECK(goTo.isArray() && goTo.getArrayItem(1).getName() == "/XYZ");
    CHECK(goTo.getArrayItem(0).getObjGen() == pages[1].getObjectHandle().getObjGen());
    // a name that does not resolve stays as it is
    CHECK_EQUAL(annots.getArrayItem(2).getKey("/Dest").getName(), std::string("/unknown"));
}

TEST(main_tempFilesCarryTheMergedDocument) {
    const auto a = pdfWithPages(".", "a.pdf", "A", 2);
    const auto b = pdfWithPages(".", "b.pdf", "B", 1);
    runCli({"-ot", "temp.pdf", a, b});
    CHECK_EQUAL(pageTexts("temp.pdf"), std::string("[A1, A2, B1]"));
    runCli({"-ots", "temp-optimized.pdf", a, b});
    CHECK_EQUAL(pageTexts("temp-optimized.pdf"), std::string("[A1, A2, B1]"));
}

// Java on Windows canonicalised the output name before writing, which refuses wildcards and names
// Windows cannot look up, and reported an output it could not create as FileNotFoundException did.
TEST(main_outputNamesFailAsJavaDoesOnWindows) {
    const auto in = fixture(".", "in.pdf", 1);
    CHECK_EQUAL(cliError({in, "a*b.pdf"}), std::string("Bad pathname"));
    CHECK_EQUAL(cliError({in, "-oi", "multi*.tiff"}), std::string("Bad pathname"));
    CHECK_EQUAL(cliError({in, "a|b.pdf"}),
                std::string("The filename, directory name, or volume label syntax is incorrect"));
    CHECK_EQUAL(cliError({in, "nodir\\out.pdf"}),
                std::string("nodir\\out.pdf (The system cannot find the path specified)"));
    CHECK_EQUAL(cliError({in, "-info", "nodir\\info.txt"}),
                std::string("nodir\\info.txt (The system cannot find the path specified)"));
}
