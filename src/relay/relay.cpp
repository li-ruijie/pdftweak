// pdftweak.com runs pdftweak.exe in the same folder with the same arguments and standard handles,
// waits for it, and returns its exit code. A shell resolves "pdftweak" to the .com first, so a
// command line waits for the GUI-subsystem program and its output reaches pipes and redirections.

#include <windows.h>

#include <cstdio>
#include <string>

namespace {

// The part of the command line after the program name, with its leading white space. The program
// name is either a quoted token or a token that ends at the first space or tab.
const wchar_t* argumentsOf(const wchar_t* commandLine) {
    const wchar_t* p = commandLine;
    if (*p == L'"') {
        ++p;
        while (*p != L'\0' && *p != L'"')
            ++p;
        if (*p == L'"')
            ++p;
    } else {
        while (*p != L'\0' && *p != L' ' && *p != L'\t')
            ++p;
    }
    return p;
}

void makeInheritable(DWORD which) {
    HANDLE handle = GetStdHandle(which);
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
        SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
}

int fail(const wchar_t* what) {
    std::fwprintf(stderr, L"pdftweak: %ls failed with error %lu\n", what, GetLastError());
    return 1;
}

} // namespace

int wmain() {
    std::wstring exe(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    if (length == 0 || length >= exe.size() || length < 4)
        return fail(L"GetModuleFileNameW");
    exe.resize(length);
    exe.replace(exe.size() - 4, 4, L".exe");
    std::wstring commandLine = L"\"" + exe + L"\"" + argumentsOf(GetCommandLineW());

    makeInheritable(STD_INPUT_HANDLE);
    makeInheritable(STD_OUTPUT_HANDLE);
    makeInheritable(STD_ERROR_HANDLE);
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    // The job ends the child when the relay ends, for example on Ctrl+C.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr)
        return fail(L"CreateJobObjectW");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits))
        return fail(L"SetInformationJobObject");

    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED,
                        nullptr, nullptr, &startup, &process))
        return fail(L"CreateProcessW");
    if (!AssignProcessToJobObject(job, process.hProcess)) {
        TerminateProcess(process.hProcess, 1);
        return fail(L"AssignProcessToJobObject");
    }
    ResumeThread(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(job);
    return static_cast<int>(exitCode);
}
