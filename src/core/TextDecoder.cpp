#include "TextDecoder.h"

#include "TextUtil.h"

namespace shl {

const char* encodingName(Encoding e) {
    switch (e) {
        case Encoding::Utf8: return "UTF-8";
        case Encoding::Utf8Bom: return "UTF-8 (BOM)";
        case Encoding::Utf16LE: return "UTF-16 LE";
        case Encoding::Utf16BE: return "UTF-16 BE";
        case Encoding::Ansi: return "ANSI";
    }
    return "?";
}

bool isValidUtf8(std::string_view s) {
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { ++i; continue; }
        size_t len;
        uint32_t cp;
        if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
        else return false;
        if (i + len > n) return false;
        for (size_t k = 1; k < len; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) ||
            cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += len;
    }
    return true;
}

static std::u16string decodeUtf16(std::string_view b, bool bigEndian) {
    std::u16string out;
    out.resize(b.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        auto lo = static_cast<unsigned char>(b[2 * i + (bigEndian ? 1 : 0)]);
        auto hi = static_cast<unsigned char>(b[2 * i + (bigEndian ? 0 : 1)]);
        out[i] = static_cast<char16_t>(lo | (hi << 8));
    }
    return out;
}

DecodedText decodeText(std::string_view bytes, const AnsiConverter& ansi) {
    DecodedText r;
    auto has = [&](std::initializer_list<unsigned char> bom) {
        if (bytes.size() < bom.size()) return false;
        size_t i = 0;
        for (auto c : bom)
            if (static_cast<unsigned char>(bytes[i++]) != c) return false;
        return true;
    };
    if (has({0xEF, 0xBB, 0xBF})) {
        r.encoding = Encoding::Utf8Bom;
        r.text = utf8ToUtf16(bytes.substr(3));
    } else if (has({0xFF, 0xFE})) {
        r.encoding = Encoding::Utf16LE;
        r.text = decodeUtf16(bytes.substr(2), false);
    } else if (has({0xFE, 0xFF})) {
        r.encoding = Encoding::Utf16BE;
        r.text = decodeUtf16(bytes.substr(2), true);
    } else if (isValidUtf8(bytes)) {
        r.encoding = Encoding::Utf8;
        r.text = utf8ToUtf16(bytes);
    } else {
        r.encoding = Encoding::Ansi;
        if (ansi) {
            r.text = ansi(bytes);
        } else {
            r.text.reserve(bytes.size());
            for (char c : bytes) r.text.push_back(static_cast<unsigned char>(c));
        }
    }
    return r;
}

std::u16string normalizeLineEndings(std::u16string_view t) {
    std::u16string out;
    out.reserve(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        char16_t c = t[i];
        if (c == u'\r') {
            out.push_back(u'\r');
            if (i + 1 < t.size() && t[i + 1] == u'\n') ++i;
        } else if (c == u'\n') {
            out.push_back(u'\r');
        } else if (c == 0) {
            out.push_back(u' ');  // NULs would truncate RichEdit text
        } else {
            out.push_back(c);
        }
    }
    return out;
}

}  // namespace shl
