#include "runner.h"

#include <windows.h>

#include <cstdio>
#include <exception>
#include <string_view>
#include <vector>

namespace pdftweak::test {
namespace {

struct Entry {
    std::string name;
    TestFunction function;
};

std::vector<Entry>& registry() {
    static std::vector<Entry> entries;
    return entries;
}

} // namespace

Registration::Registration(const char* name, TestFunction function) {
    registry().push_back({name, function});
}

std::filesystem::path binaryFolder() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

void fail(const char* file, int line, const std::string& message) {
    throw Failure(std::string(file) + "(" + std::to_string(line) + "): " + message);
}

} // namespace pdftweak::test

int main(int argc, char** argv) {
    using namespace pdftweak::test;
    if (argc < 2) {
        std::fprintf(stderr, "Usage: pdftweak-tests <group> [<group> ...]\n"
                             "Groups: unit, main, parity, gui\n");
        return 2;
    }
    // build\x64\<Configuration>\pdftweak-tests.exe -> build\test-work
    const std::filesystem::path work = binaryFolder().parent_path().parent_path() / "test-work";
    int run = 0, failed = 0, skipped = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string prefix = std::string(argv[i]) + "_";
        for (const auto& [name, function] : registry()) {
            if (!std::string_view(name).starts_with(prefix))
                continue;
            ++run;
            const std::filesystem::path folder = work / name;
            std::error_code ignored;
            std::filesystem::remove_all(folder, ignored);
            std::filesystem::create_directories(folder);
            std::filesystem::current_path(folder);
            try {
                function();
                std::printf("PASS %s\n", name.c_str());
            } catch (const Skipped& e) {
                ++skipped;
                std::printf("SKIP %s\n     %s\n", name.c_str(), e.what());
            } catch (const std::exception& e) {
                ++failed;
                std::printf("FAIL %s\n     %s\n", name.c_str(), e.what());
            }
        }
    }
    if (skipped == 0)
        std::printf("%d run, %d failed\n", run, failed);
    else
        std::printf("%d run, %d failed, %d skipped\n", run, failed, skipped);
    if (run == 0) {
        std::fprintf(stderr, "No test matched the groups given.\n");
        return 2;
    }
    return failed == 0 ? 0 : 1;
}
