// The parity group (plan section 2.8): each test runs one command line through pdftweak.com and
// through jPDF Tweak's jar, and compares the results by meaning. The jar, its libraries, and MuPDF's
// JNI library are copied once from the Java repository (PDFTWEAK_JAVA_REPO, by default
// C:\src\jpdftweak) into build\parity-java, so that a rebuild
// there does not change a run here. Both programs run in the test's folder, and "{o}" in an argument
// stands for "port" in the port's run and "java" in Java's.
//
// A document is compared through describe(), which lists the pages (boxes, rotation, text,
// annotations, and transitions) and the catalog (page labels, bookmarks, viewer settings,
// attachments, form fields, encryption, and document information without Producer and the dates),
// and through its pages rendered by MuPDF at 72 dpi, which must agree within 2 per channel on 99.5 %
// of the pixels. The differences that the README ChangeLog records are allowed where they apply.
// Without java.exe on PATH or the jar, every parity test is skipped.

#include "runner.h"

#include "describe.h"
#include "fixtures.h"
#include "inspect.h"
#include "process.h"

#include "core/formats/bookmark.h"
#include "core/formats/page_labels.h"
#include "core/mupdf_bridge.h"
#include "core/pdf/input_file.h"
#include "core/pdf/outline.h"
#include "core/text.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFNameTreeObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <objbase.h>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

using namespace pdftweak::test;

namespace {

namespace fs = std::filesystem;

const std::pair<float, float> kA4{595.0f, 842.0f};
const std::pair<float, float> kLandscape{842.0f, 595.0f};
const std::pair<float, float> kLetter{612.0f, 792.0f};

// ---- the Java side ----

fs::path javaExecutable() {
    wchar_t found[MAX_PATH];
    if (SearchPathW(nullptr, L"java.exe", nullptr, MAX_PATH, found, nullptr) == 0)
        throw Skipped("java.exe is not on PATH");
    return found;
}

// build\parity-java, filled from the Java repository on first use in this run.
const fs::path& javaFolder() {
    static fs::path folder;
    if (!folder.empty())
        return folder;
    wchar_t* configured = nullptr;
    size_t size = 0;
    fs::path repo = L"C:\\src\\jpdftweak";
    if (_wdupenv_s(&configured, &size, L"PDFTWEAK_JAVA_REPO") == 0 && configured != nullptr)
        repo = configured;
    free(configured);
    const fs::path target = binaryFolder().parent_path().parent_path() / "parity-java";
    fs::remove_all(target);
    fs::create_directories(target / "lib");
    if (!fs::is_regular_file(repo / "jpdftweak.jar"))
        throw Skipped("no jpdftweak.jar in " + repo.string() + " (set PDFTWEAK_JAVA_REPO)");
    fs::copy_file(repo / "jpdftweak.jar", target / "jpdftweak.jar");
    for (const auto& entry : fs::directory_iterator(repo / "lib"))
        if (entry.path().extension() == ".jar")
            fs::copy_file(entry.path(), target / "lib" / entry.path().filename());
    for (const auto& entry : fs::recursive_directory_iterator(repo / "build" / "natives"))
        if (entry.path().filename() == "mupdf_java64.dll")
            fs::copy_file(entry.path(), target / "mupdf_java64.dll", fs::copy_options::overwrite_existing);
    folder = target;
    return folder;
}

std::wstring quote(const std::string& utf8) {
    const std::wstring arg = pdftweak::fromUtf8(utf8);
    // The Windows java launcher expands unquoted wildcards into the files that match them, so an
    // argument with * or ? is quoted.
    if (!arg.empty() && arg.find_first_of(L" \t\"*?") == std::wstring::npos)
        return arg;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"')
            out.append(backslashes * 2 + 1, L'\\');
        else
            out.append(backslashes, L'\\');
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, L'\\');
    return out + L"\"";
}

std::vector<std::string> expand(const std::vector<std::string>& args, const std::string& side) {
    std::vector<std::string> result;
    for (std::string arg : args) {
        for (size_t at = arg.find("{o}"); at != std::string::npos; at = arg.find("{o}", at + side.size()))
            arg.replace(at, 3, side);
        result.push_back(arg);
    }
    return result;
}

std::wstring commandLine(const std::vector<std::string>& args) {
    std::wstring line;
    for (const std::string& arg : args)
        line += (line.empty() ? L"" : L" ") + quote(arg);
    return line;
}

// The outcome of one run: success, or the message of the failure.
struct Outcome {
    bool ok = false;
    std::string message;
    std::string err;
};

std::string firstLine(const std::string& text) {
    std::string line = text.substr(0, text.find_first_of("\r\n"));
    return line;
}

Outcome runPort(const std::vector<std::string>& args) {
    const ProcessResult result =
        runProcess(binaryFolder() / "pdftweak.com", commandLine(expand(args, "port")));
    Outcome outcome;
    outcome.ok = result.exitCode == 0;
    outcome.err = result.err;
    std::string message = firstLine(result.err);
    if (message.rfind("Error: ", 0) == 0)
        message = message.substr(7);
    outcome.message = message;
    return outcome;
}

// The jar's version from "JPDF Tweak x.y.z", as a number x * 10000 + y * 100 + z.
int javaVersion() {
    static int version = -1;
    if (version >= 0)
        return version;
    const ProcessResult result = runProcess(
        javaExecutable(),
        commandLine({"-jar", pdftweak::toUtf8((javaFolder() / "jpdftweak.jar").wstring()), "-version"}));
    int a = 0, b = 0, c = 0;
    const size_t at = result.out.find("Tweak ");
    if (at == std::string::npos || sscanf_s(result.out.c_str() + at + 6, "%d.%d.%d", &a, &b, &c) < 2)
        throw Failure("Cannot read the jar's version from: " + result.out);
    version = a * 10000 + b * 100 + c;
    return version;
}

Outcome runJava(const std::vector<std::string>& args) {
    // the port's baseline is jPDF Tweak 1.3.2, whose fixes older jars lack
    if (javaVersion() < 10302)
        throw Failure("The parity tests need jpdftweak.jar 1.3.2 or later.");
    std::vector<std::string> full = {"-jar", pdftweak::toUtf8((javaFolder() / "jpdftweak.jar").wstring())};
    const std::vector<std::string> rest = expand(args, "java");
    full.insert(full.end(), rest.begin(), rest.end());
    const ProcessResult result = runProcess(javaExecutable(), commandLine(full));
    Outcome outcome;
    outcome.err = result.err;
    const std::string marker = "Exception in thread \"main\" ";
    const size_t at = result.err.find(marker);
    if (at != std::string::npos) {
        std::string thrown = firstLine(result.err.substr(at + marker.size()));
        // the toString of the exception, whose class name the port leaves out except for the JDK's
        // own exceptions
        outcome.message = thrown;
        return outcome;
    }
    outcome.message = firstLine(result.err);
    outcome.ok = result.err.find("Output file written successfully.") != std::string::npos ||
                 result.err.empty() || outcome.message.rfind("Cannot write PDF: No output file.", 0) == 0;
    return outcome;
}

// The Java message without the exception class, unless the port keeps the class as Java's JDK
// exceptions need it.
bool sameMessage(const std::string& port, const std::string& java) {
    if (port == java)
        return true;
    const size_t colon = java.find(": ");
    return colon != std::string::npos && java.substr(colon + 2) == port;
}

struct Runs {
    Outcome port;
    Outcome java;
};

Runs runBoth(const std::vector<std::string>& args) {
    Runs runs{runPort(args), runJava(args)};
    return runs;
}

void requireSuccess(const Runs& runs) {
    if (!runs.port.ok)
        fail(__FILE__, __LINE__, "the port failed: " + runs.port.err);
    if (!runs.java.ok)
        fail(__FILE__, __LINE__, "Java failed: " + runs.java.err);
}

// The pages rendered at 72 dpi agree within 2 per channel on 99.5 % of the pixels. Pages listed in
// skip are left out.
void compareRendering(const fs::path& port, const fs::path& java, const std::vector<int>& skip = {}) {
    const pdftweak::mupdf::Renderer a(readFile(port));
    const pdftweak::mupdf::Renderer b(readFile(java));
    CHECK_EQUAL(a.pageCount(), b.pageCount());
    for (int i = 0; i < a.pageCount(); ++i) {
        if (std::find(skip.begin(), skip.end(), i + 1) != skip.end())
            continue;
        const auto pa = a.render(i, false, false);
        const auto pb = b.render(i, false, false);
        if (pa.width != pb.width || pa.height != pb.height)
            fail(__FILE__, __LINE__, "page " + std::to_string(i + 1) + " renders at a different size");
        size_t differing = 0;
        const size_t pixels = static_cast<size_t>(pa.width) * static_cast<size_t>(pa.height);
        for (size_t p = 0; p < pixels; ++p)
            for (int c = 0; c < 3; ++c)
                if (std::abs(pa.samples[p * 3 + c] - pb.samples[p * 3 + c]) > 2) {
                    ++differing;
                    break;
                }
        if (differing * 1000 > pixels * 5)
            fail(__FILE__, __LINE__,
                 "page " + std::to_string(i + 1) + " differs in " + std::to_string(differing) + " of " +
                     std::to_string(pixels) + " pixels");
    }
}

// Runs the command line on both sides and compares {o}.pdf.
void comparePdf(const std::vector<std::string>& args, const DescribeOptions& options = {},
                const std::vector<int>& skipRendering = {}) {
    const Runs runs = runBoth(args);
    requireSuccess(runs);
    DescribeOptions javaOptions = options;
    javaOptions.javaPermissions = true;
    CHECK_EQUAL(describe("port.pdf", options), describe("java.pdf", javaOptions));
    if (options.password.empty())
        compareRendering("port.pdf", "java.pdf", skipRendering);
}

void compareFailure(const std::vector<std::string>& args) {
    const Runs runs = runBoth(args);
    if (runs.port.ok)
        fail(__FILE__, __LINE__, "the port succeeded where Java failed with: " + runs.java.message);
    if (runs.java.ok)
        fail(__FILE__, __LINE__, "Java succeeded where the port failed with: " + runs.port.message);
    if (!sameMessage(runs.port.message, runs.java.message))
        fail(__FILE__, __LINE__,
             "the port says \"" + runs.port.message + "\" and Java \"" + runs.java.message + "\"");
    CHECK(!fs::exists("port.pdf") && !fs::exists("java.pdf"));
}

void compareText(const fs::path& port, const fs::path& java,
                 const std::vector<std::string>& maskedPrefixes = {}) {
    const auto lines = [&](const fs::path& file) {
        std::istringstream in(readFile(file));
        std::string line, result;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            bool masked = false;
            for (const std::string& prefix : maskedPrefixes)
                masked |= line.rfind(prefix, 0) == 0;
            result += (masked ? "<masked>" : line) + "\n";
        }
        return result;
    };
    CHECK_EQUAL(lines(port), lines(java));
}

// ---- images ----

struct Decoded {
    unsigned width = 0, height = 0;
    std::vector<BYTE> bgra;
};

Decoded decode(const fs::path& file, unsigned frame = 0) {
    using Microsoft::WRL::ComPtr;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ComPtr<IWICImagingFactory> factory;
    CHECK(SUCCEEDED(
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))));
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, &decoder)))
        fail(__FILE__, __LINE__, "cannot decode " + file.string());
    ComPtr<IWICBitmapFrameDecode> source;
    CHECK(SUCCEEDED(decoder->GetFrame(frame, &source)));
    ComPtr<IWICFormatConverter> converter;
    CHECK(SUCCEEDED(factory->CreateFormatConverter(&converter)));
    CHECK(
        SUCCEEDED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                        nullptr, 0, WICBitmapPaletteTypeCustom)));
    Decoded image;
    CHECK(SUCCEEDED(converter->GetSize(&image.width, &image.height)));
    image.bgra.resize(static_cast<size_t>(image.width) * image.height * 4);
    CHECK(SUCCEEDED(converter->CopyPixels(nullptr, image.width * 4, static_cast<UINT>(image.bgra.size()),
                                          image.bgra.data())));
    return image;
}

// Two image files show the same picture: the same size, and channels within tolerance on all but
// the given share of the pixels.
void compareImages(const fs::path& port, const fs::path& java, int tolerance, double share,
                   unsigned frame = 0) {
    const Decoded a = decode(port, frame);
    const Decoded b = decode(java, frame);
    CHECK_EQUAL(std::to_string(a.width) + "x" + std::to_string(a.height),
                std::to_string(b.width) + "x" + std::to_string(b.height));
    size_t differing = 0;
    for (size_t p = 0; p + 3 < a.bgra.size(); p += 4)
        for (int c = 0; c < 4; ++c)
            if (std::abs(a.bgra[p + c] - b.bgra[p + c]) > tolerance) {
                ++differing;
                break;
            }
    if (static_cast<double>(differing) > share * static_cast<double>(a.bgra.size() / 4))
        fail(__FILE__, __LINE__,
             port.string() + " differs from " + java.string() + " in " + std::to_string(differing) +
                 " pixels");
}

// ---- fixtures ----

// Four A4 pages saying "Page n", with a link on page 1 to page 3 and document information.
fs::path document() {
    FixtureOptions options;
    options.linkPage = 1;
    options.linkTarget = 3;
    options.info = {{"Title", "Parity"}, {"Author", "PDF Tweak tests"}, {"Subject", "Matrix"}};
    return writePdf(
        ".", "in.pdf",
        {{595, 842, "Page 1"}, {595, 842, "Page 2"}, {595, 842, "Page 3"}, {595, 842, "Page 4"}}, options);
}

fs::path mixedSizes() { return pdfOfSizes(".", "mixed.pdf", {kA4, kLandscape, kLetter}); }

} // namespace

// ---- documents ----

TEST(parity_copy) {
    document();
    comparePdf({"in.pdf", "{o}.pdf"});
}

TEST(parity_cropToEachBox) {
    document();
    setPageEntries("in.pdf", "boxed1.pdf", 2,
                   {{"/CropBox", "[50 100 545 742]"}, {"/TrimBox", "[60 110 535 732]"}});
    setPageEntries("boxed1.pdf", "boxed.pdf", 3,
                   {{"/ArtBox", "[70 120 525 722]"}, {"/BleedBox", "[40 90 555 752]"}});
    for (const std::string box : {"MediaBox", "CropBox", "BleedBox", "TrimBox", "ArtBox"}) {
        comparePdf({"boxed.pdf", "-crop", box, "{o}.pdf"});
        fs::remove("port.pdf");
        fs::remove("java.pdf");
    }
}

TEST(parity_rotateAndRotateExplicitly) {
    mixedSizes();
    setPageEntries("mixed.pdf", "turned.pdf", 2, {{"/Rotate", "90"}});
    for (const std::vector<std::string> args :
         {std::vector<std::string>{"mixed.pdf", "-rotate", "RL", "{o}.pdf"},
          std::vector<std::string>{"mixed.pdf", "-rot", "U", "{o}.pdf"},
          std::vector<std::string>{"turned.pdf", "-rotexp", "K", "{o}.pdf"},
          std::vector<std::string>{"mixed.pdf", "-rotateexplicitly", "R3", "{o}.pdf"}}) {
        comparePdf(args);
        fs::remove("port.pdf");
        fs::remove("java.pdf");
    }
}

TEST(parity_scale) {
    mixedSizes();
    for (const std::string value : {"595,842", "1190,1684,c", "595,842,r", "300,300,cr"}) {
        comparePdf({"mixed.pdf", "-scale", value, "{o}.pdf"});
        fs::remove("port.pdf");
        fs::remove("java.pdf");
    }
}

TEST(parity_shuffle) {
    document();
    for (const std::string rule :
         {"2:!+1N1+0%+0%", "1:!+1N1+0%+0%,!+1N0.5+0%+0%", "2:!+1L0.707+0%-100%,+2L.707+100%-100%F2",
          "-4:!-4N1+0%+0%,!+1N1+0%+0%,!+2N1+0%+0%,!-3N1+0%+0%", "1:!+1N1+50%+10"}) {
        comparePdf({"in.pdf", "-shuffle", rule, "{o}.pdf"});
        fs::remove("port.pdf");
        fs::remove("java.pdf");
    }
}

TEST(parity_preserveLinks) {
    document();
    for (const std::vector<std::string> args :
         {std::vector<std::string>{"in.pdf", "-preserveLinks", "On", "-scale", "1190,1684", "{o}.pdf"},
          std::vector<std::string>{"in.pdf", "-preserveLinks", "On", "-rotexp", "R", "{o}.pdf"},
          std::vector<std::string>{"in.pdf", "-preserveLinks", "On", "-shuffle", "2:!+1N1+0%+0%",
                                   "{o}.pdf"},
          std::vector<std::string>{"in.pdf", "-preserveLinks", "On", "-background", "bg.pdf", "{o}.pdf"}}) {
        pdfWithText(".", "bg.pdf", "Background");
        comparePdf(args);
        fs::remove("port.pdf");
        fs::remove("java.pdf");
    }
}

TEST(parity_backgroundWatermarkAndNumbers) {
    document();
    pdfWithText(".", "bg.pdf", "Background");
    setPageEntries("in.pdf", "turned.pdf", 2, {{"/Rotate", "90"}});
    for (const std::vector<std::string> args :
         {std::vector<std::string>{"in.pdf", "-background", "bg.pdf", "{o}.pdf"},
          std::vector<std::string>{"turned.pdf", "-watermark", "Draft copy,40,0.3,#336699", "{o}.pdf"},
          std::vector<std::string>{"turned.pdf", "-numbers", "OB,10,30,20,%1$d of %2$d", "{o}.pdf"},
          std::vector<std::string>{"in.pdf", "-numbers", "C,14,0,0", "{o}.pdf"},
          std::vector<std::string>{"in.pdf", "-pagenumbers", "1=Intro:1i", "--", "-numbers",
                                   "IT,9,40,30,%4$s (%3$d)", "{o}.pdf"}}) {
        comparePdf(args);
        fs::remove("port.pdf");
        fs::remove("java.pdf");
    }
    compareFailure({"in.pdf", "-numbers", "L,10,1,20", "{o}.pdf"});
}

TEST(parity_pageLabels) {
    document();
    writeFile("labels.txt", "1=1A\n2=Part :5\n4=1-\n");
    for (const std::vector<std::string> args :
         {std::vector<std::string>{"in.pdf", "-pagenumbers", "1=Intro:1i", "-pagenumbers", "3=1",
                                   "{o}.pdf"},
          std::vector<std::string>{"in.pdf", "-loadpagenumbers", "labels.txt", "{o}.pdf"},
          std::vector<std::string>{"in.pdf", "-pagenumbers", "-", "{o}.pdf"}}) {
        comparePdf(args);
        fs::remove("port.pdf");
        fs::remove("java.pdf");
    }
    compareFailure({"in.pdf", "-pagenumbers", "0=1", "{o}.pdf"});
}

TEST(parity_bookmarks) {
    document();
    writeFile("bookmarks.txt",
              "1;O;Part;1 Fit\n2;BI;Chapter \\3B one;2 XYZ 0 800 0\n2;;Section;3 FitH 500\n"
              "1;;Web;0;Action=\\22URI\\22 URI=\\22https://example.com/\\22 Color=\\220 0.5 1\\22\n");
    for (const std::vector<std::string> args :
         {std::vector<std::string>{"in.pdf", "-bookmark", "1;O;Part;1 Fit", "-bookmark", "2;B;Chapter;3",
                                   "{o}.pdf"},
          std::vector<std::string>{"in.pdf", "-loadbookmarks", "bookmarks.txt", "{o}.pdf"}}) {
        comparePdf(args);
        fs::remove("port.pdf");
        fs::remove("java.pdf");
    }
    runBoth({"in.pdf", "-loadbookmarks", "bookmarks.txt", "{o}-marked.pdf"});
    comparePdf({"port-marked.pdf", "-bookmark", "-", "{o}.pdf"});
    fs::remove("port.pdf");
    fs::remove("java.pdf");
    compareFailure({"in.pdf", "-bookmark", "2;;Too deep;1", "{o}.pdf"});
}

TEST(parity_attachments) {
    document();
    writeFile("note.txt", "hello");
    writeFile("data.csv", "a,b\n1,2\n");
    comparePdf({"in.pdf", "-attach", "note.txt", "-attach", "data.csv", "{o}.pdf"});
}

TEST(parity_transitions) {
    document();
    writeFile("trans.txt", "1:Out_Vertical_Split,2\n2-3:Inward_Box,1,4\n~1:Diagonal_Glitter,3\n");
    for (const std::vector<std::string> args :
         {std::vector<std::string>{"in.pdf", "-trans", "1-2:Dissolve,2,5", "-transition",
                                   "~1:Left-Right_Wipe,1", "{o}.pdf"},
          std::vector<std::string>{"in.pdf", "-transfile", "trans.txt", "{o}.pdf"},
          std::vector<std::string>{"in.pdf", "-transitionfile", "trans.txt", "{o}.pdf"}}) {
        comparePdf(args);
        fs::remove("port.pdf");
        fs::remove("java.pdf");
    }
}

TEST(parity_viewerPreferences) {
    document();
    comparePdf({"in.pdf", "-pagemode", "Thumbnails", "-pagelayout", "Two_Pages_Right", "-viewpref",
                "Direction=/R2L", "-viewpref", "Print_scaling=/None", "-viewpref", "Number_of_copies=3",
                "-viewpref", "Hide_Menubar=true", "{o}.pdf"});
}

TEST(parity_documentInformation) {
    document();
    comparePdf({"in.pdf", "-docinfo", "Title=New title", "-docinfo", "Author=", "-docinfo",
                "Keywords=a b c", "{o}.pdf"});
}

TEST(parity_encryption) {
    document();
    // modes 0 to 2, and 3 (AES 256) which only the help leaves out, each without metadata encryption too
    for (const std::string mode : {"0", "1", "2", "3"}) {
        for (const std::string option : {"-encmode", "-encnometadata"}) {
            if (mode == "0" && option == "-encnometadata")
                continue;
            DescribeOptions options;
            options.password = "owner";
            comparePdf({"in.pdf", "-ownerpw", "owner", "-userpw", "user", option, mode, "-permissions",
                        "Printing,Copy,ScreenReaders", "{o}.pdf"},
                       options);
            fs::remove("port.pdf");
            fs::remove("java.pdf");
        }
    }
}

TEST(parity_signing) {
    document();
    const fs::path keystore = pkcs12(".");
    for (const std::vector<std::string> args :
         {std::vector<std::string>{"in.pdf", "-keystore", keystore.string(), "-sign", "secret", "{o}.pdf"},
          std::vector<std::string>{"in.pdf", "-keystore", keystore.string(), "-keyalias", "mykey",
                                   "-certlevel", "2", "-signvisible", "secret", "{o}.pdf"}}) {
        // the visible signature's appearance is MuPDF's (ChangeLog), so page 1 is not rendered
        comparePdf(args, {}, {1});
        CHECK_EQUAL(verifySignature("port.pdf"), verifySignature("java.pdf"));
        fs::remove("port.pdf");
        fs::remove("java.pdf");
    }
}

// ---- input and output ----

TEST(parity_inputRangesInterleavingAndAliases) {
    pdfWithPages(".", "a.pdf", "A", 5);
    pdfWithPages(".", "b.pdf", "B", 3);
    for (const std::vector<std::string> args :
         {std::vector<std::string>{"-i", "a.pdf", "-i", "b.pdf", "-o", "{o}.pdf"},
          std::vector<std::string>{"-i2-4", "a.pdf", "-ie", "b.pdf", "-i~1", "a.pdf", "{o}.pdf"},
          std::vector<std::string>{"-i1-~2o+1", "a.pdf", "-i+2,1", "b.pdf", "{o}.pdf"},
          std::vector<std::string>{"-interleave", "2", "-i", "a.pdf", "-i", "b.pdf", "{o}.pdf"},
          std::vector<std::string>{"-o", "{o}.pdf", "-i=A", "a.pdf", "-i=B", "b.pdf", "-i", "B", "-i2-3",
                                   "A", "-i", "B"}}) {
        comparePdf(args);
        fs::remove("port.pdf");
        fs::remove("java.pdf");
    }
}

TEST(parity_encryptedInputNeedsTheOwnerPassword) {
    document();
    {
        QPDF pdf;
        pdf.processFile("in.pdf");
        QPDFWriter writer(pdf, "secret.pdf");
        writer.setR4EncryptionParametersInsecure("user", "owner", true, true, true, true, true, true,
                                                 qpdf_r3p_full, true, true);
        writer.write();
    }
    comparePdf({"-password", "owner", "secret.pdf", "{o}.pdf"});
    fs::remove("port.pdf");
    fs::remove("java.pdf");
    compareFailure({"-password", "user", "secret.pdf", "{o}.pdf"});
    compareFailure({"-password", "wrong", "secret.pdf", "{o}.pdf"});
}

TEST(parity_outputFlags) {
    document();
    for (const std::string flag : {"-o", "-oc", "-ot", "-ots", "-os", "-ou", "-oum", "-om"}) {
        comparePdf({"in.pdf", flag, "{o}.pdf"});
        fs::remove("port.pdf");
        fs::remove("java.pdf");
    }
    // u with an option block, which jPDF Tweak 1.3.1 ignored
    comparePdf({"in.pdf", "-preserveLinks", "On", "-ou", "{o}.pdf"});
}

TEST(parity_outputNames) {
    document();
    // the port and Java write the same names, one run after the other
    for (const std::string side : {"port", "java"}) {
        const std::vector<std::string> args = {"in.pdf", "<P>\\" + side + "-<F>-<#>.pdf"};
        const Outcome outcome = side == "port" ? runPort(args) : runJava(args);
        CHECK(outcome.ok);
    }
    CHECK(fs::exists("port-in-1.pdf") && fs::exists("java-in-1.pdf"));
    compareFailure({"in.pdf", "in.pdf"});
}

TEST(parity_burst) {
    document();
    for (const std::vector<std::string> args :
         {std::vector<std::string>{"in.pdf", "-ob", "{o}-*.pdf"},
          std::vector<std::string>{"in.pdf", "-pagenumbers", "1=p:1a", "--", "-ob", "{o}-*.pdf"}}) {
        const Runs runs = runBoth(args);
        requireSuccess(runs);
        std::vector<std::string> portFiles, javaFiles;
        for (const auto& entry : fs::directory_iterator(".")) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("port-", 0) == 0)
                portFiles.push_back(name.substr(5));
            if (name.rfind("java-", 0) == 0)
                javaFiles.push_back(name.substr(5));
        }
        std::sort(portFiles.begin(), portFiles.end());
        std::sort(javaFiles.begin(), javaFiles.end());
        CHECK(portFiles == javaFiles);
        CHECK_EQUAL(portFiles.size(), size_t(4));
        for (const std::string& name : portFiles) {
            CHECK_EQUAL(describe("port-" + name), describe("java-" + name));
            compareRendering("port-" + name, "java-" + name);
            fs::remove("port-" + name);
            fs::remove("java-" + name);
        }
    }
    compareFailure({"in.pdf", "-ob", "{o}.pdf"});
}

TEST(parity_imageExport) {
    document();
    struct Image {
        const char* type;
        const char* mode;
        const char* transparency;
        const char* extension;
        int tolerance;
    };
    // JPG, PNG, PAM, and PNM come from MuPDF on both sides, and BMP and lossless TIFF hold the
    // same pixels. GIF palettes may differ (ChangeLog), so GIF allows a wider tolerance.
    const Image images[] = {{"JPG", "RGB", "NO", "jpg", 2},    {"JPG", "GRAY", "NO", "jpg", 2},
                            {"PNG", "RGB", "YES", "png", 2},   {"PNG", "GRAY", "NO", "png", 2},
                            {"GIF", "RGB", "NO", "gif", 24},   {"GIF", "BNWI", "NO", "gif", 2},
                            {"BMP", "RGB", "NO", "bmp", 2},    {"BMP", "GRAY", "NO", "bmp", 2},
                            {"BMP", "BNW", "NO", "bmp", 2},    {"BMP", "BNWI", "NO", "bmp", 2},
                            {"TIFF", "RGB", "YES", "tiff", 2}, {"TIFF", "GRAY", "NO", "tiff", 2},
                            {"TIFF", "BNW", "NO", "tiff", 2}};
    for (const Image& image : images) {
        const Runs runs = runBoth({"in.pdf", "-burstfiletype", image.type, "-colormode", image.mode,
                                   "-transparency", image.transparency, "-ob", "{o}-*"});
        requireSuccess(runs);
        for (int page = 1; page <= 4; ++page) {
            const std::string name = "-" + std::to_string(page) + "." + image.extension;
            compareImages("port" + name, "java" + name, image.tolerance, 0.005);
        }
    }
    for (const std::string type : {"PAM", "PNM"}) {
        const Runs runs = runBoth({"in.pdf", "-burstfiletype", type, "-colormode", "RGB", "-ob", "{o}-*"});
        requireSuccess(runs);
        std::string extension = type == "PAM" ? "pam" : "pnm";
        CHECK(readFile("port-1." + extension) == readFile("java-1." + extension));
    }
    for (const std::string compression : {"NONE", "LZW", "JPEG", "ZLIB", "PACKBITS", "DEFLATE"}) {
        const Runs runs = runBoth({"in.pdf", "-burstfiletype", "TIFF", "-tiffcompression", compression,
                                   "-imagequality", "80", "-ob", "{o}-c*.tiff"});
        requireSuccess(runs);
        const auto portTags = tiffTags(readFile("port-c1.tiff"));
        const auto javaTags = tiffTags(readFile("java-c1.tiff"));
        const auto tagText = [](const std::map<int, std::vector<long long>>& tags, int tag) {
            std::string text = std::to_string(tag) + ":";
            if (tags.count(tag))
                for (const long long value : tags.at(tag))
                    text += " " + std::to_string(value);
            return text;
        };
        for (const int tag : {256, 257, 258, 259, 262, 277, 278, 284, 296})
            CHECK_EQUAL(tagText(portTags, tag), tagText(javaTags, tag));
        compareImages("port-c1.tiff", "java-c1.tiff", compression == "JPEG" ? 12 : 2, 0.005);
    }
    const Runs unsupported =
        runBoth({"in.pdf", "-burstfiletype", "PNG", "-colormode", "BNW", "-ob", "{o}-u*"});
    CHECK(!unsupported.port.ok && !unsupported.java.ok);
    CHECK(sameMessage(unsupported.port.message, unsupported.java.message));
}

TEST(parity_multipageTiff) {
    document();
    for (const std::vector<std::string> args :
         {std::vector<std::string>{"in.pdf", "-oi", "{o}.tiff"},
          std::vector<std::string>{"in.pdf", "-colormode", "GRAY", "-tiffcompression", "LZW", "-oi",
                                   "{o}-gray.tif"}}) {
        const Runs runs = runBoth(args);
        requireSuccess(runs);
    }
    CHECK_EQUAL(tiffPageCount(readFile("port.tiff")), tiffPageCount(readFile("java.tiff")));
    for (unsigned page = 0; page < 4; ++page) {
        compareImages("port.tiff", "java.tiff", 2, 0.005, page);
        compareImages("port-gray.tiff", "java-gray.tiff", 2, 0.005, page);
    }
    compareFailure({"in.pdf", "-tiffcompression", "JPEG", "-transparency", "YES", "-oi", "{o}-t.tiff"});
}

// ---- information ----

TEST(parity_infoOutputs) {
    document();
    runBoth({"in.pdf", "-bookmark", "1;O;Part;1 Fit", "-bookmark", "2;BI;Chapter;2 XYZ 0 800 0",
             "-pagenumbers", "1=Intro:1i", "{o}-marked.pdf"});
    // decision 10: the Producer that -info reports carries each program's own note
    for (const std::string option : {"-info", "-moreinfo", "-savebookmarks"}) {
        const Runs runs = runBoth({"port-marked.pdf", option, "{o}-" + option.substr(1) + ".txt"});
        CHECK(runs.port.ok && runs.java.ok);
        compareText("port-" + option.substr(1) + ".txt", "java-" + option.substr(1) + ".txt",
                    {"DOCINFO Producer"});
    }
}

TEST(parity_errors) {
    document();
    compareFailure({"missing.pdf", "{o}.pdf"});
    compareFailure({"in.pdf", "-bookmark", "not a bookmark", "{o}.pdf"});
    compareFailure({"in.pdf", "-pagenumbers", "x", "{o}.pdf"});
    compareFailure({"in.pdf", "-oi", "{o}*.tiff"});
    // decision 14: refusals that Java printed and returned from, which the port reports with exit
    // code 1
    for (const std::vector<std::string> args :
         {std::vector<std::string>{"in.pdf", "-nosuchoption", "x", "{o}.pdf"},
          std::vector<std::string>{"in.pdf", "-viewpref", "Bad", "{o}.pdf"},
          std::vector<std::string>{"in.pdf", "-pagemode", "Sideways", "{o}.pdf"},
          std::vector<std::string>{"in.pdf", "-watermark", "bad", "{o}.pdf"},
          std::vector<std::string>{"in.pdf", "-crop"}}) {
        const Runs runs = runBoth(args);
        CHECK(!runs.port.ok);
        CHECK_EQUAL(firstLine(runs.port.err), firstLine(runs.java.err));
        CHECK(!fs::exists("port.pdf") && !fs::exists("java.pdf"));
    }
}

// ---- help ----

// Every help text, with the program's name changed as decision 13 says and the version line of the
// general help left out.
TEST(parity_helpTexts) {
    const auto renamed = [](std::string text) {
        for (const auto& [from, to] : {std::pair<std::string, std::string>{"jPDF Tweak", "PDF Tweak"},
                                       std::pair<std::string, std::string>{"jpdftweak", "pdftweak"}})
            for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
                text.replace(at, from.size(), to);
        return text;
    };
    const auto output = [](const std::vector<std::string>& args, bool java) {
        if (!java)
            return runProcess(binaryFolder() / "pdftweak.com", commandLine(args)).out;
        std::vector<std::string> full = {"-jar",
                                         pdftweak::toUtf8((javaFolder() / "jpdftweak.jar").wstring())};
        full.insert(full.end(), args.begin(), args.end());
        return runProcess(javaExecutable(), commandLine(full)).out;
    };
    const auto withoutFirstLine = [](const std::string& text) { return text.substr(text.find('\n') + 1); };
    CHECK_EQUAL(withoutFirstLine(output({"-help"}, false)),
                withoutFirstLine(renamed(output({"-help"}, true))));
    for (const std::string option : {"-i",
                                     "-o",
                                     "-preserveLinks",
                                     "-crop",
                                     "-rot",
                                     "-rotexp",
                                     "-scale",
                                     "-background",
                                     "-watermark",
                                     "-numbers",
                                     "-shuffle",
                                     "-pagenumbers",
                                     "-loadpagenumbers",
                                     "-bookmark",
                                     "-loadbookmarks",
                                     "-attach",
                                     "-trans",
                                     "-transfile",
                                     "-pagemode",
                                     "-pagelayout",
                                     "-viewpref",
                                     "-docinfo",
                                     "-ownerpw",
                                     "-userpw",
                                     "-permissions",
                                     "-encmode",
                                     "-encnometadata",
                                     "-sign",
                                     "-signvisible",
                                     "-keystore",
                                     "-keyalias",
                                     "-certlevel",
                                     "-info",
                                     "-moreinfo",
                                     "-savebookmarks",
                                     "-colormode",
                                     "-tiffcompression",
                                     "-transparency",
                                     "-imagequality",
                                     "-burstfiletype"})
        CHECK_EQUAL(output({"-help", option}, false), renamed(output({"-help", option}, true)));
}
