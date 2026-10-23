#include "core/console.h"

#include <windows.h>

#include <string>

namespace pdftweak {
namespace {

void write(DWORD which, std::string_view text) {
    HANDLE handle = GetStdHandle(which);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE || text.empty())
        return;
    DWORD written = 0;
    DWORD mode = 0;
    if (GetFileType(handle) == FILE_TYPE_CHAR && GetConsoleMode(handle, &mode)) {
        const int size = static_cast<int>(text.size());
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0);
        std::wstring wide(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), size, wide.data(), length);
        WriteConsoleW(handle, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr);
    } else {
        WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    }
}

} // namespace

void writeOut(std::string_view utf8) { write(STD_OUTPUT_HANDLE, utf8); }
void writeErr(std::string_view utf8) { write(STD_ERROR_HANDLE, utf8); }

} // namespace pdftweak
