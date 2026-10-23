#include "process.h"

#include <windows.h>

#include <stdexcept>
#include <thread>

namespace pdftweak::test {
namespace {

std::string readAll(HANDLE pipe) {
    std::string text;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(pipe, buffer, sizeof buffer, &read, nullptr) && read > 0)
        text.append(buffer, read);
    return text;
}

} // namespace

ProcessResult runProcess(const std::filesystem::path& program, const std::wstring& arguments) {
    SECURITY_ATTRIBUTES inherit{sizeof inherit, nullptr, TRUE};
    HANDLE outRead, outWrite, errRead, errWrite;
    if (!CreatePipe(&outRead, &outWrite, &inherit, 0) || !CreatePipe(&errRead, &errWrite, &inherit, 0))
        throw std::runtime_error("CreatePipe failed");
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = outWrite;
    startup.hStdError = errWrite;
    std::wstring commandLine = L"\"" + program.wstring() + L"\" " + arguments;
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(program.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    if (!started) {
        CloseHandle(outRead);
        CloseHandle(errRead);
        throw std::runtime_error("CreateProcessW failed for " + program.string());
    }

    ProcessResult result;
    std::thread errReader([&] { result.err = readAll(errRead); });
    result.out = readAll(outRead);
    errReader.join();
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &result.exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(outRead);
    CloseHandle(errRead);
    return result;
}

} // namespace pdftweak::test
