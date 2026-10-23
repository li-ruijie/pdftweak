#pragma once

// The eleven tabs of the main window, in the order of MainForm.

#include "gui/tab.h"

#include <memory>

namespace pdftweak::gui {

// A tab that only names itself, as DummyTab, until its port takes its place.
class PlaceholderTab final : public Tab {
public:
    PlaceholderTab(MainWindow& mainWindow, const wchar_t* name) : Tab(mainWindow), name_(name) {}
    const wchar_t* name() const override { return name_; }
    Operation prepare() override { return {}; }

private:
    void build() override {
        FormLayout& layout = newLayout(L"f:p:g", L"f:p:g");
        layout.add(label(L"Not implemented yet."), 1, 1);
        setLayout(layout.asComponent());
    }
    const wchar_t* name_;
};

class InputTab final : public Tab {
public:
    explicit InputTab(MainWindow& mainWindow) : Tab(mainWindow) {}
    const wchar_t* name() const override { return L"Input"; }
    Operation prepare() override { return {}; }
    int batchLength() const { return 1; }

private:
    void build() override {
        FormLayout& layout = newLayout(L"f:p:g", L"f:p:g");
        layout.add(label(L"Not implemented yet."), 1, 1);
        setLayout(layout.asComponent());
    }
};

} // namespace pdftweak::gui
