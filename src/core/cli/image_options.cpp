// BurstImagesOption: -colormode, -tiffcompression, -transparency, -imagequality, and
// -burstfiletype. The MuPDF library is part of the program, so the Java check that its shared
// library exists has no counterpart.

#include "core/cli/command_option.h"

#include "core/error.h"
#include "core/image/pdf_to_image.h"
#include "core/java_text.h"

#include <optional>

namespace pdftweak::cli {
namespace {

class BurstImagesOption final : public CommandOption {
public:
    bool supportsOption(const std::string& option) const override {
        return option == "-colormode" || option == "-tiffcompression" || option == "-transparency" ||
               option == "-imagequality" || option == "-burstfiletype";
    }

    bool setOption(const std::string& option, const std::string& value, CliOutput&) override {
        if (option == "-colormode") {
            const auto mode = image::parseColorMode(value);
            if (!mode)
                refer(option);
            colorMode_ = *mode;
        } else if (option == "-tiffcompression") {
            const auto compression = image::parseTiffCompression(value);
            if (!compression)
                refer(option);
            compression_ = *compression;
        } else if (option == "-transparency") {
            if (value == "YES")
                transparent_ = true;
            else if (value == "NO")
                transparent_ = false;
            else
                refer(option);
        } else if (option == "-imagequality") {
            std::optional<int> quality;
            try {
                quality = javaParseInt(value);
            } catch (const Error&) {
            }
            if (!quality || *quality < 0 || *quality > 100)
                refer(option);
            quality_ = *quality;
        } else if (option == "-burstfiletype") {
            fileType_ = image::parseImageType(value);
            if (!fileType_)
                refer(option);
        }
        return true;
    }

    void run(pdf::Tweak& tweak, pdf::InputFile&, CliOutput&) override {
        if (fileType_ && *fileType_ != image::ImageType::PDF) {
            if (*fileType_ == image::ImageType::TIFF && colorMode_ != image::ColorMode::RGB)
                transparent_ = false;
            tweak.setPdfImages(
                image::PdfToImage(true, colorMode_, *fileType_, compression_, quality_, transparent_));
        } else {
            tweak.setPdfImages(image::PdfToImage(false, colorMode_,
                                                 fileType_.value_or(image::ImageType::PDF), compression_,
                                                 quality_, transparent_));
        }
    }

    std::string summary() const override {
        return " -colormode              Set the color mode of the exported images\n"
               " -tiffcompression        Set compression type for TIFF image files\n"
               " -transparency           Set if your image has transparent layers\n"
               " -imagequality           Set the quality for the exported images\n"
               " -burstfiletype          Set the file type of the burst pages\n";
    }

    std::string help(const std::string& option) const override {
        if (option == "-colormode")
            return " -colormode {MODE}\n"
                   "    Set the color mode of the exported images\n"
                   "\t accepted values:{RGB, GRAY, BNW, BNWI}\n"
                   "\t *option applicable only on burst pages function.\n";
        if (option == "-tiffcompression")
            return " -tiffcompression {COMPRESSIONTYPE}\n"
                   "    Set compression type for TIFF image files\n"
                   "\t accepted values:{NONE, LZW, JPEG, ZLIB, PACKBITS, DEFLATE}\n"
                   "\t *option applicable only on burst pages function \n";
        if (option == "-transparency")
            return " -transparency {YES/NO}\n"
                   "    Set if your image has transparent layers\n"
                   "\t accepted values:{YES, NO}\n"
                   "\t *option applicable only on burst pages function \n"
                   "     and PNG,TIFF,PAM,GIF images\n";
        if (option == "-imagequality")
            return " -imagequality {0-100}\n"
                   "    Set the quality for the exported images\n"
                   "\t accepted values:{Integers [0,100]}\n"
                   "\t *option applicable only on burst pages function \n";
        return " -burstfiletype {FILETYPE}\n"
               "    Set the file type of the burst pages\n"
               "\t accepted values:{PDF, JPG, PNG, GIF, PAM, PNM, BMP, TIFF}\n"
               "\t *option applicable only on burst pages function\n";
    }

private:
    // the DocumentException that Java threw for a value it does not accept
    [[noreturn]] static void refer(const std::string& option) {
        throw Error(Error::Kind::Document, "Please refer to \"-help " + option + "\"");
    }

    image::ColorMode colorMode_ = image::ColorMode::RGB;
    std::optional<image::ImageType> fileType_;
    image::TiffCompression compression_ = image::TiffCompression::NONE;
    int quality_ = 100;
    bool transparent_ = false;
};

} // namespace

std::unique_ptr<CommandOption> makeBurstImagesOption() { return std::make_unique<BurstImagesOption>(); }

} // namespace pdftweak::cli
