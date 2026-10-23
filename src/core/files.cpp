#include "core/files.h"

#include "core/error.h"
#include "core/java_text.h"
#include "core/text.h"

#include <windows.h>

#include <fstream>
#include <iterator>

namespace pdftweak {

std::string systemMessage(unsigned long error) {
    wchar_t* buffer = nullptr;
    const DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                          FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text = size == 0 ? std::wstring() : std::wstring(buffer, size);
    LocalFree(buffer);
    while (!text.empty() &&
           (text.back() == L'\r' || text.back() == L'\n' || text.back() == L'.' || text.back() == L' '))
        text.pop_back();
    if (text.empty())
        return "Error " + std::to_string(error);
    return toUtf8(text);
}

void checkCanonical(const std::filesystem::path& path) {
    const std::wstring text = path.wstring();
    if (text.find_first_of(L"*?") != std::wstring::npos)
        throw Error(Error::Kind::Io, "Bad pathname");
    WIN32_FIND_DATAW data;
    const HANDLE found = FindFirstFileW(std::filesystem::absolute(path).c_str(), &data);
    if (found != INVALID_HANDLE_VALUE) {
        FindClose(found);
        return;
    }
    const DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
        throw Error(Error::Kind::Io, systemMessage(error));
}

void createForWriting(const std::filesystem::path& path, const std::string& name) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw Error(Error::Kind::Io, name + " (" + systemMessage(GetLastError()) + ")");
    CloseHandle(file);
}

std::vector<std::string> readLines(const std::string& file) {
    std::ifstream in(std::filesystem::path(fromUtf8(file)), std::ios::binary);
    if (!in)
        throw Error(Error::Kind::Io, file + " (The system cannot find the file specified)");
    const std::string text =
        javaUtf8Decode(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
    std::vector<std::string> lines;
    std::string line;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\n' || c == '\r') {
            lines.push_back(line);
            line.clear();
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
        } else {
            line += c;
        }
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

} // namespace pdftweak
