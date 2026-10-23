// Checks that pdftweak.com runs pdftweak.exe with its arguments, passes its output through, and
// returns its exit code.

#include "runner.h"

#include "process.h"

#include <string>

TEST(main_relayPassesOutputAndSuccess) {
    const auto result =
        pdftweak::test::runProcess(pdftweak::test::binaryFolder() / "pdftweak.com", L"-version");
    CHECK_EQUAL(result.exitCode, 0ul);
    CHECK_EQUAL(result.out, std::string("PDF Tweak 2.0.0\r\n"));
    CHECK_EQUAL(result.err, std::string(""));
}

TEST(main_relayPassesErrorsAndFailure) {
    const auto result = pdftweak::test::runProcess(pdftweak::test::binaryFolder() / "pdftweak.com",
                                                   L"\"an argument with spaces\"");
    CHECK_EQUAL(result.exitCode, 1ul);
    CHECK_EQUAL(result.out, std::string(""));
    CHECK(result.err.find("-version") != std::string::npos);
}
