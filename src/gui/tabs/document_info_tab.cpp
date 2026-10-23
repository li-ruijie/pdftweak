#include "core/formats/tables.h"
#include "core/java_text.h"
#include "core/text.h"
#include "gui/main_window.h"
#include "gui/tabs.h"

namespace pdftweak::gui {

void DocumentInfoTab::build() {
    FormLayout& layout = newLayout(L"f:p:g, f:p", L"f:p, f:p, f:p:g");
    infoChange_ = checkBox(L"Change Document Info", [this] {
        const bool b = isChecked(infoChange_);
        enable(infoLoad_, b);
        enable(infoAdd_, b);
        infoEntries_->setEnabled(b);
    });
    layout.add(infoChange_, 1, 1);
    infoLoad_ = button(L"Load from document", [this] {
        infoEntries_->clear();
        if (!mainWindow_.inputFile())
            return;
        for (const auto& [key, value] : mainWindow_.inputFile()->infoDictionary())
            infoEntries_->addRow({fromUtf8(key), fromUtf8(value)});
    });
    layout.add(infoLoad_, 2, 1);
    infoAdd_ = button(L"Add predefined...", [this] {
        // a popup menu of the known names under the button
        const HMENU menu = CreatePopupMenu();
        const auto& names = formats::infoNames();
        for (size_t i = 0; i < names.size(); ++i)
            AppendMenuW(menu, MF_STRING, i + 1, fromUtf8(names[i]).c_str());
        RECT button;
        GetWindowRect(infoAdd_, &button);
        const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, button.left,
                                          button.bottom, 0, handle(), nullptr);
        DestroyMenu(menu);
        if (chosen > 0)
            addPredefined(chosen - 1);
    });
    layout.add(infoAdd_, 1, 2, 2);
    infoEntries_ = std::make_unique<TableEditor>(
        std::vector<Column>{{L"Name", ColumnType::Text}, {L"Value", ColumnType::Text}},
        std::vector<Value>{std::wstring(), std::wstring()});
    layout.add(childPage(*infoEntries_), 1, 3, 2);
    setLayout(layout.asComponent());
    enable(infoLoad_, false);
    enable(infoAdd_, false);
    infoEntries_->setEnabled(false);
}

void DocumentInfoTab::addPredefined(int index) {
    infoEntries_->addRow({fromUtf8(formats::infoNames()[static_cast<size_t>(index)]), std::wstring()});
}

Operation DocumentInfoTab::prepare() {
    if (!isChecked(infoChange_))
        return {};
    // a HashMap: a repeated name keeps its first place and takes the last value
    std::vector<std::pair<std::string, std::string>> entries;
    for (int i = 0; i < infoEntries_->rowCount(); ++i) {
        const std::vector<Value>& row = infoEntries_->row(i);
        const auto* key = std::get_if<std::wstring>(&row[0]);
        const auto* value = std::get_if<std::wstring>(&row[1]);
        const std::string name = key == nullptr ? std::string() : toUtf8(*key);
        const std::string text = value == nullptr ? std::string() : toUtf8(*value);
        bool known = false;
        for (auto& entry : entries) {
            if (entry.first == name) {
                entry.second = text;
                known = true;
            }
        }
        if (!known)
            entries.emplace_back(name, text);
    }
    std::vector<std::string> keys;
    for (const auto& entry : entries)
        keys.push_back(entry.first);
    std::vector<std::pair<std::string, std::string>> ordered;
    for (const size_t i : javaHashMapOrder(keys))
        ordered.push_back(entries[i]);
    return [ordered](RunContext& context) { context.tweak->updateInfoDictionary(ordered); };
}

} // namespace pdftweak::gui
