#include "core/error.h"
#include "core/image/pdf_to_image.h"
#include "core/text.h"
#include "gui/dialogs.h"
#include "gui/main_window.h"
#include "gui/tabs.h"

#include <commctrl.h>

#include <algorithm>
#include <cwctype>

namespace pdftweak::gui {
namespace {

const wchar_t* const kImagesIn72Dpi = L"Images will be exported in 72 dpi";

std::vector<std::wstring> colorModes(int count) {
    std::vector<std::wstring> names;
    for (int i = 0; i < count; ++i)
        names.push_back(fromUtf8(image::colorModeText(static_cast<image::ColorMode>(i))));
    return names;
}

} // namespace

void OutputTab::build() {
    FormLayout& layout = newLayout(
        L"f:p, f:p:g, f:p", L"f:p, f:p, f:p, f:p, f:p, f:p, f:p, f:p, f:p, f:p, f:p, f:p, f:p, f:p:g");
    layout.add(label(L"Filename:"), 1, 1);
    outputFile_ = edit(L"");
    layout.add(outputFile_, 2, 1);
    layout.add(button(L"...",
                      [this] {
                          if (const auto file = saveFile(mainWindow_.handle(), Chooser::Pdf))
                              chooseOutput(*file);
                      }),
               3, 1);
    multipageTiff_ = checkBox(L"Export as Tiff multipage image", [this] {
        findSharedLibrary();
        enable(fileType_, false);
        setChecked(burst_, false);
        whichToEnable(isChecked(multipageTiff_) ? 100 : 0);
        updateExtension();
    });
    layout.add(multipageTiff_, 1, 2, 3);
    burst_ = checkBox(L"Burst pages (use * in file name as wildcard for page number)", [this] {
        setChecked(multipageTiff_, false);
        enable(fileType_, isChecked(burst_));
        clearWarning();
        if (!isChecked(burst_) && !isChecked(multipageTiff_)) {
            whichToEnable(0);
        } else {
            whichToEnable(selectedIndex(fileType_));
            if (selectedIndex(fileType_) != 0)
                findSharedLibrary();
        }
        updateExtension();
    });
    layout.add(burst_, 1, 3, 3);
    layout.add(label(L"Type:"), 1, 4);
    std::vector<std::wstring> types;
    for (int i = 0; i <= static_cast<int>(image::ImageType::TIFF); ++i)
        types.push_back(fromUtf8(image::imageTypeName(static_cast<image::ImageType>(i))));
    fileType_ = comboBox(types, [this] {
        if (selectedIndex(fileType_) != lastFileType_) {
            lastFileType_ = selectedIndex(fileType_);
            fileTypeChanged();
        }
    });
    layout.add(fileType_, 2, 4, 2);
    enable(fileType_, false);

    const HWND imageOptions = groupBox(L"Burst as Image Options");
    FormLayout& imagePanel = newLayout(L"f:p, f:p:g, f:p", L"f:p, f:p, f:p, 30px");
    colorLabel_ = label(L"Color Mode:");
    imagePanel.add(colorLabel_, 1, 1);
    colorMode_ = comboBox(colorModes(4), [this] {
        if (selectedIndex(colorMode_) != lastColorMode_) {
            lastColorMode_ = selectedIndex(colorMode_);
            colorModeChanged();
        }
    });
    imagePanel.add(colorMode_, 2, 1, 2);
    compressionLabel_ = label(L"Compression:");
    imagePanel.add(compressionLabel_, 1, 2);
    std::vector<std::wstring> compressions;
    for (int i = 0; i <= static_cast<int>(image::TiffCompression::DEFLATE); ++i)
        compressions.push_back(
            fromUtf8(image::tiffCompressionName(static_cast<image::TiffCompression>(i))));
    compressionType_ = comboBox(compressions, [this] {
        if (selectedIndex(compressionType_) != lastCompression_) {
            lastCompression_ = selectedIndex(compressionType_);
            compressionChanged();
        }
    });
    imagePanel.add(compressionType_, 2, 2, 2);
    qualityLabel_ = label(L"Quality:");
    imagePanel.add(qualityLabel_, 1, 3);
    qualitySlider_ = createChild(handle(), TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_HORZ | TBS_NOTICKS);
    SendMessageW(qualitySlider_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(qualitySlider_, TBM_SETPOS, TRUE, 100);
    imagePanel.add(qualitySlider_, 2, 3, 2);
    transparent_ = checkBox(L"Save transparency of image background");
    imagePanel.add(transparent_, 1, 4, 2);
    // the label with the info icon, whose tooltip says how images are exported
    warning_ = createChild(handle(), WC_STATICW, L"", SS_ICON | SS_CENTERIMAGE | SS_NOTIFY);
    HICON icon = nullptr;
    LoadIconWithScaleDown(nullptr, IDI_INFORMATION, scaled(handle(), 16), scaled(handle(), 16), &icon);
    SendMessageW(warning_, STM_SETICON, reinterpret_cast<WPARAM>(icon), 0);
    ShowWindow(warning_, SW_HIDE);
    imagePanel.add(sized(warning_, {16, 16}), 3, 4);
    layout.add(titled(imageOptions, imagePanel.asComponent()), 1, 5, 3);

    layout.add(separator(), 1, 6, 3);
    uncompressed_ = checkBox(L"Save uncompressed", [this] {
        setWindowText(pageMarks_,
                      std::wstring(isChecked(uncompressed_) ? L"Add" : L"Remove") + L" PdfTk page marks");
    });
    layout.add(uncompressed_, 1, 7, 3);
    pageMarks_ = checkBox(L"Remove PdfTk page marks");
    layout.add(pageMarks_, 1, 8, 3);
    tempfiles_ = checkBox(L"Use temporary files for intermediary results (saves RAM)");
    layout.add(tempfiles_, 1, 9, 3);
    optimizeSize_ = checkBox(L"Optimize PDF size (will need a lot of RAM)");
    layout.add(optimizeSize_, 1, 10, 3);
    fullyCompressed_ = checkBox(L"Use better compression (Acrobat 6.0)");
    layout.add(fullyCompressed_, 1, 11, 3);
    layout.add(label(L"You can use the following variables in the output filename:\n"
                     L"<F>: Input filename without extension\n"
                     L"<FX>: Input filename with extension\n"
                     L"<P>: Input file path without filename\n"
                     L"<#>: Next free number (where file does not exist)\n"
                     L"* Page number (for bursting pages)"),
               1, 12, 3);
    setLayout(layout.asComponent());
    setOptionsEnabled(false, false, false, false, true);
}

void OutputTab::chooseOutput(const std::filesystem::path& file) {
    std::wstring filename = std::filesystem::absolute(file).wstring();
    if (file.filename().wstring().find(L'.') == std::wstring::npos)
        filename += currentExtension_;
    filename = setCorrectExtension(filename);
    if (std::filesystem::exists(filename) && !confirmOverwrite(mainWindow_.handle()))
        return;
    setWindowText(outputFile_, filename);
}

void OutputTab::findSharedLibrary() {
    // MuPDF is built in, so this is Java's case of a library that was found
    warningText_ = kImagesIn72Dpi;
    setTooltip(warning_, warningText_);
    ShowWindow(warning_, SW_SHOW);
}

void OutputTab::clearWarning() {
    warningText_.clear();
    setTooltip(warning_, warningText_);
    ShowWindow(warning_, SW_HIDE);
}

std::wstring OutputTab::setCorrectExtension(std::wstring filename) {
    const bool changeExtension = filename.ends_with(currentExtension_);
    if (changeExtension)
        filename.resize(filename.size() - currentExtension_.size());
    if (isChecked(burst_) || isChecked(multipageTiff_)) {
        if (selectedIndex(fileType_) == 7 || isChecked(multipageTiff_)) {
            currentExtension_ = L".tiff";
        } else {
            std::wstring type = itemText(fileType_, selectedIndex(fileType_));
            std::transform(type.begin(), type.end(), type.begin(),
                           [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            currentExtension_ = L"." + type;
        }
    } else {
        currentExtension_ = L".pdf";
    }
    if (changeExtension)
        filename += currentExtension_;
    return filename;
}

void OutputTab::updateExtension() {
    setWindowText(outputFile_, setCorrectExtension(windowText(outputFile_)));
}

void OutputTab::whichToEnable(int option) {
    // a new model of the colour modes selects its first, without an item event
    if (option >= 1 && option <= 5) {
        if (itemCount(colorMode_) != 2) {
            setItems(colorMode_, colorModes(2));
            lastColorMode_ = 0;
        }
    } else if (option > 5) {
        if (itemCount(colorMode_) != 4) {
            setItems(colorMode_, colorModes(4));
            lastColorMode_ = 0;
        }
    }
    switch (option) {
    case 1: setOptionsEnabled(true, false, true, false, false); break;
    case 2:
    case 3:
    case 4: setOptionsEnabled(true, false, false, true, false); break;
    case 5:
    case 6: setOptionsEnabled(true, false, false, false, false); break;
    case 7:
    case 100: setOptionsEnabled(true, true, tiffQuality(), true, false); break;
    default: setOptionsEnabled(false, false, false, false, true); break;
    }
}

void OutputTab::setOptionsEnabled(bool color, bool compression, bool quality, bool transparency,
                                  bool pdfOptions) {
    enable(colorLabel_, color);
    enable(colorMode_, color);
    enable(compressionLabel_, compression);
    enable(compressionType_, compression);
    enable(qualityLabel_, quality);
    enable(qualitySlider_, quality);
    // Java cannot JPEG-compress a TIFF with transparency
    const bool jpeg = selectedIndex(compressionType_) == static_cast<int>(image::TiffCompression::JPEG);
    enable(transparent_, transparency && !(compression && jpeg));
    for (HWND control : {uncompressed_, pageMarks_, optimizeSize_, fullyCompressed_})
        enable(control, pdfOptions);
}

bool OutputTab::tiffQuality() const {
    const auto compression = static_cast<image::TiffCompression>(selectedIndex(compressionType_));
    return compression == image::TiffCompression::JPEG || compression == image::TiffCompression::ZLIB ||
           compression == image::TiffCompression::DEFLATE;
}

void OutputTab::colorModeChanged() {
    if (selectedIndex(fileType_) == 7 || isChecked(multipageTiff_)) {
        if (selectedIndex(colorMode_) == static_cast<int>(image::ColorMode::RGB))
            enable(transparent_,
                   selectedIndex(compressionType_) != static_cast<int>(image::TiffCompression::JPEG));
        else
            enable(transparent_, false);
    }
}

void OutputTab::compressionChanged() {
    setOptionsEnabled(true, true, tiffQuality(), selectedIndex(colorMode_) == 0, false);
}

void OutputTab::fileTypeChanged() {
    whichToEnable(selectedIndex(fileType_));
    if (selectedIndex(fileType_) == 0) {
        setOptionsEnabled(false, false, false, false, true);
        if (warningText_ == kImagesIn72Dpi)
            clearWarning();
    } else {
        findSharedLibrary();
    }
    updateExtension();
}

void OutputTab::checkRun() {
    const std::wstring output = windowText(outputFile_);
    if (output.empty())
        throw Error(Error::Kind::Io, "No output file selected");
    if (mainWindow_.inputTab().batchLength() > 1) {
        if (output.find(L"<F>") == std::wstring::npos && output.find(L"<FX>") == std::wstring::npos &&
            output.find(L"<P>") == std::wstring::npos && output.find(L"<#>") == std::wstring::npos)
            throw Error(Error::Kind::Io, "Variables in output file name required for batch mode");
    }
    mainWindow_.inputTab().setUseTempFiles(isChecked(tempfiles_));
}

Operation OutputTab::prepare() {
    const bool pageMarks = isChecked(pageMarks_);
    const bool uncompressed = isChecked(uncompressed_);
    const bool multipageTiff = isChecked(multipageTiff_);
    const bool burst = isChecked(burst_);
    const bool optimizeSize = isChecked(optimizeSize_);
    const bool fullyCompressed = isChecked(fullyCompressed_);
    const bool matchedTransparency = isEnabled(transparent_) && isChecked(transparent_);
    const bool burstImages = selectedIndex(fileType_) != 0 && !multipageTiff;
    const auto type =
        multipageTiff ? image::ImageType::TIFF : static_cast<image::ImageType>(selectedIndex(fileType_));
    const image::PdfToImage images(
        burstImages, static_cast<image::ColorMode>(selectedIndex(colorMode_)), type,
        static_cast<image::TiffCompression>(selectedIndex(compressionType_)),
        static_cast<int>(SendMessageW(qualitySlider_, TBM_GETPOS, 0, 0)), matchedTransparency);
    const std::string output = toUtf8(windowText(outputFile_));
    return [=](RunContext& context) {
        if (pageMarks) {
            if (uncompressed)
                context.tweak->addPageMarks();
            else
                context.tweak->removePageMarks();
        }
        context.tweak->setPdfImages(images);
        context.tweak->writeOutput(output, multipageTiff, burst, uncompressed, optimizeSize,
                                   fullyCompressed);
    };
}

} // namespace pdftweak::gui
