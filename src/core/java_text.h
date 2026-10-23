#pragma once

#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pdftweak {

// Float.toString and Double.toString of Java 19 and later: the shortest decimal that reads back as
// the same value, in plain notation from 10^-3 up to below 10^7 and in computerized scientific
// notation (1.0E-5) otherwise, always with at least one digit after the point.
std::string javaFloat(float value);
std::string javaDouble(double value);

// Integer.parseInt and Double.parseDouble. Both throw Error(Document) with Java's
// NumberFormatException message, such as: For input string: "x"
int javaParseInt(std::string_view text);
double javaParseDouble(std::string_view text);

// String.split on one literal character. Java drops trailing empty pieces unless the limit is
// negative (keepTrailingEmpty), and splitting an empty string gives one empty piece.
std::vector<std::string> javaSplit(std::string_view text, char separator, bool keepTrailingEmpty);

// String.trim, which removes the characters up to and including the space at both ends.
std::string javaTrim(std::string_view text);

// String.format for the arguments the page number mask receives (integers and strings), with the
// conversions d, o, x, X, s, S, b, B, c, %, and n, argument indices, the < flag, and the flags
// - # + space 0 , and (. An invalid mask throws Error(Io) whose message is the toString of the
// IllegalFormatException Java would throw, as PdfTweak reported it.
using FormatArg = std::variant<long long, std::string>;
std::string javaFormat(std::string_view mask, std::span<const FormatArg> args);

// new String(bytes, UTF_8): bytes decoded as Java does, with one U+FFFD for each malformed run as
// the JDK's decoder measures it (one byte for a bad lead or a bad second byte, two or three for a
// partly valid sequence, and the whole tail for a truncated but valid start). Returns UTF-8.
std::string javaUtf8Decode(std::string_view bytes);

// String.hashCode over the UTF-16 code units of UTF-8 text.
int javaStringHash(std::string_view utf8);

// The iteration order of a java.util.HashMap<String, ?> whose keys were put in the given order (no
// duplicates): buckets in index order for the final capacity (16, doubling whenever the count
// passes three quarters of it), insertion order within a bucket. Resizing keeps the relative order
// within a bucket, so only the final capacity matters. A bin is turned into a tree only after
// eight collisions at capacity 64 or more, which the maps of document information and bookmark
// options cannot reach, so trees are not modelled. Returns indices into keys.
std::vector<size_t> javaHashMapOrder(const std::vector<std::string>& keys);

} // namespace pdftweak
