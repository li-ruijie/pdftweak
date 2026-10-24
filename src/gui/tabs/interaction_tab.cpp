#include "core/error.h"
#include "core/formats/tables.h"
#include "core/formats/viewer_preferences.h"
#include "core/text.h"
#include "gui/main_window.h"
#include "gui/tabs.h"

#include <algorithm>
#include <optional>

namespace pdftweak::gui {
namespace {

std::vector<std::wstring> wide(const std::vector<std::string>& items) {
    std::vector<std::wstring> result;
    for (const std::string& item : items)
        result.push_back(fromUtf8(item));
    return result;
}

// A number cell left empty, where Java's unboxing threw a NullPointerException it did not report.
int integer(const Value& value) {
    if (const auto* number = std::get_if<int>(&value))
        return *number;
    throw Error(Error::Kind::Io, "Please fill in every cell of the transitions table.");
}

struct Transition {
    int from, to, type, duration, pageDuration;
};

} // namespace

void InteractionTab::build() {
    FormLayout& panel1 = newLayout(L"f:p:g", L"f:p, f:p:g");
    addTransitions_ = checkBox(L"Add page transitions", [this] { updateEnabledState(); });
    panel1.add(addTransitions_, 1, 1);
    transitions_ = std::make_unique<TableEditor>(
        std::vector<Column>{{L"First Page", ColumnType::Integer},
                            {L"Last Page", ColumnType::Integer},
                            {L"Transition", ColumnType::Choice, wide(formats::transitionNames()), 200},
                            {L"TransDur.", ColumnType::Integer},
                            {L"PageDur.", ColumnType::Integer}},
        std::vector<Value>{1, -1, std::wstring(L"None"), 0, -1});
    transitions_->setPreferredSize({200, 300});
    panel1.add(childPage(*transitions_), 1, 2);

    FormLayout& panel2 = newLayout(L"f:p, f:p:g", L"f:p, f:p, f:p, 10dlu");
    addPrefs_ = checkBox(L"Set Viewer Preferences", [this] { updateEnabledState(); });
    panel2.add(addPrefs_, 1, 1, 2);
    panel2.add(label(L"Page Mode: "), 1, 2);
    pageMode_ = comboBox(wide(formats::pageModeNames()));
    panel2.add(pageMode_, 2, 2);
    panel2.add(label(L"Page Layout: "), 1, 3);
    pageLayout_ = comboBox(wide(formats::pageLayoutNames()));
    panel2.add(pageLayout_, 2, 3);
    panel2.add(separator(), 1, 4, 2);
    const auto& preferences = formats::viewerPreferences();
    for (size_t i = 0; i < preferences.size(); ++i) {
        panel2.appendRow(L"f:p");
        optionalPrefCheck_.push_back(
            checkBox(fromUtf8(preferences[i].name) + L": ", [this] { updateEnabledState(); }));
        panel2.add(optionalPrefCheck_.back(), 1, static_cast<int>(i) + 5);
        optionalPrefValue_.push_back(comboBox(wide(preferences[i].values)));
        panel2.add(optionalPrefValue_.back(), 2, static_cast<int>(i) + 5);
    }
    FormLayout& layout = newLayout(L"f:p:g", L"f:p:g");
    layout.add(splitPane(panel1.asComponent(), panel2.asComponent()), 1, 1);
    setLayout(layout.asComponent());
    updateEnabledState();
}

void InteractionTab::updateEnabledState() {
    transitions_->setEnabled(isChecked(addTransitions_));
    const bool b = isChecked(addPrefs_);
    enable(pageMode_, b);
    enable(pageLayout_, b);
    for (size_t i = 0; i < optionalPrefCheck_.size(); ++i) {
        enable(optionalPrefCheck_[i], b);
        enable(optionalPrefValue_[i], b && isChecked(optionalPrefCheck_[i]));
    }
}

void InteractionTab::checkRun() { transitions_->checkRun("page transition"); }

Operation InteractionTab::prepare() {
    std::optional<std::vector<Transition>> transitions;
    if (isChecked(addTransitions_)) {
        transitions.emplace();
        const std::vector<std::wstring> names = wide(formats::transitionNames());
        for (int i = 0; i < transitions_->rowCount(); ++i) {
            const std::vector<Value>& row = transitions_->row(i);
            Transition t{};
            t.from = integer(row[0]);
            t.to = integer(row[1]);
            const auto* name = std::get_if<std::wstring>(&row[2]);
            const auto found = name == nullptr ? names.end() : std::find(names.begin(), names.end(), *name);
            if (found == names.end())
                throw Error(Error::Kind::Io,
                            "Unknown transition: " + (name == nullptr ? std::string() : toUtf8(*name)));
            t.type = static_cast<int>(found - names.begin());
            t.duration = integer(row[3]);
            t.pageDuration = integer(row[4]);
            transitions->push_back(t);
        }
    }
    std::optional<std::pair<int, std::vector<std::pair<std::string, std::string>>>> preferences;
    if (isChecked(addPrefs_)) {
        // PdfWriter.PageLayoutSinglePage << page layout plus PdfWriter.PageModeUseNone << page mode
        const int simplePrefs = (1 << selectedIndex(pageLayout_)) + (64 << selectedIndex(pageMode_));
        std::vector<std::pair<std::string, std::string>> optional;
        const auto& all = formats::viewerPreferences();
        for (size_t i = 0; i < optionalPrefCheck_.size(); ++i)
            if (isChecked(optionalPrefCheck_[i]))
                optional.emplace_back(
                    all[i].key, all[i].values[static_cast<size_t>(selectedIndex(optionalPrefValue_[i]))]);
        preferences.emplace(simplePrefs, std::move(optional));
    }
    if (!transitions && !preferences)
        return {};
    return [transitions, preferences](RunContext& context) {
        pdf::Tweak& tweak = *context.tweak;
        if (transitions) {
            for (Transition t : *transitions) {
                const int pages = tweak.pageCount();
                const int typed = t.from;
                if (t.from < 0)
                    t.from += pages + 1;
                if (t.to < 0)
                    t.to += pages + 1;
                if (t.from > pages)
                    continue;
                if (t.to > pages)
                    t.to = pages;
                for (int j = t.from; j <= t.to; ++j) {
                    // a page before the first, where Java's setTransition indexed outside its array, named
                    // as the First Page cell gives it
                    if (j < 1)
                        throw Error(Error::Kind::Io, "Page " + std::to_string(typed) + " does not exist.");
                    tweak.setTransition(j, t.type, t.duration, t.pageDuration);
                }
            }
        }
        if (preferences)
            tweak.setViewerPreferences(preferences->first, preferences->second);
    };
}

} // namespace pdftweak::gui
