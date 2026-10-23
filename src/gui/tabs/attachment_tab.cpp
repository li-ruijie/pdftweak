#include "gui/dialogs.h"
#include "gui/main_window.h"
#include "gui/tabs.h"

#include <commctrl.h>

#include <algorithm>

namespace pdftweak::gui {

void AttachmentTab::build() {
    // BorderLayout: the add button above the list, the remove button below it
    FormLayout& layout = newLayout(L"f:p:g", L"f:p, f:p:g, f:p");
    add_ = button(L"Add attachment...", [this] {
        const auto files = openFiles(mainWindow_.handle(), Chooser::Other, false);
        if (!files.empty())
            addAttachment(files[0]);
    });
    layout.add(add_, 1, 1);
    list_ = createChild(handle(), WC_LISTBOXW, L"",
                        WS_TABSTOP | WS_VSCROLL | LBS_EXTENDEDSEL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
                        WS_EX_CLIENTEDGE);
    layout.add(list_, 1, 2);
    remove_ = button(L"Remove", [this] {
        // the selected entries, from the last, as JList.getSelectedIndices
        const auto count = static_cast<int>(SendMessageW(list_, LB_GETSELCOUNT, 0, 0));
        std::vector<int> selected(static_cast<size_t>(std::max(count, 0)));
        if (count > 0)
            SendMessageW(list_, LB_GETSELITEMS, static_cast<WPARAM>(count),
                         reinterpret_cast<LPARAM>(selected.data()));
        for (auto it = selected.rbegin(); it != selected.rend(); ++it) {
            SendMessageW(list_, LB_DELETESTRING, static_cast<WPARAM>(*it), 0);
            files_.erase(files_.begin() + *it);
        }
    });
    layout.add(remove_, 1, 3);
    setLayout(layout.asComponent());
}

void AttachmentTab::addAttachment(const std::filesystem::path& file) {
    files_.push_back(file);
    SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(file.c_str()));
}

Operation AttachmentTab::prepare() {
    if (files_.empty())
        return {};
    const std::vector<std::filesystem::path> files = files_;
    return [files](RunContext& context) {
        for (const auto& file : files)
            context.tweak->addFile(file);
    };
}

} // namespace pdftweak::gui
