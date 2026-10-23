// SignOptions: -sign, -signvisible, -keystore, -keyalias, and -certlevel.

#include "core/cli/command_option.h"

#include "core/java_text.h"
#include "core/text.h"

#include <cstdlib>
#include <filesystem>
#include <optional>

namespace pdftweak::cli {
namespace {

// new File(System.getProperty("user.home"), ".keystore").getAbsolutePath()
std::string defaultKeystore() {
    wchar_t* profile = nullptr;
    size_t size = 0;
    std::wstring home;
    if (_wdupenv_s(&profile, &size, L"USERPROFILE") == 0 && profile != nullptr)
        home = profile;
    free(profile);
    return toUtf8((std::filesystem::path(home) / L".keystore").wstring());
}

class SignOptions final : public CommandOption {
public:
    bool supportsOption(const std::string& option) const override {
        return option == "-keystore" || option == "-keyalias" || option == "-certlevel" ||
               option == "-sign" || option == "-signvisible";
    }

    bool setOption(const std::string& option, const std::string& value, CliOutput&) override {
        if (option == "-keystore") {
            keystore_ = value;
        } else if (option == "-keyalias") {
            alias_ = value;
        } else if (option == "-certlevel") {
            certLevel_ = javaParseInt(value);
        } else {
            passphrase_ = value;
            visible_ = option == "-signvisible";
        }
        return true;
    }

    void run(pdf::Tweak& tweak, pdf::InputFile&, CliOutput&) override {
        if (passphrase_)
            tweak.setSignature(fromUtf8(keystore_), alias_, *passphrase_, certLevel_, visible_);
    }

    std::string summary() const override {
        return " -sign                   Sign PDF\n"
               " -signvisible            Sign PDF and show signature on page 1\n"
               " -keystore               Set keystore filename for signing\n"
               " -keyalias               Set key alias for signing\n"
               " -certlevel              Set certification level\n";
    }

    std::string help(const std::string& option) const override {
        if (option == "-keystore")
            return " -keystore {FILENAME}\n"
                   "    Set the keystore to get the signing key from.";
        if (option == "-keyalias")
            return " -keyalias {ALIAS}\n"
                   "    Set the alias of the signing key in the keystore.\n"
                   "    Default is 'mykey'.";
        if (option == "-certlevel")
            return " -certlevel {LEVEL}\n"
                   "    Set the certification level of the new signature.\n"
                   "    Available levels:\n"
                   "        0 = Not certified (default)\n"
                   "        1 = No changes allowed\n"
                   "        2 = Form filling allowed\n"
                   "        3 = Form filling and annotations allowed";
        return " -sign {PASSPHRASE}\n"
               " -signvisible {PASSPHRASE}\n"
               "    Sign this pdf with the selected key.\n"
               "    Use {PASSPHRASE} to unlock key in keystore.\n"
               "    Signature will be visible on page 1 if -signvisible\n"
               "    is used.";
    }

private:
    std::string keystore_ = defaultKeystore();
    std::string alias_ = "mykey";
    int certLevel_ = 0;
    bool visible_ = false;
    std::optional<std::string> passphrase_;
};

} // namespace

std::unique_ptr<CommandOption> makeSignOptions() { return std::make_unique<SignOptions>(); }

} // namespace pdftweak::cli
