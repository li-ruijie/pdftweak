#include "core/error.h"
#include "core/java_text.h"
#include "core/text.h"
#include "gui/dialogs.h"
#include "gui/main_window.h"
#include "gui/tabs.h"

#include <array>
#include <optional>

namespace pdftweak::gui {
namespace {

// The NumberFormatException of a field, which WatermarkTab.run reports as an unparsable value.
int parseInt(HWND field) {
    try {
        return javaParseInt(toUtf8(windowText(field)));
    } catch (const Error& error) {
        throw Error(Error::Kind::Io, std::string("Unparsable value: ") + error.what());
    }
}

float parseFloat(HWND field) {
    try {
        return javaParseFloat(toUtf8(windowText(field)));
    } catch (const Error& error) {
        throw Error(Error::Kind::Io, std::string("Unparsable value: ") + error.what());
    }
}

} // namespace

void WatermarkTab::build() {
    FormLayout& layout =
        newLayout(L"f:p, f:p:g, 80dlu, f:p", L"f:p, f:p, 10dlu, f:p, f:p, f:p, f:p, f:p, 10dlu, "
                                             L"f:p, f:p, f:p, f:p, f:p, f:p, f:p, f:p:g");
    pdfWatermark_ =
        checkBox(L"Add first page of PDF as background watermark", [this] { updatePdfWatermarkEnabled(); });
    layout.add(pdfWatermark_, 1, 1, 4);
    layout.add(label(L"Filename: "), 1, 2);
    filename_ = edit(L"", 0, ES_READONLY);
    layout.add(filename_, 2, 2, 2);
    fileButton_ = button(L"...", [this] {
        const auto files = openFiles(mainWindow_.handle(), Chooser::Pdf, false);
        if (!files.empty())
            setWindowText(filename_, std::filesystem::absolute(files[0]).wstring());
    });
    layout.add(fileButton_, 4, 2);
    layout.add(separator(), 1, 3, 4);
    textWatermark_ = checkBox(L"Add transparent text watermark", [this] { updateTextWatermarkEnabled(); });
    layout.add(textWatermark_, 1, 4, 4);
    layout.add(label(L"Text:"), 1, 5);
    watermarkText_ = edit(L"Confidential");
    layout.add(watermarkText_, 2, 5, 3);
    layout.add(label(L"Font size:"), 1, 6);
    watermarkSize_ = edit(L"100");
    layout.add(watermarkSize_, 2, 6, 3);
    layout.add(label(L"Opacity:"), 1, 7);
    watermarkOpacity_ = edit(L"0.25");
    layout.add(watermarkOpacity_, 2, 7, 3);
    watermarkUseColor_ = checkBox(L"Color:");
    layout.add(watermarkUseColor_, 1, 8);
    // a button that shows the colour, as the JButton whose background Java set
    watermarkColor_ = createChild(handle(), WC_BUTTONW, L"", WS_TABSTOP | BS_OWNERDRAW);
    onCommand(watermarkColor_, [this](int code) {
        if (code != BN_CLICKED)
            return;
        if (const auto chosen = chooseColor(mainWindow_.handle(), color_))
            setColor(*chosen);
    });
    layout.add(watermarkColor_, 2, 8, 3);
    layout.add(separator(), 1, 9, 4);
    pageNumbers_ = checkBox(L"Add page numbers", [this] { updatePageNumbersEnabled(); });
    layout.add(pageNumbers_, 1, 10, 4);
    layout.add(label(L"Font size:"), 1, 11);
    pgnoSize_ = edit(L"10");
    layout.add(pgnoSize_, 2, 11, 3);
    layout.add(label(L"Horizontal:"), 1, 12);
    pgnoHOffset_ = edit(L"25");
    layout.add(pgnoHOffset_, 2, 12);
    pgnoHRef_ =
        comboBox({L"PS points from left margin", L"PS points from center", L"PS points from right margin",
                  L"PS points from inner margin", L"PS points from outer margin"});
    layout.add(pgnoHRef_, 3, 12, 2);
    layout.add(label(L"Vertical:"), 1, 13);
    pgnoVOffset_ = edit(L"25");
    layout.add(pgnoVOffset_, 2, 13);
    pgnoVRef_ =
        comboBox({L"PS points from bottom margin", L"PS points from center", L"PS points from top margin"});
    layout.add(pgnoVRef_, 3, 13, 2);
    useMask_ = checkBox(L"Mask: ", [this] { updatePageNumbersEnabled(); });
    layout.add(useMask_, 1, 14);
    maskText_ = edit(L"Page %1$d of %2$d");
    layout.add(maskText_, 2, 14, 3);
    layout.add(
        label(L"%1$d - page index, %2$d - page count, %3$d - logical page number, %4$s page number text"),
        2, 15, 3);
    differentPageNumbers_ = checkBox(L"Use different page numbers", [this] { updatePageNumbersEnabled(); });
    layout.add(differentPageNumbers_, 1, 16, 2);
    load_ = button(L"Load from document", [this] { page_numbers::load(mainWindow_, *pageNumberRanges_); });
    layout.add(load_, 3, 16, 2);
    pageNumberRanges_ = page_numbers::buildRanges();
    pageNumberRanges_->setPreferredSize({750, 100});
    layout.add(childPage(*pageNumberRanges_), 1, 17, 4);
    setLayout(layout.asComponent());
    updatePdfWatermarkEnabled();
    updateTextWatermarkEnabled();
    updatePageNumbersEnabled();
}

void WatermarkTab::setColor(COLORREF color) {
    color_ = color;
    InvalidateRect(watermarkColor_, nullptr, TRUE);
}

LRESULT WatermarkTab::message(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DRAWITEM) {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (item->hwndItem == watermarkColor_) {
            RECT rect = item->rcItem;
            const bool pressed = (item->itemState & ODS_SELECTED) != 0;
            DrawFrameControl(item->hDC, &rect, DFC_BUTTON, DFCS_BUTTONPUSH | (pressed ? DFCS_PUSHED : 0));
            InflateRect(&rect, -scaled(watermarkColor_, 4), -scaled(watermarkColor_, 4));
            COLORREF fill = color_;
            if ((item->itemState & ODS_DISABLED) != 0) {
                // the colour faded towards the face of the button, as a disabled button shows it
                const COLORREF face = GetSysColor(COLOR_BTNFACE);
                fill = RGB((GetRValue(fill) + GetRValue(face)) / 2, (GetGValue(fill) + GetGValue(face)) / 2,
                           (GetBValue(fill) + GetBValue(face)) / 2);
            }
            const HBRUSH brush = CreateSolidBrush(fill);
            FillRect(item->hDC, &rect, brush);
            DeleteObject(brush);
            if ((item->itemState & ODS_FOCUS) != 0) {
                InflateRect(&rect, scaled(watermarkColor_, 2), scaled(watermarkColor_, 2));
                DrawFocusRect(item->hDC, &rect);
            }
            return TRUE;
        }
    }
    return Tab::message(msg, wParam, lParam);
}

void WatermarkTab::updatePdfWatermarkEnabled() {
    enable(fileButton_, isChecked(pdfWatermark_));
    enable(filename_, isChecked(pdfWatermark_));
}

void WatermarkTab::updateTextWatermarkEnabled() {
    const bool text = isChecked(textWatermark_);
    for (HWND control :
         {watermarkText_, watermarkSize_, watermarkOpacity_, watermarkUseColor_, watermarkColor_})
        enable(control, text);
}

void WatermarkTab::updatePageNumbersEnabled() {
    const bool numbers = isChecked(pageNumbers_);
    for (HWND control : {pgnoSize_, pgnoHOffset_, pgnoVOffset_, pgnoHRef_, pgnoVRef_, useMask_})
        enable(control, numbers);
    enable(maskText_, numbers && isChecked(useMask_));
    enable(differentPageNumbers_, numbers);
    enable(load_, numbers && isChecked(differentPageNumbers_));
    pageNumberRanges_->setEnabled(numbers && isChecked(differentPageNumbers_));
}

void WatermarkTab::checkRun() { pageNumberRanges_->checkRun("different page number"); }

Operation WatermarkTab::prepare() {
    bool run = false;
    std::shared_ptr<pdf::InputFile> wmFile;
    std::optional<std::string> wmText;
    int wmSize = 0, pnSize = 0, pnPosition = -1;
    bool pnFlipEven = false;
    float wmOpacity = 0, pnHOff = 0, pnVOff = 0;
    std::optional<std::array<int, 3>> wmColor;
    std::string mask;
    std::optional<std::vector<formats::PageLabelFormat>> labels;
    if (isChecked(pdfWatermark_)) {
        run = true;
        wmFile = std::make_shared<pdf::InputFile>(std::filesystem::path(windowText(filename_)), "");
    }
    if (isChecked(textWatermark_)) {
        run = true;
        wmText = toUtf8(windowText(watermarkText_));
        wmSize = parseInt(watermarkSize_);
        wmOpacity = parseFloat(watermarkOpacity_);
        if (wmSize == 0)
            throw Error(Error::Kind::Io, "Font size may not be zero");
    }
    if (isChecked(pageNumbers_)) {
        if (isChecked(differentPageNumbers_)) {
            labels = page_numbers::formats(*pageNumberRanges_);
            mask = "%4$s";
        }
        run = true;
        int hIndex = selectedIndex(pgnoHRef_);
        if (hIndex > 2) {
            hIndex = hIndex * 2 - 6;
            pnFlipEven = true;
        }
        pnPosition = selectedIndex(pgnoVRef_) * 3 + hIndex;
        pnSize = parseInt(pgnoSize_);
        pnHOff = parseFloat(pgnoHOffset_);
        pnVOff = parseFloat(pgnoVOffset_);
        if (pnSize == 0)
            throw Error(Error::Kind::Io, "Font size may not be zero");
    }
    if (isChecked(watermarkUseColor_))
        wmColor = std::array<int, 3>{GetRValue(color_), GetGValue(color_), GetBValue(color_)};
    if (isChecked(useMask_))
        mask = toUtf8(windowText(maskText_));
    if (!run)
        return {};
    return [=](RunContext& context) {
        if (labels)
            context.tweak->setPageNumbers(*labels);
        context.tweak->addWatermark(wmFile.get(), wmText, wmSize, wmOpacity, wmColor, pnPosition,
                                    pnFlipEven, pnSize, pnHOff, pnVOff, mask);
    };
}

} // namespace pdftweak::gui
