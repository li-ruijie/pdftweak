#pragma once

// Tab: one page of the main window, as jpdftweak.gui.tabs.Tab. Java's tabs read their controls and
// changed the PdfTweak in run. The port splits that in two: prepare reads the controls on the
// window's thread and returns the work, which the run then does on its worker thread.

#include "core/pdf/tweak.h"
#include "core/progress.h"
#include "gui/window.h"

#include <functional>
#include <memory>

namespace pdftweak::gui {

class MainWindow;

struct RunContext {
    // the file of a batch, from 0, and 0 outside batch processing
    int batchIndex = 0;
    Progress& progress;
    // made by the work of the Input tab, then changed by the others in tab order
    std::unique_ptr<pdf::Tweak> tweak;
};

using Operation = std::function<void(RunContext&)>;

class Tab : public Page {
public:
    explicit Tab(MainWindow& mainWindow) : mainWindow_(mainWindow) {}

    // getTabName
    virtual const wchar_t* name() const = 0;

    // Checks the settings before a run. Throws Error(Io) with Java's message, as its IOException.
    virtual void checkRun() {}

    // The work of the tab with the settings its controls hold now, or an empty function when the tab
    // leaves the document as it is. Throws Error(Io) for a setting that does not parse.
    virtual Operation prepare() = 0;

protected:
    MainWindow& mainWindow_;
};

} // namespace pdftweak::gui
