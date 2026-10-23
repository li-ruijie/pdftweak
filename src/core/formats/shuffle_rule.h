#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pdftweak::formats {

// ShuffleRule: where one page of a pass goes on the output pages, as written in a rule set such as
// "2:!+1L0.707+0%-100%,+2L.707+100%-100%".
struct ShuffleRule {
    enum class Base { Absolute, Beginning, End };

    bool newPageBefore = false;
    Base base = Base::Beginning;
    int page = 1;
    char rotate = 'N'; // N, R, U, or L
    double scale = 1;
    double offsetX = 0;
    bool offsetXPercent = false;
    double offsetY = 0;
    bool offsetYPercent = false;
    double frameWidth = 0;

    std::string pageString() const;    // "+1", "-4", or "3"
    std::string offsetXString() const; // "+0.0%", as Java prints it
    std::string offsetYString() const;
    int rotateAngle() const; // 0, 90, 180, or 270
    std::string toString() const;
};

// A whole rule set: the pages each pass covers (negative for reverse passes), the size of the
// blocks shuffled separately (0 for none), and the rules.
struct RuleSet {
    int passLength = 1;
    int blockSize = 0;
    std::vector<ShuffleRule> rules;
};

// ShuffleRule.parseRule and parseRuleSet. Throws Error(Document) with the message of the Java
// NumberFormatException, which is the rule itself for a rule that does not parse.
ShuffleRule parseRule(std::string_view rule);
RuleSet parseRuleSet(std::string_view ruleSet);

// The predefined rule sets of the Shuffle tab, as (name, rule set) in the Java order.
const std::vector<std::pair<std::string, std::string>>& presetRuleSets();

} // namespace pdftweak::formats
