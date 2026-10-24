// Ports of RegressionTests.java: each pins a defect an earlier version of jPDF Tweak had.

#include "runner.h"

#include "cli_run.h"
#include "fixtures.h"
#include "inspect.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <filesystem>

using namespace pdftweak::test;

TEST(main_copyKeepsPageCount) {
    const auto in = fixture(".", "in.pdf", 3);
    runCli({in, "out.pdf"});
    CHECK_EQUAL(pageCount("out.pdf"), 3);
}

TEST(main_burstWithoutTransformations) {
    const auto in = fixture(".", "in.pdf", 2);
    runCli({in, "-ob", std::filesystem::absolute("b*.pdf")});
    for (int page = 1; page <= 2; ++page) {
        const std::filesystem::path part = "b" + std::to_string(page) + ".pdf";
        CHECK(std::filesystem::is_regular_file(part));
        CHECK_EQUAL(pageCount(part), 1);
    }
}

TEST(main_infoOmitsAnEmptyPageLabelPrefix) {
    const auto plain = fixture(".", "plain.pdf", 2);
    {
        QPDF pdf;
        pdf.processFile(plain.string().c_str());
        pdf.getRoot().replaceKey("/PageLabels",
                                 QPDFObjectHandle::parse("<< /Nums [0 << /S /D /P () >>] >>"));
        QPDFWriter writer(pdf, "in.pdf");
        writer.write();
    }
    CHECK(info("in.pdf", "-info").find("PAGELABELS 1=1") != std::string::npos);
}

TEST(main_moreInfoPageSizeRangesDoNotOverlap) {
    const auto in = pdfOfSizes(".", "in.pdf", {{595.0f, 842.0f}, {595.0f, 842.0f}, {842.0f, 595.0f}});
    const std::string text = info(in, "-moreinfo");
    CHECK(text.find("PAGESIZE 1-2 595.0x842.0") != std::string::npos);
    CHECK(text.find("PAGESIZE 3-3 842.0x595.0") != std::string::npos);
}

TEST(main_helpTextIsAccurate) {
    const std::string encmode = help("-encmode");
    CHECK(encmode.find(" -encmode {MODE}\n -encnometadata {MODE}\n    Set encryption mode.") !=
          std::string::npos);
    CHECK(help("-permissions").find(" -permissions {PERMISSION}") != std::string::npos);
    const std::string output = help("-o");
    CHECK(output.find("-oi") != std::string::npos);
    CHECK(output.find("remove") != std::string::npos);
}

// RegressionTests.main_uncompressedOutputAfterAnOption: in jPDF Tweak 1.3.1 any option made -ou
// compress again, as it set up the image export that turned the flag off.
TEST(main_uncompressedOutputAfterAnOption) {
    const auto in = fixture(".", "in.pdf", 1);
    runCli({in, "-watermark", "DRAFT,20,0.5", "-ou", "out.pdf"});
    CHECK_EQUAL(streamFilters("out.pdf"), std::string("[none]"));
}
