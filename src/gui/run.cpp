#include "gui/run.h"

#include "core/error.h"
#include "core/text.h"
#include "core/version.h"

#include <algorithm>
#include <new>

namespace pdftweak::gui {

double RunStatus::fraction() const {
    const double stageFraction =
        units > 0 ? static_cast<double>(std::min(done, units)) / static_cast<double>(units) : 0.0;
    const double inFile = (operation + stageFraction) / std::max(1, operations);
    return std::clamp((file + std::min(1.0, inFile)) / std::max(1, files), 0.0, 1.0);
}

std::wstring RunStatus::fileLine() const {
    return L"File " + std::to_wstring(file + 1) + L" of " + std::to_wstring(files) + L": " + fileName;
}

std::wstring RunStatus::stageLine() const {
    if (stage.empty())
        return tab;
    std::wstring line = tab + L": " + fromUtf8(stage);
    if (stage == "Writing output")
        line += L", " + std::to_wstring(std::min(done, units)) + L" %";
    else if (units > 1)
        line += L", page " + std::to_wstring(std::min(done + 1, units)) + L" of " + std::to_wstring(units);
    return line;
}

void GuiProgress::stage(const std::string& utf8Label, long long units) {
    RunStatus copy;
    {
        const std::lock_guard lock(mutex_);
        status_.stage = utf8Label;
        status_.units = units;
        status_.done = 0;
        copy = status_;
    }
    if (onStage)
        onStage(*this, copy);
    post(true);
}

void GuiProgress::advance(long long units) {
    {
        const std::lock_guard lock(mutex_);
        status_.done += units;
    }
    post(false);
}

void GuiProgress::beginFile(int file, int files, std::wstring name, int operations) {
    {
        const std::lock_guard lock(mutex_);
        status_ = RunStatus{};
        status_.file = file;
        status_.files = files;
        status_.fileName = std::move(name);
        status_.operations = operations;
    }
    post(true);
}

void GuiProgress::beginOperation(int operation, std::wstring tab) {
    {
        const std::lock_guard lock(mutex_);
        status_.operation = operation;
        status_.tab = std::move(tab);
        status_.stage.clear();
        status_.units = 0;
        status_.done = 0;
    }
    post(true);
}

RunStatus GuiProgress::status() const {
    const std::lock_guard lock(mutex_);
    return status_;
}

void GuiProgress::post(bool always) {
    const auto now = std::chrono::steady_clock::now();
    {
        const std::lock_guard lock(mutex_);
        if (!always && now - lastPost_ < std::chrono::milliseconds(50))
            return;
        lastPost_ = now;
    }
    PostMessageW(window_, message_, 0, 0);
}

Run::Run(HWND window, UINT progressMessage, UINT doneMessage, std::vector<NamedOperation> operations,
         std::vector<std::wstring> fileNames, std::function<void(GuiProgress&, const RunStatus&)> onStage)
    : window_(window), doneMessage_(doneMessage), operations_(std::move(operations)),
      fileNames_(std::move(fileNames)), progress_(window, progressMessage) {
    progress_.onStage = std::move(onStage);
    result_.files = static_cast<int>(fileNames_.size());
    thread_ = std::thread([this] { work(); });
}

Run::~Run() {
    progress_.cancel();
    if (thread_.joinable())
        thread_.join();
}

const RunResult& Run::finish() {
    if (thread_.joinable())
        thread_.join();
    return result_;
}

void Run::work() {
    const int files = static_cast<int>(fileNames_.size());
    try {
        for (int task = 0; task < files; ++task) {
            progress_.beginFile(task, files, fileNames_[static_cast<size_t>(task)],
                                static_cast<int>(operations_.size()));
            RunContext context{task, progress_, nullptr};
            try {
                for (size_t i = 0; i < operations_.size(); ++i) {
                    progress_.beginOperation(static_cast<int>(i), operations_[i].tab);
                    if (progress_.cancelled())
                        throw Cancelled();
                    operations_[i].operation(context);
                }
            } catch (...) {
                if (context.tweak)
                    context.tweak->cleanup();
                throw;
            }
            if (context.tweak)
                context.tweak->cleanup();
            result_.finishedFiles = task + 1;
        }
        result_.kind = RunResult::Kind::Finished;
        result_.message = L"Finished";
        result_.title = L"" PDFTWEAK_PROGRAM_NAME;
    } catch (const Cancelled&) {
        result_.kind = RunResult::Kind::Cancelled;
        result_.message = L"Cancelled after " + std::to_wstring(result_.finishedFiles) + L" of " +
                          std::to_wstring(files) + L" files";
        result_.title = L"" PDFTWEAK_PROGRAM_NAME;
    } catch (const Error& error) {
        result_.kind = RunResult::Kind::Failed;
        result_.message = fromUtf8(error.what());
        result_.title = L"Error";
    } catch (const std::bad_alloc& error) {
        result_.kind = RunResult::Kind::Failed;
        result_.message = L"" PDFTWEAK_PROGRAM_NAME L" has run out of memory. You can enable the Tempfile "
                          L"option on the output tab.";
        result_.title = L"Out of memory: " + fromUtf8(error.what());
    } catch (const std::exception& error) {
        result_.kind = RunResult::Kind::Failed;
        result_.message = fromUtf8(error.what());
        result_.title = L"Error";
    }
    PostMessageW(window_, doneMessage_, 0, 0);
}

} // namespace pdftweak::gui
