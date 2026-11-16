// Small portable string helpers.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace shl {

std::string toLowerAscii(std::string_view s);
std::string_view trim(std::string_view s);
std::vector<std::string> splitList(std::string_view s, char sep);  // trimmed, empties dropped
bool iequals(std::string_view a, std::string_view b);

// UTF conversions (invalid sequences become U+FFFD).
std::u16string utf8ToUtf16(std::string_view s);
std::string utf16ToUtf8(std::u16string_view s);

// Path from a UTF-8 string (portable replacement for deprecated u8path).
inline std::filesystem::path utf8Path(std::string_view s) {
    std::u8string u(s.begin(), s.end());
    return std::filesystem::path(u);
}
inline std::string pathToUtf8(const std::filesystem::path& p) {
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// Character class helpers for lexers (ASCII-centric; non-ASCII letters count as
// identifier characters so Unicode identifiers stay in one token).
inline bool isDigit(char16_t c) { return c >= u'0' && c <= u'9'; }
inline bool isHexDigit(char16_t c) {
    return isDigit(c) || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F');
}
inline bool isAlpha(char16_t c) {
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}
inline bool isIdentStart(char16_t c) { return isAlpha(c) || c == u'_' || c >= 0x80; }
inline bool isIdentChar(char16_t c) { return isIdentStart(c) || isDigit(c); }
inline bool isSpace(char16_t c) {
    return c == u' ' || c == u'\t' || c == u'\f' || c == u'\v' || c == 0xA0 || c == 0xFEFF;
}
inline bool isNewline(char16_t c) { return c == u'\n' || c == u'\r'; }

// Compare a UTF-16 range with an ASCII literal.
bool equalsAscii(std::u16string_view s, std::string_view ascii);

}  // namespace shl
