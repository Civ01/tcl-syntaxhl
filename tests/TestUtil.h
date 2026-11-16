#pragma once

#include <doctest.h>

#include <string>
#include <vector>

#include "core/Config.h"
#include "core/LexerFactory.h"
#include "core/TextUtil.h"
#include "core/Token.h"

namespace shl::test {

inline LanguageOptions opts(std::initializer_list<std::pair<const char*, const char*>> kv = {}) {
    LanguageOptions o;
    for (auto& [k, v] : kv) o.values[k] = v;
    return o;
}

struct Lexed {
    std::u16string text;
    std::vector<Token> tokens;

    // Class of the token that exactly covers the `nth` occurrence of `needle`
    // (or "<none>" / "<partial:...>" describing what was found instead).
    std::string at(const char* needle, int nth = 0) const {
        auto n = utf8ToUtf16(needle);
        size_t pos = 0;
        for (int i = 0;; ++i) {
            pos = text.find(n, i == 0 ? 0 : pos + 1);
            if (pos == std::u16string::npos) return "<not found>";
            if (i == nth) break;
        }
        for (const auto& t : tokens) {
            if (t.start <= pos && pos < t.start + t.length) {
                if (t.start + t.length >= pos + n.size()) return tokenClassName(t.cls);
                return std::string("<partial:") + tokenClassName(t.cls) + ">";
            }
        }
        return "default";
    }
};

inline Lexed lexWith(ILexer& lx, const std::string& utf8) {
    Lexed r;
    r.text = utf8ToUtf16(utf8);
    LexState st;
    lx.lex(r.text, st, r.tokens);
    return r;
}

inline Lexed lexLang(const char* id, const std::string& utf8, const LanguageOptions& o = {}) {
    auto lx = createNativeLexer(id, o);
    REQUIRE(lx);
    return lexWith(*lx, utf8);
}

// Lexing line by line with carried state must equal lexing the whole text.
inline void checkResumable(ILexer& lx, const std::string& utf8) {
    auto text = utf8ToUtf16(utf8);
    std::vector<Token> whole;
    {
        LexState st;
        lx.lex(text, st, whole);
    }
    std::vector<Token> pieces;
    LexState st;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find_first_of(u"\r\n", pos);
        size_t next = eol == std::u16string::npos ? text.size() : eol + 1;
        if (eol != std::u16string::npos && text[eol] == u'\r' && next < text.size() && text[next] == u'\n') ++next;
        std::vector<Token> part;
        lx.lex(std::u16string_view(text).substr(pos, next - pos), st, part);
        for (auto t : part) {
            t.start += static_cast<uint32_t>(pos);
            // Emitter merges contiguous same-class tokens; pieces never merge
            // across calls, so normalise by merging here too.
            if (!pieces.empty() && pieces.back().cls == t.cls && pieces.back().start + pieces.back().length == t.start)
                pieces.back().length += t.length;
            else
                pieces.push_back(t);
        }
        pos = next;
    }
    REQUIRE(whole.size() == pieces.size());
    for (size_t i = 0; i < whole.size(); ++i) {
        CAPTURE(i);
        CHECK(whole[i] == pieces[i]);
    }
}

// Tokens must be ordered, non-overlapping and inside the text.
inline void checkWellFormed(const Lexed& l) {
    uint32_t last = 0;
    for (const auto& t : l.tokens) {
        CHECK(t.length > 0);
        CHECK(t.start >= last);
        CHECK(t.start + t.length <= l.text.size());
        last = t.start + t.length;
    }
}

}  // namespace shl::test
