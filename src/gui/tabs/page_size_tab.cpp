#include "core/error.h"
#include "core/formats/page_dimension.h"
#include "core/java_text.h"
#include "core/text.h"
#include "gui/tabs.h"

namespace pdftweak::gui {

void PageSizeTab::build() {
    FormLayout& layout =
        newLayout(L"f:p, f:p:g, f:p", L"f:p, 10dlu,f:p, f:p, f:p, 10dlu, f:p, 10dlu, f:p,f:p, f:p,f:p, "
                                      L"f:p,f:p, 10dlu, f:p, f:p:g");
    cropPages_ = checkBox(L"Crop to:", [this] { enable(cropTo_, isChecked(cropPages_)); });
    layout.add(cropPages_, 1, 1);
    cropTo_ = comboBox({L"MediaBox", L"CropBox", L"BleedBox", L"TrimBox", L"ArtBox"});
    layout.add(cropTo_, 2, 1, 2);
    selectIndex(cropTo_, 1);
    enable(cropTo_, false);
    layout.add(separator(), 1, 2, 3);
    rotatePages_ = checkBox(L"Rotate pages", [this] {
        enable(rotatePortrait_, isChecked(rotatePages_));
        enable(rotateLandscape_, isChecked(rotatePages_));
        if (!isChecked(rotatePages_)) {
            selectIndex(rotatePortrait_, 0);
            selectIndex(rotateLandscape_, 0);
        }
    });
    layout.add(rotatePages_, 1, 3, 3);
    layout.add(label(L"Portrait pages:"), 1, 4);
    rotatePortrait_ = comboBox({L"Keep", L"Right", L"Upside-Down", L"Left"});
    layout.add(rotatePortrait_, 2, 4, 2);
    layout.add(label(L"Landscape pages:"), 1, 5);
    rotateLandscape_ = comboBox({L"Keep", L"Right", L"Upside-Down", L"Left"});
    layout.add(rotateLandscape_, 2, 5, 2);
    enable(rotatePortrait_, false);
    enable(rotateLandscape_, false);
    layout.add(separator(), 1, 6, 3);
    fixRotation_ = checkBox(L"Remove implicit page rotation");
    layout.add(fixRotation_, 1, 7, 3);
    layout.add(separator(), 1, 8, 3);
    scalePages_ = checkBox(L"Scale pages", [this] {
        const bool scale = isChecked(scalePages_);
        for (HWND control : {scaleSize_, scaleWidth_, scaleHeight_, scaleCenter_, scaleNoPreserve_})
            enable(control, scale);
    });
    layout.add(scalePages_, 1, 9, 3);
    layout.add(label(L"Page Size:"), 1, 10);
    std::vector<std::wstring> sizes;
    for (const formats::PageDimension& size : formats::commonSizes())
        sizes.push_back(fromUtf8(size.name));
    scaleSize_ = comboBox(sizes, [this] { updateScaleSize(); });
    layout.add(scaleSize_, 2, 10, 2);
    layout.add(label(L"Page Width:"), 1, 11);
    scaleWidth_ = edit(L"");
    layout.add(scaleWidth_, 2, 11);
    layout.add(label(L"PostScript points"), 3, 11);
    layout.add(label(L"Page Height:"), 1, 12);
    scaleHeight_ = edit(L"");
    layout.add(scaleHeight_, 2, 12);
    layout.add(label(L"PostScript points"), 3, 12);
    scaleCenter_ = checkBox(L"Center instead of enlarging");
    layout.add(scaleCenter_, 1, 13, 3);
    scaleNoPreserve_ = checkBox(L"Do not preserve aspect ratio");
    layout.add(scaleNoPreserve_, 1, 14, 3);
    enable(scaleSize_, false);
    selectIndex(scaleSize_, 0);
    updateScaleSize();
    enable(scaleWidth_, false);
    enable(scaleHeight_, false);
    enable(scaleCenter_, false);
    enable(scaleNoPreserve_, false);
    layout.add(separator(), 1, 15, 3);
    preserveHyperlinks_ = checkBox(L"Preserve hyperlinks (EXPERIMENTAL)");
    layout.add(preserveHyperlinks_, 1, 16, 3);
    setLayout(layout.asComponent());
}

void PageSizeTab::updateScaleSize() {
    const formats::PageDimension& size =
        formats::commonSizes()[static_cast<size_t>(selectedIndex(scaleSize_))];
    setWindowText(scaleWidth_, fromUtf8(javaFloat(size.width)));
    setWindowText(scaleHeight_, fromUtf8(javaFloat(size.height)));
}

Operation PageSizeTab::prepare() {
    const bool preserveHyperlinks = isChecked(preserveHyperlinks_);
    const bool crop = isChecked(cropPages_);
    const auto cropTo = static_cast<pdf::PageBox>(selectedIndex(cropTo_));
    const bool rotate = isChecked(rotatePages_);
    const int portrait = selectedIndex(rotatePortrait_);
    const int landscape = selectedIndex(rotateLandscape_);
    const bool fixRotation = isChecked(fixRotation_);
    const bool scale = isChecked(scalePages_);
    float width = 0, height = 0;
    if (scale) {
        try {
            width = javaParseFloat(toUtf8(windowText(scaleWidth_)));
            height = javaParseFloat(toUtf8(windowText(scaleHeight_)));
        } catch (const Error&) {
            throw Error(Error::Kind::Io, "Invalid scale size");
        }
    }
    const bool center = isChecked(scaleCenter_);
    const bool preserveAspectRatio = !isChecked(scaleNoPreserve_);
    if (!preserveHyperlinks && !crop && !rotate && !fixRotation && !scale)
        return {};
    return [=](RunContext& context) {
        if (preserveHyperlinks)
            context.tweak->preserveHyperlinks();
        if (crop)
            context.tweak->cropPages(cropTo);
        if (rotate)
            context.tweak->rotatePages(portrait, landscape);
        if (fixRotation)
            context.tweak->removeRotation();
        if (scale)
            context.tweak->scalePages(width, height, center, preserveAspectRatio);
    };
}

} // namespace pdftweak::gui
