// Base class for line-oriented lexers plus small scanning helpers.
//
// LineLexer splits input into lines and calls lexLine() for each. Because a
// lexLine() implementation may only look inside its own line, and any
// information that must cross a line boundary lives in LexState, every
// LineLexer is automatically resumable at line starts (a requirement for lazy
// highlighting).
#pragma once

#include <initializer_list>
#include <string_view>
#include <unordered_set>

#include "core/TextUtil.h"
#include "core/Token.h"

namespace shl {

class Emitter {
public:
    Emitter(std::vector<Token>& out, uint32_t base) : out_(out), base_(base) {}
    void add(size_t pos, size_t len, TokenClass c) {
        if (len == 0 || c == TokenClass::Default) return;
        auto start = base_ + static_cast<uint32_t>(pos);
        // Merge with the previous token when contiguous and same class.
        if (!out_.empty() && out_.back().cls == c && out_.back().start + out_.back().length == start) {
            out_.back().length += static_cast<uint32_t>(len);
            return;
        }
        out_.push_back(Token{start, static_cast<uint32_t>(len), c});
    }

private:
    std::vector<Token>& out_;
    uint32_t base_;
};

class LineLexer : public ILexer {
public:
    void lex(std::u16string_view text, LexState& state, std::vector<Token>& out) override {
        size_t pos = 0, n = text.size();
        while (pos < n) {
            size_t eol = pos;
            while (eol < n && !isNewline(text[eol])) ++eol;
            Emitter em(out, static_cast<uint32_t>(pos));
            lexLine(text.substr(pos, eol - pos), state, em);
            endOfLine(state);
            if (eol < n) {
                if (text[eol] == u'\r' && eol + 1 < n && text[eol + 1] == u'\n') eol += 2;
                else eol += 1;
            }
            pos = eol;
        }
    }

protected:
    // `line` excludes the line terminator. Token positions are relative to line.
    virtual void lexLine(std::u16string_view line, LexState& state, Emitter& em) = 0;
    // Called after every line that ended (or the final line); default no-op.
    virtual void endOfLine(LexState&) {}
};

// Set of ASCII words, looked up with UTF-16 views.
class WordSet {
public:
    WordSet(std::initializer_list<const char*> words) {
        for (auto w : words) {
            std::u16string s;
            for (const char* p = w; *p; ++p) s.push_back(static_cast<char16_t>(*p));
            set_.insert(std::move(s));
        }
    }
    bool contains(std::u16string_view w) const { return set_.count(std::u16string(w)) != 0; }

private:
    std::unordered_set<std::u16string> set_;
};

inline size_t skipSpaces(std::u16string_view s, size_t i) {
    while (i < s.size() && isSpace(s[i])) ++i;
    return i;
}

inline size_t scanIdent(std::u16string_view s, size_t i) {
    while (i < s.size() && isIdentChar(s[i])) ++i;
    return i;
}

// C-family number literal starting at i (digit or '.' followed by digit).
// Handles 0x/0o/0b prefixes, '_' separators (if allowUnderscore), fraction,
// exponent. Returns end position; caller handles suffixes.
inline size_t scanCNumber(std::u16string_view s, size_t i, bool allowUnderscore) {
    size_t n = s.size();
    auto digitsWhile = [&](size_t k, auto pred) {
        while (k < n && (pred(s[k]) || (allowUnderscore && s[k] == u'_'))) ++k;
        return k;
    };
    if (s[i] == u'0' && i + 1 < n) {
        char16_t p = s[i + 1] | 0x20;
        if (p == u'x') return digitsWhile(i + 2, [](char16_t c) { return isHexDigit(c); });
        if (p == u'o') return digitsWhile(i + 2, [](char16_t c) { return c >= u'0' && c <= u'7'; });
        if (p == u'b') return digitsWhile(i + 2, [](char16_t c) { return c == u'0' || c == u'1'; });
    }
    size_t k = digitsWhile(i, [](char16_t c) { return isDigit(c); });
    if (k < n && s[k] == u'.' && (k + 1 >= n || s[k + 1] != u'.')) {
        k = digitsWhile(k + 1, [](char16_t c) { return isDigit(c); });
    }
    if (k < n && (s[k] == u'e' || s[k] == u'E')) {
        size_t e = k + 1;
        if (e < n && (s[e] == u'+' || s[e] == u'-')) ++e;
        if (e < n && isDigit(s[e])) k = digitsWhile(e, [](char16_t c) { return isDigit(c); });
    }
    return k;
}

inline bool startsWith(std::u16string_view s, size_t i, std::u16string_view what) {
    return s.size() >= i + what.size() && s.substr(i, what.size()) == what;
}

}  // namespace shl
