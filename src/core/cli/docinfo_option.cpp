// DocInfoOption: -docinfo NAME=VALUE.

#include "core/cli/command_option.h"

#include "core/java_text.h"

namespace pdftweak::cli {
namespace {

class DocInfoOption final : public CommandOption {
public:
    bool supportsOption(const std::string& option) const override { return option == "-docinfo"; }

    bool setOption(const std::string&, const std::string& value, CliOutput& output) override {
        const size_t pos = value.find('=');
        if (pos == std::string::npos) {
            output.err("Missing equals sign in -docinfo value: " + value + "\r\n");
            return false;
        }
        // a HashMap: a repeated key keeps its first place and takes the last value
        const std::string key = value.substr(0, pos);
        for (auto& entry : docinfo_) {
            if (entry.first == key) {
                entry.second = value.substr(pos + 1);
                return true;
            }
        }
        docinfo_.emplace_back(key, value.substr(pos + 1));
        return true;
    }

    void run(pdf::Tweak& tweak, pdf::InputFile&, CliOutput&) override {
        if (docinfo_.empty())
            return;
        std::vector<std::string> keys;
        for (const auto& entry : docinfo_)
            keys.push_back(entry.first);
        std::vector<std::pair<std::string, std::string>> ordered;
        for (const size_t i : javaHashMapOrder(keys))
            ordered.push_back(docinfo_[i]);
        tweak.updateInfoDictionary(ordered);
    }

    std::string summary() const override { return " -docinfo                Change document info\n"; }

    std::string help(const std::string&) const override {
        return " -docinfo {NAME}={VALUE}\n"
               "    Set {NAME} to {VALUE} in document info.";
    }

private:
    std::vector<std::pair<std::string, std::string>> docinfo_;
};

} // namespace

std::unique_ptr<CommandOption> makeDocInfoOption() { return std::make_unique<DocInfoOption>(); }

} // namespace pdftweak::cli
