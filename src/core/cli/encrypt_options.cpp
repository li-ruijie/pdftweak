// EncryptOptions: -ownerpw, -userpw, -permissions, -encmode, and -encnometadata.

#include "core/cli/command_option.h"

#include "core/formats/tables.h"
#include "core/java_text.h"
#include "core/text.h"

#include <optional>

namespace pdftweak::cli {
namespace {

class EncryptOptions final : public CommandOption {
public:
    bool supportsOption(const std::string& option) const override {
        return option == "-ownerpw" || option == "-userpw" || option == "-permissions" ||
               option == "-encmode" || option == "-encnometadata";
    }

    bool setOption(const std::string& option, const std::string& value, CliOutput& output) override {
        if (option == "-ownerpw") {
            ownerpw_ = value;
        } else if (option == "-userpw") {
            userpw_ = value;
            if (!ownerpw_)
                ownerpw_ = value;
        } else if (option == "-permissions") {
            permissions_ = 0;
            for (const std::string& perm : javaSplit(value, ',', false)) {
                bool known = false;
                for (size_t i = 0; i < formats::permissionNames().size(); ++i) {
                    if (perm == formats::permissionNames()[i]) {
                        permissions_ += formats::permissionBits()[i];
                        known = true;
                        break;
                    }
                }
                if (!known)
                    output.err("Skipping invalid permission: " + perm + "\r\n");
            }
        } else {
            encmode_ = javaParseInt(value) + (option == "-encnometadata" ? 8 : 0);
        }
        return true;
    }

    void run(pdf::Tweak& tweak, pdf::InputFile&, CliOutput&) override {
        if (ownerpw_)
            tweak.setEncryption(encmode_, permissions_, toLatin1(*ownerpw_), toLatin1(userpw_));
    }

    std::string summary() const override {
        return " -ownerpw                Encrypt PDF and set owner password.\n"
               " -userpw                 Encrypt PDF and set user password.\n"
               " -permissions            Set file permissions.\n"
               " -encmode                Set encryption mode.\n"
               " -encnometadata          Set encryption mode and do not encrypt metadata.\n";
    }

    std::string help(const std::string& option) const override {
        if (option == "-ownerpw") {
            return " -ownerpw {PASSWORD}\n"
                   "    Encrypt PDF and set owner password.";
        } else if (option == "-userpw") {
            return " -userpw {PASSWORD}\n"
                   "    Encrypt PDF and set user password.";
        } else if (option == "-permissions") {
            return " -permissions {PERMISSION}[,{PERMISSION}[,...]]\n"
                   "    Set file permissions for encrypted PDF. \n"
                   "    Permission names are the same as in the GUI.";
        } else {
            return " -encmode {MODE}\n"
                   " -encnometadata {MODE}\n"
                   "    Set encryption mode. If -encnometadata is used,\n"
                   "    metadata is left unencrypted. Available modes:\n"
                   "        0 = 40-bit RC4 (default)\n"
                   "        1 = 128-bit RC4\n"
                   "        2 = 128-bit AES (Acrobat 7.0)";
        }
    }

private:
    std::string userpw_;
    std::optional<std::string> ownerpw_;
    int permissions_ = 0;
    int encmode_ = 0;
};

} // namespace

std::unique_ptr<CommandOption> makeEncryptOptions() { return std::make_unique<EncryptOptions>(); }

} // namespace pdftweak::cli
