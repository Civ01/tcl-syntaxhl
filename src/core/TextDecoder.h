// Byte -> UTF-16 decoding with BOM / UTF-8 detection and ANSI fallback.
#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace shl {

enum class Encoding { Utf8, Utf8Bom, Utf16LE, Utf16BE, Ansi };

const char* encodingName(Encoding e);

// Converts bytes in the "ANSI" (system) code page. The platform layer supplies
// MultiByteToWideChar(CP_ACP); the portable default maps bytes as Latin-1.
using AnsiConverter = std::function<std::u16string(std::string_view)>;

struct DecodedText {
    std::u16string text;  // without BOM
    Encoding encoding = Encoding::Utf8;
};

bool isValidUtf8(std::string_view bytes);

DecodedText decodeText(std::string_view bytes, const AnsiConverter& ansi = {});

// Replace CRLF / lone LF / lone CR with a single '\r' (RichEdit's internal line
// terminator) so that offsets in our text match RichEdit character positions.
std::u16string normalizeLineEndings(std::u16string_view text);

}  // namespace shl
