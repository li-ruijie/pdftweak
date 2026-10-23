#pragma once

// A run of the tabs' work on a worker thread (spec section 9.1). The worker reports through
// GuiProgress, which posts at most one update per 50 milliseconds to the window, and ends by posting
// its result. Cancel takes effect at the next page or stage boundary.

#include "core/progress.h"
#include "gui/tab.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pdftweak::gui {

// What the progress panel shows.
struct RunStatus {
    int file = 0; // from 0
    int files = 1;
    std::wstring fileName;
    int operation = 0; // the tabs whose work is done, for this file
    int operations = 1;
    std::wstring tab;
    std::string stage; // the core's label, empty before the tab's first stage
    long long units = 0;
    long long done = 0;

    // (finished files + (finished tabs + stage fraction) / tabs) / files
    double fraction() const;
    std::wstring fileLine() const;  // "File 2 of 5: report.pdf"
    std::wstring stageLine() const; // "Shuffle/N-up: Shuffling, page 37 of 120"
};

class GuiProgress final : public Progress {
public:
    // Posts message to window when the status changes.
    GuiProgress(HWND window, UINT message) : window_(window), message_(message) {}

    void stage(const std::string& utf8Label, long long units) override;
    void advance(long long units) override;
    bool cancelled() const override { return cancelled_; }

    void cancel() { cancelled_ = true; }
    void beginFile(int file, int files, std::wstring name, int operations);
    void beginOperation(int operation, std::wstring tab);
    RunStatus status() const;

    // Called on the worker thread as each stage starts, for the tests, which may cancel there.
    std::function<void(GuiProgress&, const RunStatus&)> onStage;

private:
    void post(bool always);

    HWND window_;
    UINT message_;
    mutable std::mutex mutex_;
    RunStatus status_;
    std::atomic<bool> cancelled_{false};
    std::chrono::steady_clock::time_point lastPost_{};
};

// The outcome the worker posts when it ends.
struct RunResult {
    enum class Kind { Finished, Cancelled, Failed };
    Kind kind = Kind::Finished;
    int finishedFiles = 0;
    int files = 0;
    std::wstring message;
    std::wstring title;
};

// One tab's work with its name.
struct NamedOperation {
    std::wstring tab;
    Operation operation;
};

class Run {
public:
    // Starts the worker. It posts progressMessage with the status and doneMessage when it ends, after
    // which result() holds the outcome.
    Run(HWND window, UINT progressMessage, UINT doneMessage, std::vector<NamedOperation> operations,
        std::vector<std::wstring> fileNames,
        std::function<void(GuiProgress&, const RunStatus&)> onStage = {});
    ~Run();
    Run(const Run&) = delete;
    Run& operator=(const Run&) = delete;

    GuiProgress& progress() { return progress_; }
    // Waits for the worker, which has posted doneMessage.
    const RunResult& finish();

private:
    void work();

    HWND window_;
    UINT doneMessage_;
    std::vector<NamedOperation> operations_;
    std::vector<std::wstring> fileNames_;
    GuiProgress progress_;
    RunResult result_;
    std::thread thread_;
};

} // namespace pdftweak::gui
