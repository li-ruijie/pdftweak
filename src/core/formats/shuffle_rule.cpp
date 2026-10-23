#include "core/formats/shuffle_rule.h"

#include "core/error.h"
#include "core/java_text.h"

#include <regex>

namespace pdftweak::formats {
namespace {

std::string signedOffset(double value, bool percent) {
    std::string text = javaDouble(value) + (percent ? "%" : "");
    if (text[0] != '-')
        text = "+" + text;
    return text;
}

// ShuffleRule.predefinedRuleSets, each "rules=name".
const char* const kPresets[] = {
    "1:!+1N1+0%+0%=Default",
    "1:!-1N1+0%+0%=Reverse Page Order",
    "2:!+1L0.707+0%-100%,+2L.707+100%-100%=2-up Portrait",
    "2:!+1L0.707+0%-100%,+2L.707+0%-200%=2-up Landscape",
    "4:!+1N0.5+0%+100%,+2N0.5+100%+100%,+3N0.5+0%+0%,+4N0.5+100%+0%,=4-up",
    "9:!+1N.333+0%+200%,+2N.333+100%+200%,+3N.333+200%+200%,+4N.333+0%+100%,+5N.333+100%+100%,"
    "+6N.333+200%+100%,+7N.333+0%+0%,+8N.333+100%+0%,+9N.333+200%+0%,=9-up",
    "-4:!-4N1+0%+0%,!+1N1+0%+0%,!+2N1+0%+0%,!-3N1+0%+0%=Booklet Reorder",
    "-4:!-4L0.707+0%-100%,+1L.707+100%-100%,!+2L0.707+0%-100%,-3L.707+100%-100%=Booklet Portrait",
    "-4:!-4L0.707+0%-100%,+1L.707+0%-200%,!+2L0.707+0%-100%,-3L.707+0%-200%=Booklet Landscape",
    "4:!-4N0.5+0%+100%,+1N0.5+100%+100%,+2U0.5-200%-100%,-3U0.5-100%-100%=Booklet 2-Up Fold",
    "-8:!-8N0.5+0%+100%,+1N0.5+100%+100%,-6N0.5+0%+0%,+3N0.5+100%+0%,!+2N0.5+0%+100%,"
    "-7N0.5+100%+100%,+4N0.5+0%+0%,-5N0.5+100%+0%=Booklet 2-Up Cut",
    "8:!+1L0.353+300%-100%,+2R0.353-400%+100%,+3R0.353-300%+100%,+4R0.353-200%+100%,"
    "+5R0.353-100%+100%,+6L0.353+0%-100%,+7L0.353+100%-100%,+8L0.353+200%-100%=PocketMod",
    "8:!+1L0.353+300%-200%,+2R0.353-400%+0%,+3R0.353-300%+0%,+4R0.353-200%+0%,+5R0.353-100%+0%,"
    "+6L0.353+0%-200%,+7L0.353+100%-200%,+8L0.353+200%-200%=PocketMod UpsideDown",
    "1:!+1N0.25+150%+300%,+1L0.25+250%-100%,+1U0.25-250%-100%,+1R0.25-350%+183%=Star Test Pattern",
    "2:!+1L0.707+0%-100%,+1L.707+100%-100%=Tile Copy 2x1",
    "4:!+1N0.5+0%+100%,+1N0.5+100%+100%,+1N0.5+0%+0%,+1N0.5+100%+0%=Tile Copy 2x2",
    "8:!+1L0.3535+0%-100%,+1L0.3535+0%-200%,+1L0.3535+100%-100%,+1L0.3535+100%-200%,"
    "+1L0.3535+200%-100%,+1L0.3535+200%-200%,+1L0.3535+300%-100%,+1L0.3535+300%-200%,=Tile Copy 4x2",
    "9:!+1N0.333+0.0%+200.0%,+1N0.333+100.0%+200.0%,+1N0.333+200.0%+200.0%,+1N0.333+0.0%+100.0%,"
    "+1N0.333+100.0%+100.0%,+1N0.333+200.0%+100.0%,+1N0.333+0.0%+0.0%,+1N0.333+100.0%+0.0%,"
    "+1N0.333+200.0%+0.0%=Tile Copy 3x3",
    "16:!+1N0.25+0%+300%,+1N0.25+100%+300%,+1N0.25+200%+300%,+1N0.25+300%+300%,+1N0.25+0%+200%,"
    "+1N0.25+100%+200%,+1N0.25+200%+200%,+1N0.25+300%+200%,+1N0.25+0%+100%,+1N0.25+100%+100%,"
    "+1N0.25+200%+100%,+1N0.25+300%+100%,+1N0.25+0%+0%,+1N0.25+100%+0%,+1N0.25+200%+0%,"
    "+1N0.25+300%+0%=Tile Copy 4x4",
    "32:!+1L0.17675+0%-100%,+1L0.17675+0%-200%,+1L0.17675+100%-100%,+1L0.17675+100%-200%,"
    "+1L0.17675+200%-100%,+1L0.17675+200%-200%,+1L0.17675+300%-100%,+1L0.17675+300%-200%,"
    "+1L0.17675+400%-100%,+1L0.17675+400%-200%,+1L0.17675+500%-100%,+1L0.17675+500%-200%,"
    "+1L0.17675+600%-100%,+1L0.17675+600%-200%,+1L0.17675+700%-100%,+1L0.17675+700%-200%,"
    "+1L0.17675+0%-300%,+1L0.17675+0%-400%,+1L0.17675+100%-300%,+1L0.17675+100%-400%,"
    "+1L0.17675+200%-300%,+1L0.17675+200%-400%,+1L0.17675+300%-300%,+1L0.17675+300%-400%,"
    "+1L0.17675+400%-300%,+1L0.17675+400%-400%,+1L0.17675+500%-300%,+1L0.17675+500%-400%,"
    "+1L0.17675+600%-300%,+1L0.17675+600%-400%,+1L0.17675+700%-300%,+1L0.17675+700%-400%=Tile Copy 4x8",
    "1:!+1N2.0+0.0%-25.0%,!+1N2.0-50.0%-25.0%=Split 2-up Landscape (Scale to Portrait first)",
};

} // namespace

std::string ShuffleRule::pageString() const {
    const char* sign = base == Base::Absolute ? "" : base == Base::Beginning ? "+" : "-";
    return sign + std::to_string(page);
}

std::string ShuffleRule::offsetXString() const { return signedOffset(offsetX, offsetXPercent); }
std::string ShuffleRule::offsetYString() const { return signedOffset(offsetY, offsetYPercent); }

int ShuffleRule::rotateAngle() const {
    switch (rotate) {
    case 'N': return 0;
    case 'R': return 90;
    case 'U': return 180;
    case 'L': return 270;
    default: throw Error(Error::Kind::Document, "Illegal rotation " + std::string(1, rotate));
    }
}

std::string ShuffleRule::toString() const {
    return (newPageBefore ? "!" : "") + pageString() + rotate + javaDouble(scale) + offsetXString() +
           offsetYString() + (frameWidth == 0 ? "" : "F" + javaDouble(frameWidth));
}

ShuffleRule parseRule(std::string_view text) {
    static const std::regex pattern(
        R"((\!?)([-+]?)([0-9]+)([NRLU])([0-9.]+)([+-][0-9.]+)(%?)([+-][0-9.]+)(%?)((F[0-9.]+)?))");
    const std::string rule(text);
    std::smatch m;
    if (!std::regex_match(rule, m, pattern))
        throw Error(Error::Kind::Document, rule);
    ShuffleRule r;
    r.newPageBefore = m[1].length() == 1;
    r.base = m[2].length() == 0 ? ShuffleRule::Base::Absolute
             : m[2].str()[0] == '+' ? ShuffleRule::Base::Beginning
                                    : ShuffleRule::Base::End;
    r.page = javaParseInt(m[3].str());
    r.rotate = m[4].str()[0];
    r.scale = javaParseDouble(m[5].str());
    r.offsetX = javaParseDouble(m[6].str());
    r.offsetXPercent = m[7].length() == 1;
    r.offsetY = javaParseDouble(m[8].str());
    r.offsetYPercent = m[9].length() == 1;
    r.frameWidth = m[10].length() == 0 ? 0 : javaParseDouble(m[11].str().substr(1));
    if (r.page <= 0) // the IllegalArgumentException of the ShuffleRule constructor
        throw Error(Error::Kind::Document, "Invalid page number " + std::to_string(r.page));
    return r;
}

RuleSet parseRuleSet(std::string_view text) {
    const size_t colon = text.find(':');
    if (colon == std::string_view::npos)
        throw Error(Error::Kind::Document, "No colon found");
    size_t comma = text.find(',');
    if (comma == std::string_view::npos || comma > colon)
        comma = colon;
    RuleSet set;
    set.passLength = javaParseInt(text.substr(0, comma));
    set.blockSize = comma == colon ? 0 : javaParseInt(text.substr(comma + 1, colon - comma - 1));
    for (const std::string& rule : javaSplit(text.substr(colon + 1), ',', false))
        set.rules.push_back(parseRule(rule));
    if (set.rules.empty())
        throw Error(Error::Kind::Document, "No rules found");
    if (!set.rules[0].newPageBefore)
        throw Error(Error::Kind::Document, "First rule must have new page before");
    return set;
}

const std::vector<std::pair<std::string, std::string>>& presetRuleSets() {
    static const std::vector<std::pair<std::string, std::string>> presets = [] {
        std::vector<std::pair<std::string, std::string>> list;
        for (const char* preset : kPresets) {
            const std::string text(preset);
            const size_t equals = text.find('=');
            list.emplace_back(text.substr(equals + 1), text.substr(0, equals));
        }
        return list;
    }();
    return presets;
}

} // namespace pdftweak::formats
