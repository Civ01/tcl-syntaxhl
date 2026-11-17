// INI lexer: [section] (brackets = section_bracket, name = section),
// key = value, full-line ';' / '#' comments.
#include "LineLexer.h"
#include "Lexers.h"

namespace shl {
namespace {

class IniLexer final : public LineLexer {
protected:
    void lexLine(std::u16string_view s, LexState&, Emitter& em) override {
        size_t n = s.size();
        size_t i = skipSpaces(s, 0);
        if (i >= n) return;
        if (s[i] == u';' || s[i] == u'#') {
            em.add(i, n - i, TokenClass::Comment);
            return;
        }
        if (s[i] == u'[') {
            size_t close = s.find(u']', i);
            size_t end = close == std::u16string_view::npos ? n : close + 1;
            em.add(i, 1, TokenClass::SectionBracket);
            size_t nameEnd = close == std::u16string_view::npos ? n : close;
            em.add(i + 1, nameEnd - (i + 1), TokenClass::Section);
            if (close != std::u16string_view::npos) em.add(close, 1, TokenClass::SectionBracket);
            size_t r = skipSpaces(s, end);
            if (r < n && (s[r] == u';' || s[r] == u'#')) em.add(r, n - r, TokenClass::Comment);
            return;
        }
        size_t sep = std::u16string_view::npos;
        for (size_t k = i; k < n; ++k)
            if (s[k] == u'=' || s[k] == u':') { sep = k; break; }
        if (sep == std::u16string_view::npos) {
            em.add(i, n - i, TokenClass::Value);
            return;
        }
        size_t keyEnd = sep;
        while (keyEnd > i && isSpace(s[keyEnd - 1])) --keyEnd;
        em.add(i, keyEnd - i, TokenClass::Key);
        em.add(sep, 1, TokenClass::Operator);
        size_t v = skipSpaces(s, sep + 1);
        size_t vEnd = n;
        while (vEnd > v && isSpace(s[vEnd - 1])) --vEnd;
        if (v >= vEnd) return;
        value(s.substr(v, vEnd - v), v, em);
    }

private:
    static void value(std::u16string_view val, size_t off, Emitter& em) {
        size_t n = val.size();
        if (n >= 2 && (val[0] == u'"' || val[0] == u'\'') && val[n - 1] == val[0]) {
            em.add(off, n, TokenClass::String);
            return;
        }
        // Whole-value number (optionally signed / with fraction)?
        size_t k = (val[0] == u'-' || val[0] == u'+') ? 1 : 0;
        if (k < n && (isDigit(val[k]) || (val[k] == u'.' && k + 1 < n && isDigit(val[k + 1])))) {
            size_t end = scanCNumber(val, k, false);
            if (end == n) {
                em.add(off, n, TokenClass::Number);
                return;
            }
        }
        static const WordSet consts{"true", "false", "yes", "no", "on", "off", "True", "False",
                                    "TRUE", "FALSE", "Yes", "No", "YES", "NO", "On", "Off", "ON", "OFF"};
        if (consts.contains(val)) {
            em.add(off, n, TokenClass::Constant);
            return;
        }
        // Mixed value: highlight quoted parts as strings.
        size_t run = 0;
        for (size_t i = 0; i < n; ++i) {
            if (val[i] == u'"') {
                size_t close = val.find(u'"', i + 1);
                if (close == std::u16string_view::npos) break;
                em.add(off + run, i - run, TokenClass::Value);
                em.add(off + i, close + 1 - i, TokenClass::String);
                i = close;
                run = close + 1;
            }
        }
        em.add(off + run, n - run, TokenClass::Value);
    }
};

}  // namespace

LexerPtr createIniLexer(const LanguageOptions&) { return std::make_unique<IniLexer>(); }

}  // namespace shl
