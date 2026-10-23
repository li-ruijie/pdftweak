// Ports of OperationTests.java: what each operation produces, checked by meaning rather than bytes.

#include "runner.h"

#include "cli_run.h"
#include "fixtures.h"
#include "inspect.h"

#include "core/pdf/input_file.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <filesystem>

using namespace pdftweak::test;

namespace {

const std::pair<float, float> kA4{595.0f, 842.0f};
const std::pair<float, float> kLandscapeA4{842.0f, 595.0f};

bool opens(const std::filesystem::path& pdf, const std::string& password = "") {
    try {
        openPdf(pdf, password);
        return true;
    } catch (const Failure&) {
        return false;
    }
}

int permissionValue(const std::filesystem::path& pdf, const std::string& password) {
    const auto qpdf = openPdf(pdf, password);
    int r = 0, p = 0;
    qpdf->isEncrypted(r, p);
    return p;
}

} // namespace

TEST(main_encryptionModesPasswordsAndPermissions) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4});
    // decision 4: R2 and R3 /P as Java writes them, R4 with bit 10 set
    const int expected[] = {-44, -3884, -3372};
    for (int mode = 0; mode <= 2; ++mode) {
        const std::filesystem::path out = "mode" + std::to_string(mode) + ".pdf";
        runCli({in, "-ownerpw", "owner", "-userpw", "user", "-encmode", std::to_string(mode),
                "-permissions", "Printing,Copy", out});
        CHECK(!opens(out));
        {
            const auto user = openPdf(out, "user");
            CHECK(!user->ownerPasswordMatched());
        }
        const pdftweak::pdf::InputFile owner(out, "owner");
        CHECK_EQUAL(owner.cryptoMode(), mode);
        const int permissions = owner.permissions();
        CHECK((permissions & 4) != 0);  // printing
        CHECK((permissions & 16) != 0); // copying
        CHECK((permissions & 8) == 0);  // no changes
        CHECK(owner.metadataEncrypted());
        CHECK_EQUAL(permissionValue(out, "owner"), expected[mode]);
        CHECK_EQUAL(cliError({out, "-info", "info.txt"}), std::string("Bad user password"));
    }
    runCli({in, "-ownerpw", "owner", "-encnometadata", "2", "nometadata.pdf"});
    const pdftweak::pdf::InputFile noMetadata("nometadata.pdf", "owner");
    CHECK(!noMetadata.metadataEncrypted());
    CHECK_EQUAL(noMetadata.cryptoMode(), 10);
    CHECK_EQUAL(cliError({in, "-ownerpw", "", "empty.pdf"}),
                std::string("Owner password may not be empty"));
    // iText's mode 3 is AES-256, revision 5
    runCli({in, "-ownerpw", "owner", "-encmode", "3", "aes256.pdf"});
    CHECK_EQUAL(pdftweak::pdf::InputFile("aes256.pdf", "owner").cryptoMode(), 3);
    CHECK_EQUAL(cliError({in, "-ownerpw", "owner", "-encmode", "4", "bad.pdf"}),
                std::string("No valid encryption mode"));
}

TEST(main_encryptedInputNeedsTheOwnerPassword) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4});
    runCli({in, "-ownerpw", "owner", "-userpw", "user", "-encmode", "1", "-permissions", "Printing",
            "encrypted.pdf"});
    CHECK_EQUAL(cliError({"-password", "user", "encrypted.pdf", "user.pdf"}),
                std::string("PdfReader not opened with owner password"));
    runCli({"-password", "owner", "encrypted.pdf", "-info", "info.txt"});
    const std::string text = readFile("info.txt");
    CHECK(text.find("ENCRYPTION 1 METADATA Printing") != std::string::npos);
    CHECK(text.find("OWNERPW owner") != std::string::npos);
    CHECK(text.find("USERPW user") != std::string::npos);
    runCli({"-password", "owner", "encrypted.pdf", "decrypted.pdf"});
    CHECK(!openPdf("decrypted.pdf")->isEncrypted());
}

TEST(main_pageRangesSelectReverseAndPad) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4, kA4, kA4, kA4, kA4, kA4});
    const std::pair<const char*, const char*> cases[] = {{"-i3-4", "[3, 4]"},
                                                         {"-i~2", "[5]"},
                                                         {"-i5-", "[5, 6]"},
                                                         {"-i4-2", "[4, 3, 2]"},
                                                         {"-ie", "[2, 4, 6]"},
                                                         {"-io", "[1, 3, 5]"},
                                                         {"-i+2", "[0, 0, 1, 2, 3, 4, 5, 6]"},
                                                         {"-i1-~3o+1", "[0, 1, 3]"}};
    int i = 0;
    for (const auto& [option, expected] : cases) {
        const std::filesystem::path out = "range" + std::to_string(i++) + ".pdf";
        runCli({option, in, out});
        CHECK_EQUAL(pageNumbers(out), std::string(expected));
    }
}

TEST(main_inputFilesAliasesAndInterleaving) {
    const auto a = pdfWithPages(".", "a.pdf", "A", 2);
    const auto b = pdfWithPages(".", "b.pdf", "B", 1);
    runCli({"-o", "joined.pdf", a, b});
    CHECK_EQUAL(pageTexts("joined.pdf"), std::string("[A1, A2, B1]"));
    runCli({"-o", "mixed.pdf", "-i=A", a, "-i=B", b, "-i", "B", "-i1", "A", "-i", "B"});
    CHECK_EQUAL(pageTexts("mixed.pdf"), std::string("[B1, A1, B1]"));
    runCli({"-interleave", "1", "-o", "interleaved.pdf", a, b});
    // a blank page for the shorter file
    CHECK_EQUAL(pageTexts("interleaved.pdf"), std::string("[A1, B1, A2, ]"));
}

TEST(main_outputFlags) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4, kA4});
    runCli({in, "plain.pdf"});
    runCli({in, "-ou", "uncompressed.pdf"});
    runCli({in, "-oum", "marked.pdf"});
    runCli({"marked.pdf", "-om", "unmarked.pdf"});
    {
        const auto qpdf = openPdf("uncompressed.pdf");
        QPDFObjectHandle contents =
            QPDFPageDocumentHelper(*qpdf).getAllPages()[0].getObjectHandle().getKey("/Contents");
        CHECK(contents.isStream() && !contents.getDict().hasKey("/Filter"));
    }
    for (int page = 1; page <= 2; ++page) {
        CHECK_EQUAL(pageEntry("marked.pdf", page, "pdftk_PageNum"), page);
        CHECK_EQUAL(pageEntry("unmarked.pdf", page, "pdftk_PageNum"), -1);
    }
    runCli({in, "-oc", "compressed.pdf"});
    CHECK(readFile("compressed.pdf").find("/ObjStm") != std::string::npos);
    CHECK(readFile("plain.pdf").find("/ObjStm") == std::string::npos);

    // Two copies of one file, so that the result holds each content stream and font twice.
    const auto same = pdfOfSizes(".", "same.pdf", {kA4, kA4});
    std::filesystem::copy_file(same, "copy.pdf");
    runCli({"-o", "twice.pdf", same, "copy.pdf"});
    runCli({"twice.pdf", "-os", "optimized.pdf"});
    CHECK_EQUAL(pageNumbers("optimized.pdf"), std::string("[1, 2, 1, 2]"));
    CHECK(objectCount("optimized.pdf") < objectCount("twice.pdf"));
}

TEST(main_outputNamesUseVariablesAndLabels) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4, kA4});
    runCli({in, "<P>\\<F>-copy.pdf"});
    CHECK(std::filesystem::is_regular_file("in-copy.pdf"));
    runCli({in, "<P>\\<FX>.copy"});
    CHECK(std::filesystem::is_regular_file("in.pdf.copy"));
    runCli({in, "<P>\\n<#>.pdf"});
    runCli({in, "<P>\\n<#>.pdf"});
    CHECK(std::filesystem::is_regular_file("n1.pdf") && std::filesystem::is_regular_file("n2.pdf"));
    {
        // page labels a and b, which -pagenumbers "1=1a" would set
        QPDF pdf;
        pdf.processFile("in.pdf");
        pdf.getRoot().replaceKey("/PageLabels", QPDFObjectHandle::parse("<< /Nums [0 << /S /a >>] >>"));
        QPDFWriter writer(pdf, "labelled.pdf");
        writer.write();
    }
    runCli({"labelled.pdf", "-ob", std::filesystem::absolute("page-*.pdf")});
    CHECK(std::filesystem::is_regular_file("page-a.pdf") && std::filesystem::is_regular_file("page-b.pdf"));
    CHECK_EQUAL(cliError({in, in}), std::string("Output file must be different from input file(s)"));
    CHECK_EQUAL(cliError({in, "-ob", std::filesystem::absolute("nostar.pdf")}),
                std::string("Output filename does not contain *"));
}

TEST(main_documentInfoIsChangedAndXmpDropped) {
    FixtureOptions options;
    options.info = {{"Title", "Old title"}, {"Author", "Someone"}};
    options.xmp = true;
    const auto in = writePdf(".", "in.pdf", {{595, 842, "Page 1"}}, options);
    runCli({in, "-docinfo", "Title=Report über", "-docinfo", "Author=", "out.pdf"});
    const auto qpdf = openPdf("out.pdf");
    QPDFObjectHandle dictionary = qpdf->getTrailer().getKey("/Info");
    CHECK_EQUAL(dictionary.getKey("/Title").getUTF8Value(), std::string("Report über"));
    CHECK(!dictionary.hasKey("/Author"));
    CHECK(!qpdf->getRoot().hasKey("/Metadata"));
    CHECK(info("out.pdf", "-info").find("DOCINFO Title=Report über") != std::string::npos);
}

TEST(main_pageOperationsKeepInfoXmpAndForms) {
    FixtureOptions options;
    options.info = {{"Title", "Kept title"}};
    options.xmp = true;
    options.textField = true;
    const auto in = writePdf(".", "in.pdf", {{595, 842, "Page 1"}}, options);
    runCli({"-o", "copied.pdf", in});
    const auto qpdf = openPdf("copied.pdf");
    QPDFObjectHandle fields = qpdf->getRoot().getKey("/AcroForm").getKey("/Fields");
    CHECK(fields.isArray() && fields.getArrayNItems() == 1);
    CHECK_EQUAL(fields.getArrayItem(0).getKey("/V").getUTF8Value(), std::string("value"));
    CHECK_EQUAL(qpdf->getTrailer().getKey("/Info").getKey("/Title").getUTF8Value(),
                std::string("Kept title"));
    CHECK(qpdf->getRoot().getKey("/Metadata").isStream());
}
