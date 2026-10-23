#pragma once

// PreviewPanel: the pages of a shuffle rule set drawn as yellow rectangles, each rotated, scaled, and
// moved as its rule says, on white output pages.

#include "core/formats/shuffle_rule.h"

#include <windows.h>

#include <vector>

namespace pdftweak::gui {

class Preview {
public:
    HWND create(HWND parent);
    HWND handle() const { return hwnd_; }

    void setConfig(std::vector<formats::ShuffleRule> rules);
    void setPageFormat(float width, float height);

private:
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void paint(HDC dc);

    HWND hwnd_ = nullptr;
    float pageWidth_ = 1;
    float pageHeight_ = 1;
    std::vector<formats::ShuffleRule> rules_;
};

} // namespace pdftweak::gui
