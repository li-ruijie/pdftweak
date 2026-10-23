#include "core/error.h"
#include "core/formats/page_dimension.h"
#include "core/java_text.h"
#include "core/text.h"
#include "gui/dialogs.h"
#include "gui/main_window.h"
#include "gui/tabs.h"

namespace pdftweak::gui {
namespace {

// The NullPointerException of parseGUI, for a cell left empty.
struct MissingField {};

const std::wstring& cellText(const Value& value) {
    if (const auto* text = std::get_if<std::wstring>(&value))
        return *text;
    throw MissingField{};
}

double cellNumber(const Value& value) {
    if (const auto* number = std::get_if<double>(&value))
        return *number;
    throw MissingField{};
}

const wchar_t* rotateName(char rotate) {
    switch (rotate) {
    case 'N': return L"None";
    case 'L': return L"Left";
    case 'R': return L"Right";
    default: return L"Upside-Down";
    }
}

} // namespace

void ShuffleTab::build() {
    FormLayout& panel1 = newLayout(L"f:p, f:p:g, f:p, f:p", L"f:p, f:p, f:p, 10dlu, f:p, f:p, f:p:g");
    FormLayout& panel2 = newLayout(L"f:p:g", L"f:p, f:p:g");
    std::vector<std::wstring> sizes;
    for (const formats::PageDimension& size : formats::commonSizes())
        sizes.push_back(fromUtf8(size.name));
    previewFormat_ = comboBox(sizes, [this] {
        const formats::PageDimension& size =
            formats::commonSizes()[static_cast<size_t>(selectedIndex(previewFormat_))];
        preview_.setPageFormat(size.width, size.height);
    });
    panel2.add(previewFormat_, 1, 1);
    const HWND preview = preview_.create(handle());
    panel2.add(sized(preview, {50, 100}), 1, 2);
    chooseItem(previewFormat_, 0);

    shufflePages_ = checkBox(L"Shuffle pages", [this] { updateEnabledState(); });
    panel1.add(shufflePages_, 1, 1, 4);
    panel1.add(label(L"Preset: "), 1, 2);
    std::vector<std::wstring> presets;
    for (const auto& [name, rules] : formats::presetRuleSets())
        presets.push_back(fromUtf8(name));
    preset_ = comboBox(presets, [this] {
        const std::string chosen = toUtf8(itemText(preset_, selectedIndex(preset_)));
        for (const auto& [name, rules] : formats::presetRuleSets()) {
            if (name == chosen) {
                setWindowText(configString_, fromUtf8(rules));
                parseConfigString();
            }
        }
    });
    panel1.add(preset_, 2, 2, 3);
    panel1.add(label(L"Config string: "), 1, 3);
    configString_ = edit(L"");
    panel1.add(configString_, 2, 3, 2);
    use_ = button(L"Use", [this] { parseConfigString(); });
    panel1.add(use_, 4, 3);
    panel1.add(separator(), 1, 4, 4);
    panel1.add(label(L"Each pass covers  "), 1, 5);
    pagesPerPass_ = edit(L"1");
    panel1.add(pagesPerPass_, 2, 5);
    panel1.add(label(L"  page(s) "), 3, 5);
    updatePreview_ = button(L"Update", [this] { parseGui(); });
    panel1.add(updatePreview_, 4, 5);
    blockShuffle_ = checkBox(L"Shuffle blocks of  ", [this] { updateEnabledState(); });
    panel1.add(blockShuffle_, 1, 6);
    blockSize_ = edit(L"20");
    panel1.add(blockSize_, 2, 6);
    panel1.add(label(L" pages individually"), 3, 6, 2);
    shuffleRulesTable_ = std::make_unique<TableEditor>(
        std::vector<Column>{{L"Page", ColumnType::Text},
                            {L"OffsetX", ColumnType::Text},
                            {L"OffsetY", ColumnType::Text},
                            {L"ScaleFactor", ColumnType::Number},
                            {L"Rotate", ColumnType::Choice, {L"None", L"Left", L"Upside-Down", L"Right"}},
                            {L"NewPageBefore", ColumnType::Boolean},
                            {L"FrameWidth", ColumnType::Number}},
        std::vector<Value>{std::wstring(L"+1"), std::wstring(L"0%"), std::wstring(L"0%"), 1.0,
                           std::wstring(L"None"), true, 0.0});
    shuffleRulesTable_->setPreferredSize({400, 100});
    panel1.add(childPage(*shuffleRulesTable_), 1, 7, 4);

    FormLayout& layout = newLayout(L"f:p:g", L"f:p:g");
    layout.add(splitPane(panel1.asComponent(), panel2.asComponent()), 1, 1);
    setLayout(layout.asComponent());
    chooseItem(preset_, 0);
    updateEnabledState();
}

void ShuffleTab::parseGui() {
    std::string text;
    try {
        const int pages = javaParseInt(toUtf8(windowText(pagesPerPass_)));
        text += std::to_string(pages);
        if (isChecked(blockShuffle_))
            text += "," + std::to_string(javaParseInt(toUtf8(windowText(blockSize_))));
        text += ':';
        for (int i = 0; i < shuffleRulesTable_->rowCount(); ++i) {
            const std::vector<Value>& row = shuffleRulesTable_->row(i);
            formats::ShuffleRule rule;
            const std::string page = toUtf8(cellText(row[0]));
            std::string tmp = page;
            rule.base = formats::ShuffleRule::Base::Absolute;
            if (tmp.starts_with("+")) {
                rule.base = formats::ShuffleRule::Base::Beginning;
                tmp = tmp.substr(1);
            } else if (tmp.starts_with("-")) {
                rule.base = formats::ShuffleRule::Base::End;
                tmp = tmp.substr(1);
            }
            rule.page = javaParseInt(tmp);
            if (rule.page <= 0)
                throw Error(Error::Kind::Document, page);
            tmp = toUtf8(cellText(row[1]));
            if (tmp.ends_with("%")) {
                rule.offsetXPercent = true;
                tmp.pop_back();
            }
            rule.offsetX = javaParseDouble(tmp);
            tmp = toUtf8(cellText(row[2]));
            if (tmp.ends_with("%")) {
                rule.offsetYPercent = true;
                tmp.pop_back();
            }
            rule.offsetY = javaParseDouble(tmp);
            rule.scale = cellNumber(row[3]);
            rule.rotate = static_cast<char>(cellText(row[4]).at(0));
            if (!std::holds_alternative<bool>(row[5]))
                throw MissingField{};
            rule.newPageBefore = std::get<bool>(row[5]);
            rule.frameWidth = cellNumber(row[6]);
            if (i > 0)
                text += ",";
            text += rule.toString();
        }
    } catch (const Error& error) {
        messageBox(mainWindow_.handle(), L"Unparsable option: " + fromUtf8(error.what()), L"Message",
                   MB_ICONINFORMATION);
        return;
    } catch (const MissingField&) {
        messageBox(mainWindow_.handle(), L"Please fill in all the fields.", L"Message", MB_ICONINFORMATION);
        return;
    }
    setWindowText(configString_, fromUtf8(text));
    parseConfigString();
}

void ShuffleTab::parseConfigString() {
    const std::wstring config = windowText(configString_);
    formats::RuleSet rules;
    try {
        rules = formats::parseRuleSet(toUtf8(config));
    } catch (const formats::InvalidRule&) {
        messageBox(mainWindow_.handle(), L"Unparsable config string: " + config, L"Message",
                   MB_ICONINFORMATION);
        return;
    } catch (const Error& error) {
        messageBox(mainWindow_.handle(), L"Unparsable config string: " + fromUtf8(error.what()), L"Message",
                   MB_ICONINFORMATION);
        return;
    }
    preview_.setConfig(rules.rules);
    setWindowText(pagesPerPass_, std::to_wstring(rules.passLength));
    if (rules.blockSize == 0) {
        setChecked(blockShuffle_, false);
    } else {
        setChecked(blockShuffle_, true);
        setWindowText(blockSize_, std::to_wstring(rules.blockSize));
    }
    shuffleRulesTable_->clear();
    for (const formats::ShuffleRule& rule : rules.rules)
        shuffleRulesTable_->addRow(
            {fromUtf8(rule.pageString()), fromUtf8(rule.offsetXString()), fromUtf8(rule.offsetYString()),
             rule.scale, std::wstring(rotateName(rule.rotate)), rule.newPageBefore, rule.frameWidth});
    shufflePagesPerPass_ = rules.passLength;
    shuffleRules_ = rules.rules;
    updateEnabledState();
}

void ShuffleTab::updateEnabledState() {
    const bool b = isChecked(shufflePages_);
    for (HWND control :
         {updatePreview_, use_, previewFormat_, preset_, pagesPerPass_, blockShuffle_, configString_})
        enable(control, b);
    enable(blockSize_, b && isChecked(blockShuffle_));
    shuffleRulesTable_->setEnabled(b);
}

void ShuffleTab::checkRun() {
    shuffleRulesTable_->checkRun("shuffle rule");
    if (isChecked(blockShuffle_)) {
        try {
            if (javaParseInt(toUtf8(windowText(blockSize_))) < 2)
                throw Error(Error::Kind::Document, "");
        } catch (const Error&) {
            throw Error(Error::Kind::Io, "Invalid shuffle block size");
        }
    }
}

Operation ShuffleTab::prepare() {
    if (!isChecked(shufflePages_))
        return {};
    formats::RuleSet rules;
    rules.passLength = shufflePagesPerPass_;
    rules.blockSize = isChecked(blockShuffle_) ? javaParseInt(toUtf8(windowText(blockSize_))) : 0;
    rules.rules = shuffleRules_;
    return [rules](RunContext& context) { context.tweak->shufflePages(rules); };
}

} // namespace pdftweak::gui
