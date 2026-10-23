#include "core/java_text.h"

#include "core/error.h"
#include "core/text.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <string>

namespace pdftweak {
namespace {

// Formats the shortest round-trip digits of value as Java does.
template <typename T> std::string javaNumber(T value) {
    if (std::isnan(value))
        return "NaN";
    if (std::isinf(value))
        return value > 0 ? "Infinity" : "-Infinity";
    if (value == 0)
        return std::signbit(value) ? "-0.0" : "0.0";

    // Shortest digits in scientific form, such as "7.07e-01" or "-2e+23".
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::scientific);
    std::string text(buffer, result.ptr);
    std::string sign;
    if (text[0] == '-') {
        sign = "-";
        text.erase(0, 1);
    }
    const size_t e = text.find('e');
    const int exponent = std::stoi(text.substr(e + 1));
    std::string digits = text.substr(0, e);
    digits.erase(std::remove(digits.begin(), digits.end(), '.'), digits.end());

    const T magnitude = std::fabs(value);
    if (magnitude >= T(1e-3) && magnitude < T(1e7)) {
        std::string integer, fraction;
        if (exponent >= 0) {
            const size_t integerDigits = static_cast<size_t>(exponent) + 1;
            if (digits.size() <= integerDigits) {
                integer = digits + std::string(integerDigits - digits.size(), '0');
                fraction = "0";
            } else {
                integer = digits.substr(0, integerDigits);
                fraction = digits.substr(integerDigits);
            }
        } else {
            integer = "0";
            fraction = std::string(static_cast<size_t>(-exponent - 1), '0') + digits;
        }
        return sign + integer + "." + fraction;
    }
    const std::string fraction = digits.size() > 1 ? digits.substr(1) : "0";
    return sign + digits.substr(0, 1) + "." + fraction + "E" + std::to_string(exponent);
}

[[noreturn]] void formatError(const std::string& exception, const std::string& message) {
    throw Error(Error::Kind::Io, "java.util." + exception + ": " + message);
}

std::string groupThousands(const std::string& digits) {
    std::string result;
    const size_t first = digits.size() % 3 == 0 ? 3 : digits.size() % 3;
    result = digits.substr(0, first);
    for (size_t i = first; i < digits.size(); i += 3)
        result += "," + digits.substr(i, 3);
    return result;
}

// Pads to a width counted in characters, as Java counts them.
std::string pad(const std::string& text, int width, bool left) {
    const int length = static_cast<int>(codePoints(text).size());
    if (width <= 0 || length >= width)
        return text;
    const std::string fill(static_cast<size_t>(width - length), ' ');
    return left ? text + fill : fill + text;
}

std::string upper(std::string text) {
    for (char& c : text)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return text;
}

struct Spec {
    std::string text; // the whole specifier, for error messages
    int index = 0;    // 1-based argument index, 0 for the next ordinary one, -1 for '<'
    std::string flags;
    int width = -1;
    int precision = -1;
    char conversion = 0;
};

bool has(const Spec& s, char flag) { return s.flags.find(flag) != std::string::npos; }

std::string formatInteger(const Spec& s, long long value) {
    const char c = s.conversion;
    std::string body;
    bool negative = false;
    if (c == 'd') {
        negative = value < 0;
        const unsigned long long magnitude = negative ? 0ULL - static_cast<unsigned long long>(value)
                                                      : static_cast<unsigned long long>(value);
        body = std::to_string(magnitude);
        if (has(s, ','))
            body = groupThousands(body);
    } else {
        // Java formats int arguments in hexadecimal and octal as the 32-bit two's complement.
        const uint32_t bits = static_cast<uint32_t>(static_cast<int32_t>(value));
        char buffer[16];
        const auto r = std::to_chars(buffer, buffer + sizeof buffer, bits, c == 'o' ? 8 : 16);
        body.assign(buffer, r.ptr);
        if (has(s, '#'))
            body = (c == 'o' ? "0" : "0x") + body;
        if (c == 'X')
            body = upper(body);
    }
    std::string prefix, suffix;
    if (negative) {
        if (has(s, '(')) {
            prefix = "(";
            suffix = ")";
        } else {
            prefix = "-";
        }
    } else if (has(s, '+')) {
        prefix = "+";
    } else if (has(s, ' ')) {
        prefix = " ";
    }
    if (has(s, '0') && s.width > 0) {
        const int used = static_cast<int>(prefix.size() + body.size() + suffix.size());
        if (used < s.width)
            body = std::string(static_cast<size_t>(s.width - used), '0') + body;
    }
    return pad(prefix + body + suffix, s.width, has(s, '-'));
}

void checkFlags(const Spec& s, const std::string& allowed) {
    std::string wrong;
    for (const char f : s.flags)
        if (allowed.find(f) == std::string::npos && f != '<')
            wrong += f;
    if (!wrong.empty())
        formatError("FormatFlagsConversionMismatchException",
                    std::string("Conversion = ") + s.conversion + ", Flags = " + wrong);
}

} // namespace

std::vector<std::string> javaSplit(std::string_view text, char separator, bool keepTrailingEmpty) {
    std::vector<std::string> pieces;
    size_t start = 0;
    while (true) {
        const size_t at = text.find(separator, start);
        if (at == std::string_view::npos) {
            pieces.emplace_back(text.substr(start));
            break;
        }
        pieces.emplace_back(text.substr(start, at - start));
        start = at + 1;
    }
    if (!keepTrailingEmpty && pieces.size() > 1)
        while (!pieces.empty() && pieces.back().empty())
            pieces.pop_back();
    return pieces;
}

std::string javaTrim(std::string_view text) {
    size_t a = 0, b = text.size();
    while (a < b && static_cast<unsigned char>(text[a]) <= ' ')
        ++a;
    while (b > a && static_cast<unsigned char>(text[b - 1]) <= ' ')
        --b;
    return std::string(text.substr(a, b - a));
}

std::string javaFloat(float value) { return javaNumber(value); }
std::string javaDouble(double value) { return javaNumber(value); }

int javaParseInt(std::string_view text) {
    const std::string quoted = "For input string: \"" + std::string(text) + "\"";
    size_t start = 0;
    if (!text.empty() && (text[0] == '-' || text[0] == '+'))
        start = 1;
    if (start == text.size())
        throw Error(Error::Kind::Document, quoted);
    long long value = 0;
    for (size_t i = start; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9')
            throw Error(Error::Kind::Document, quoted);
        value = value * 10 + (text[i] - '0');
        if (value > 2147483648LL)
            throw Error(Error::Kind::Document, quoted);
    }
    if (text[0] == '-')
        value = -value;
    if (value > 2147483647LL)
        throw Error(Error::Kind::Document, quoted);
    return static_cast<int>(value);
}

double javaParseDouble(std::string_view text) {
    // Java trims white space and accepts a leading sign, digits with an optional point, and an
    // exponent. Hexadecimal and the d and f suffixes are not used by the formats ported here.
    size_t a = 0, b = text.size();
    while (a < b && static_cast<unsigned char>(text[a]) <= ' ')
        ++a;
    while (b > a && static_cast<unsigned char>(text[b - 1]) <= ' ')
        --b;
    const std::string trimmed(text.substr(a, b - a));
    const std::string message =
        trimmed.empty() ? "empty String" : "For input string: \"" + std::string(text) + "\"";
    double value = 0;
    const char* first = trimmed.data();
    const char* last = trimmed.data() + trimmed.size();
    if (first != last && *first == '+')
        ++first;
    const auto r = std::from_chars(first, last, value);
    if (trimmed.empty() || r.ec != std::errc() || r.ptr != last)
        throw Error(Error::Kind::Document, message);
    return value;
}

std::string javaFormat(std::string_view mask, std::span<const FormatArg> args) {
    std::string out;
    int ordinary = 0;  // arguments consumed by specifiers without an index
    int previous = -1; // the argument of the previous specifier, for '<'
    size_t i = 0;
    while (i < mask.size()) {
        if (mask[i] != '%') {
            out += mask[i++];
            continue;
        }
        Spec s;
        const size_t start = i++;
        // argument index
        size_t j = i;
        while (j < mask.size() && mask[j] >= '0' && mask[j] <= '9')
            ++j;
        if (j < mask.size() && mask[j] == '$' && j > i) {
            s.index = std::stoi(std::string(mask.substr(i, j - i)));
            i = j + 1;
        }
        while (i < mask.size() && std::string_view("-#+ 0,(<").find(mask[i]) != std::string_view::npos) {
            if (mask[i] == '<')
                s.index = -1;
            s.flags += mask[i++];
        }
        // A width may not start with 0, which is the zero flag.
        j = i;
        while (j < mask.size() && mask[j] >= '0' && mask[j] <= '9')
            ++j;
        if (j > i) {
            s.width = std::stoi(std::string(mask.substr(i, j - i)));
            i = j;
        }
        if (i < mask.size() && mask[i] == '.') {
            j = i + 1;
            while (j < mask.size() && mask[j] >= '0' && mask[j] <= '9')
                ++j;
            if (j == i + 1)
                formatError("UnknownFormatConversionException", "Conversion = '.'");
            s.precision = std::stoi(std::string(mask.substr(i + 1, j - i - 1)));
            i = j;
        }
        if (i >= mask.size())
            formatError("UnknownFormatConversionException", "Conversion = '%'");
        s.conversion = mask[i++];
        s.text = std::string(mask.substr(start, i - start));
        const char c = s.conversion;

        if (c == '%' || c == 'n') {
            if (c == 'n')
                out += "\r\n";
            else
                out += pad("%", s.width, has(s, '-'));
            continue;
        }
        if (std::string_view("doxXsSbBc").find(c) == std::string_view::npos)
            formatError("UnknownFormatConversionException", std::string("Conversion = '") + c + "'");
        if ((has(s, '-') || has(s, '0')) && s.width < 0)
            formatError("MissingFormatWidthException", s.text);
        if (s.precision >= 0 && std::string_view("doxXc").find(c) != std::string_view::npos)
            formatError("IllegalFormatPrecisionException", std::to_string(s.precision));

        int argument;
        if (s.index == -1)
            argument = previous;
        else if (s.index > 0)
            argument = s.index - 1;
        else
            argument = ordinary++;
        if (argument < 0 || argument >= static_cast<int>(args.size()))
            formatError("MissingFormatArgumentException", "Format specifier '" + s.text + "'");
        previous = argument;
        const FormatArg& arg = args[static_cast<size_t>(argument)];
        const bool isInteger = std::holds_alternative<long long>(arg);

        std::string piece;
        if (c == 'd' || c == 'o' || c == 'x' || c == 'X') {
            if (!isInteger)
                formatError("IllegalFormatConversionException", std::string(1, c) + " != java.lang.String");
            checkFlags(s, c == 'd' ? "-+ 0,(" : "-#0");
            piece = formatInteger(s, std::get<long long>(arg));
        } else if (c == 'c') {
            if (!isInteger)
                formatError("IllegalFormatConversionException", "c != java.lang.String");
            checkFlags(s, "-");
            const std::u32string point(1, static_cast<char32_t>(std::get<long long>(arg)));
            const std::string utf8 = fromCodePoints(point);
            piece = pad(utf8, s.width, has(s, '-'));
        } else {
            checkFlags(s, "-");
            if (c == 'b' || c == 'B')
                piece = "true"; // every argument here is non-null
            else
                piece = isInteger ? std::to_string(std::get<long long>(arg)) : std::get<std::string>(arg);
            const std::u32string points = codePoints(piece);
            if (s.precision >= 0 && static_cast<int>(points.size()) > s.precision)
                piece = fromCodePoints(points.substr(0, static_cast<size_t>(s.precision)));
            if (c == 'S' || c == 'B')
                piece = upper(piece);
            piece = pad(piece, s.width, has(s, '-'));
        }
        out += piece;
    }
    return out;
}

std::string javaUtf8Decode(std::string_view bytes) {
    const auto at = [&](size_t i) { return static_cast<unsigned char>(bytes[i]); };
    const auto continuation = [](unsigned char b) { return (b & 0xC0) == 0x80; };
    std::u32string out;
    const size_t n = bytes.size();
    size_t i = 0;
    while (i < n) {
        const unsigned char b1 = at(i);
        if (b1 < 0x80) {
            out.push_back(b1);
            ++i;
        } else if (b1 >= 0xC2 && b1 <= 0xDF) {
            if (i + 1 < n && continuation(at(i + 1))) {
                out.push_back(static_cast<char32_t>(((b1 & 0x1F) << 6) | (at(i + 1) & 0x3F)));
                i += 2;
            } else {
                out.push_back(0xFFFD);
                ++i;
            }
        } else if (b1 >= 0xE0 && b1 <= 0xEF) {
            // isMalformed3_2: an overlong E0 sequence or a second byte that does not continue
            const auto badSecond = [&](unsigned char b2) {
                return (b1 == 0xE0 && (b2 & 0xE0) == 0x80) || !continuation(b2);
            };
            if (i + 2 < n) {
                const unsigned char b2 = at(i + 1), b3 = at(i + 2);
                if (badSecond(b2)) {
                    out.push_back(0xFFFD);
                    ++i;
                } else if (!continuation(b3)) {
                    out.push_back(0xFFFD);
                    i += 2;
                } else {
                    const char32_t c =
                        static_cast<char32_t>(((b1 & 0x0F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F));
                    out.push_back(c >= 0xD800 && c <= 0xDFFF ? char32_t{0xFFFD} : c);
                    i += 3;
                }
            } else if (i + 1 < n && badSecond(at(i + 1))) {
                out.push_back(0xFFFD);
                ++i;
            } else {
                out.push_back(0xFFFD); // a truncated sequence ends the input
                i = n;
            }
        } else if (b1 >= 0xF0 && b1 <= 0xF7) {
            // isMalformed4_2: a lead above F4, an overlong or out-of-range second byte, or none
            const auto badSecond = [&](unsigned char b2) {
                return b1 > 0xF4 || (b1 == 0xF0 && (b2 < 0x90 || b2 > 0xBF)) ||
                       (b1 == 0xF4 && (b2 & 0xF0) != 0x80) || !continuation(b2);
            };
            if (i + 3 < n) {
                const unsigned char b2 = at(i + 1), b3 = at(i + 2), b4 = at(i + 3);
                if (badSecond(b2)) {
                    out.push_back(0xFFFD);
                    ++i;
                } else if (!continuation(b3)) {
                    out.push_back(0xFFFD);
                    i += 2;
                } else if (!continuation(b4)) {
                    out.push_back(0xFFFD);
                    i += 3;
                } else {
                    out.push_back(static_cast<char32_t>(((b1 & 0x07) << 18) | ((b2 & 0x3F) << 12) |
                                                        ((b3 & 0x3F) << 6) | (b4 & 0x3F)));
                    i += 4;
                }
            } else if (b1 > 0xF4 || (i + 1 < n && badSecond(at(i + 1)))) {
                out.push_back(0xFFFD);
                ++i;
            } else if (i + 2 < n && !continuation(at(i + 2))) {
                out.push_back(0xFFFD);
                i += 2;
            } else {
                out.push_back(0xFFFD);
                i = n;
            }
        } else {
            out.push_back(0xFFFD); // a continuation byte, C0, C1, or F8 to FF
            ++i;
        }
    }
    return fromCodePoints(out);
}

int javaStringHash(std::string_view utf8) {
    std::uint32_t h = 0;
    for (const char32_t c : codePoints(utf8)) {
        if (c >= 0x10000) {
            const char32_t v = c - 0x10000;
            h = 31 * h + static_cast<std::uint32_t>(0xD800 + (v >> 10));
            h = 31 * h + static_cast<std::uint32_t>(0xDC00 + (v & 0x3FF));
        } else {
            h = 31 * h + static_cast<std::uint32_t>(c);
        }
    }
    return static_cast<int>(h);
}

std::vector<size_t> javaHashMapOrder(const std::vector<std::string>& keys) {
    size_t capacity = 16;
    while (keys.size() > capacity * 3 / 4)
        capacity *= 2;
    std::vector<size_t> order(keys.size());
    std::vector<size_t> bucket(keys.size());
    for (size_t i = 0; i < keys.size(); ++i) {
        const auto h = static_cast<std::uint32_t>(javaStringHash(keys[i]));
        bucket[i] = (h ^ (h >> 16)) & (capacity - 1);
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return bucket[a] < bucket[b]; });
    return order;
}

} // namespace pdftweak
