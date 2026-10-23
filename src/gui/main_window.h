#pragma once

// MainForm: the window with the eleven tabs and the Run and Quit buttons.

#include "core/pdf/input_file.h"
#include "gui/tab.h"

#include <memory>
#include <vector>

namespace pdftweak::gui {

class InputTab;

class MainWindow {
public:
    // Creates the window, shown when visible is set. The tests create it hidden.
    explicit MainWindow(bool visible);
    ~MainWindow();
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    HWND handle() const;

    int tabCount() const { return static_cast<int>(tabs_.size()); }
    Tab& tab(int index) { return *tabs_[static_cast<size_t>(index)]; }
    // The tab of the given type, such as tab<OutputTab>().
    template <typename T> T& tab() {
        for (const auto& t : tabs_)
            if (auto* found = dynamic_cast<T*>(t.get()))
                return *found;
        throw std::logic_error("no such tab");
    }
    InputTab& inputTab();

    // getInputFile and setInputFile: the first input file, or null.
    const std::shared_ptr<pdf::InputFile>& inputFile() const { return inputFile_; }
    void setInputFile(std::shared_ptr<pdf::InputFile> file) { inputFile_ = std::move(file); }

    // runTweaks: checks every tab, then runs each tab's work on each file of the batch.
    void runTweaks();

private:
    class Content;
    std::vector<std::unique_ptr<Tab>> tabs_;
    std::unique_ptr<Content> content_;
    std::unique_ptr<Frame> frame_;
    std::shared_ptr<pdf::InputFile> inputFile_;
};

} // namespace pdftweak::gui
