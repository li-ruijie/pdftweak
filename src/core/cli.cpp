#include "core/cli.h"

#include "core/console.h"
#include "core/version.h"

namespace pdftweak {

int runCommandLine(std::span<const std::wstring_view> args) {
    for (const std::wstring_view arg : args) {
        if (arg == L"-v" || arg == L"-version") {
            writeOut(PDFTWEAK_PROGRAM_NAME " " PDFTWEAK_VERSION_STRING "\r\n");
            return 0;
        }
    }
    writeErr("This build of " PDFTWEAK_PROGRAM_NAME " supports only -version.\r\n");
    return 1;
}

} // namespace pdftweak
