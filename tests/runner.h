#pragma once

// A minimal test runner, like the Java project's RegressionTests. A test is a function registered
// under a name of the form <group>_<description>. pdftweak-tests.exe runs every test of each group
// named on its command line, each in a fresh directory build\test-work\<name>.

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>

namespace pdftweak::test {

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

using TestFunction = void (*)();

struct Registration {
    Registration(const char* name, TestFunction function);
};

// The folder that holds pdftweak-tests.exe, pdftweak.exe, and pdftweak.com.
std::filesystem::path binaryFolder();

[[noreturn]] void fail(const char* file, int line, const std::string& message);

template <typename A, typename B>
void checkEqual(const A& actual, const B& expected, const char* text, const char* file, int line) {
    if (!(actual == expected)) {
        std::ostringstream message;
        message << text << ": expected <" << expected << "> but was <" << actual << ">";
        fail(file, line, message.str());
    }
}

} // namespace pdftweak::test

#define TEST(name)                                                                                         \
    static void name();                                                                                    \
    static const ::pdftweak::test::Registration registration_##name(#name, name);                          \
    static void name()

#define CHECK(condition)                                                                                   \
    do {                                                                                                   \
        if (!(condition))                                                                                  \
            ::pdftweak::test::fail(__FILE__, __LINE__, "CHECK(" #condition ")");                           \
    } while (false)

#define CHECK_EQUAL(actual, expected)                                                                      \
    ::pdftweak::test::checkEqual((actual), (expected), #actual, __FILE__, __LINE__)
