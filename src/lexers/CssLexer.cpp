// CSS lexer with optional SCSS/LESS support.
//
// State:
//   mode bit0     : inside /* block comment */
//   mode bits 1-3 : context (Selector, DeclStart, Property, Value, Prelude)
//   mode bit4     : the current at-rule prelude opens a declaration block
//   depth         : brace nesting depth
//   extra bit i   : block at level i holds declarations (vs. nested rules)
#include "LineLexer.h"
#include "Lexers.h"

namespace shl {
namespace {

constexpr uint32_t kComment = 1;
constexpr uint32_t kCtxShift = 1, kCtxMask = 7u << kCtxShift;
enum Ctx : uint32_t { Selector = 0, DeclStart = 1, Property = 2, Value = 3, Prelude = 4 };
constexpr uint32_t kPreludeDecl = 16;

const WordSet& declAtRules() {
    static const WordSet w{"font-face", "page", "property", "counter-style", "viewport",
                           "font-feature-values", "mixin", "include", "function", "if", "else",
                           "each", "for", "while", "at-root", "return", "content"};
    return w;
}
const WordSet& preludeKeywords() {
    static const WordSet w{"and", "or", "not", "only", "from", "through", "to", "in", "as", "with"};
    return w;
}

inline bool isCssIdentStart(std::u16string_view s, size_t i) {
    if (i >= s.size()) return false;
    char16_t c = s[i];
    if (isIdentStart(c)) return true;
    if (c == u'-' && i + 1 < s.size()) {
        char16_t d = s[i + 1];
        return isIdentStart(d) || d == u'-';
    }
    return false;
}
inline size_t scanCssIdent(std::u16string_view s, size_t i) {
    while (i < s.size() && (isIdentChar(s[i]) || s[i] == u'-')) ++i;
    return i;
}

class CssLexer final : public LineLexer {
public:
    explicit CssLexer(bool scss) : scss_(scss) {}

protected:
    void lexLine(std::u16string_view s, LexState& st, Emitter& em) override {
        size_t n = s.size(), i = 0;
        while (i < n) {
            if (st.mode & kComment) {
                size_t end = s.find(u"*/", i);
                if (end == std::u16string_view::npos) { em.add(i, n - i, TokenClass::Comment); return; }
                em.add(i, end + 2 - i, TokenClass::Comment);
                i = end + 2;
                st.mode &= ~kComment;
                continue;
            }
            char16_t c = s[i];
            if (isSpace(c)) { ++i; continue; }
            if (c == u'/' && i + 1 < n && s[i + 1] == u'*') {
                st.mode |= kComment;
                em.add(i, 2, TokenClass::Comment);
                i += 2;
                continue;
            }
            if (scss_ && c == u'/' && i + 1 < n && s[i + 1] == u'/') { em.add(i, n - i, TokenClass::Comment); return; }
            if (c == u'"' || c == u'\'') { i = string(s, i, em); continue; }
            if (scss_ && c == u'#' && i + 1 < n && s[i + 1] == u'{') { i = interpolation(s, i, em); continue; }
            if (c == u'{') { openBlock(st); em.add(i++, 1, TokenClass::Punctuation); continue; }
            if (c == u'}') { closeBlock(st); em.add(i++, 1, TokenClass::Punctuation); continue; }
            if (c == u';') { setCtx(st, inDecl(st) ? DeclStart : Selector); em.add(i++, 1, TokenClass::Punctuation); continue; }

            uint32_t ctx = getCtx(st);
            if (ctx == DeclStart) {
                if (c == u'@') { i = atRule(s, i, st, em); continue; }
                if (scss_ && c == u'$') { i = scssVarStatement(s, i, st, em); continue; }
                setCtx(st, statementIsSelector(s, i) ? Selector : Property);
                ctx = getCtx(st);
            }
            switch (ctx) {
                case Selector: i = selector(s, i, st, em); break;
                case Property: i = property(s, i, st, em); break;
                case Value:
                case Prelude: i = value(s, i, st, em, ctx == Prelude); break;
                default: ++i; break;
            }
        }
    }

private:
    static uint32_t getCtx(const LexState& st) { return (st.mode & kCtxMask) >> kCtxShift; }
    static void setCtx(LexState& st, uint32_t c) { st.mode = (st.mode & ~kCtxMask) | (c << kCtxShift); }
    static bool inDecl(const LexState& st) {
        if (st.depth == 0) return false;
        if (st.depth > 64) return true;
        return (st.extra >> (st.depth - 1)) & 1;
    }
    static void openBlock(LexState& st) {
        uint32_t ctx = getCtx(st);
        bool decl = ctx == Prelude ? (st.mode & kPreludeDecl) != 0 : true;
        if (st.depth < 64) {
            uint64_t bit = uint64_t(1) << st.depth;
            st.extra = decl ? (st.extra | bit) : (st.extra & ~bit);
        }
        ++st.depth;
        st.mode &= ~kPreludeDecl;
        setCtx(st, decl ? DeclStart : Selector);
    }
    static void closeBlock(LexState& st) {
        if (st.depth > 0) --st.depth;
        setCtx(st, inDecl(st) ? DeclStart : Selector);
    }

    // Decide whether a statement inside a declaration block is a nested rule
    // (selector ... '{') or a declaration (property: value;). Line-local.
    bool statementIsSelector(std::u16string_view s, size_t i) const {
        size_t n = s.size();
        int paren = 0;
        for (size_t k = i; k < n; ++k) {
            char16_t c = s[k];
            if (c == u'"' || c == u'\'') {
                size_t close = s.find(c, k + 1);
                if (close == std::u16string_view::npos) return false;
                k = close;
                continue;
            }
            if (c == u'#' && k + 1 < n && s[k + 1] == u'{') {
                size_t close = s.find(u'}', k + 2);
                if (close == std::u16string_view::npos) return false;
                k = close;
                continue;
            }
            if (c == u'(') ++paren;
            else if (c == u')') --paren;
            else if (paren <= 0) {
                if (c == u'{') return true;
                if (c == u';' || c == u'}') return false;
                if (c == u'/' && k + 1 < n && (s[k + 1] == u'*' || (scss_ && s[k + 1] == u'/'))) break;
            }
        }
        // No terminator on this line: a trailing ',' means a selector list.
        size_t e = n;
        while (e > i && isSpace(s[e - 1])) --e;
        return e > i && s[e - 1] == u',';
    }

    static size_t string(std::u16string_view s, size_t i, Emitter& em) {
        size_t n = s.size(), k = i + 1;
        char16_t q = s[i];
        while (k < n && s[k] != q) k += (s[k] == u'\\' && k + 1 < n) ? 2 : 1;
        size_t end = k < n ? k + 1 : n;
        em.add(i, end - i, TokenClass::String);
        return end;
    }

    static size_t interpolation(std::u16string_view s, size_t i, Emitter& em) {
        size_t close = s.find(u'}', i + 2);
        size_t end = close == std::u16string_view::npos ? i + 2 : close + 1;
        em.add(i, end - i, TokenClass::Variable);
        return end;
    }

    size_t atRule(std::u16string_view s, size_t i, LexState& st, Emitter& em) {
        size_t end = scanCssIdent(s, i + 1);
        if (end == i + 1) { em.add(i, 1, TokenClass::Operator); return i + 1; }
        auto name = s.substr(i + 1, end - i - 1);
        size_t k = skipSpaces(s, end);
        if (scss_ && k < s.size() && s[k] == u':') {
            // LESS variable declaration: @name: value;
            em.add(i, end - i, TokenClass::Variable);
            em.add(k, 1, TokenClass::Punctuation);
            setCtx(st, Value);
            return k + 1;
        }
        em.add(i, end - i, TokenClass::AtRule);
        bool decl = declAtRules().contains(name) || inDecl(st);
        st.mode = decl ? (st.mode | kPreludeDecl) : (st.mode & ~kPreludeDecl);
        setCtx(st, Prelude);
        return end;
    }

    // `$var: value;` (SCSS) at statement level.
    size_t scssVarStatement(std::u16string_view s, size_t i, LexState& st, Emitter& em) {
        size_t end = scanCssIdent(s, i + 1);
        em.add(i, end - i, TokenClass::Variable);
        size_t k = skipSpaces(s, end);
        if (k < s.size() && s[k] == u':') {
            em.add(k, 1, TokenClass::Punctuation);
            setCtx(st, Value);
            return k + 1;
        }
        return end;
    }

    size_t selector(std::u16string_view s, size_t i, LexState& st, Emitter& em) {
        size_t n = s.size();
        char16_t c = s[i];
        if (c == u'@') return atRule(s, i, st, em);
        if (scss_ && c == u'$') return scssVarStatement(s, i, st, em);
        if ((c == u'.' || c == u'#' || (scss_ && c == u'%')) && isCssIdentStart(s, i + 1)) {
            size_t end = scanCssIdent(s, i + 1);
            em.add(i, end - i, TokenClass::Selector);
            return end;
        }
        if (isCssIdentStart(s, i)) {
            size_t end = scanCssIdent(s, i);
            em.add(i, end - i, TokenClass::Selector);
            return end;
        }
        if (c == u'&' || c == u'*') { em.add(i, 1, TokenClass::Selector); return i + 1; }
        if (c == u':') {
            size_t k = i + 1;
            if (k < n && s[k] == u':') ++k;
            if (isCssIdentStart(s, k)) {
                size_t end = scanCssIdent(s, k);
                em.add(i, end - i, TokenClass::Keyword);
                return end;
            }
            em.add(i, k - i, TokenClass::Punctuation);
            return k;
        }
        if (c == u'[') {
            em.add(i, 1, TokenClass::Punctuation);
            size_t k = i + 1;
            while (k < n && s[k] != u']') {
                if (isSpace(s[k])) { ++k; continue; }
                if (s[k] == u'"' || s[k] == u'\'') { k = string(s, k, em); continue; }
                if (isCssIdentStart(s, k)) {
                    size_t e = scanCssIdent(s, k);
                    em.add(k, e - k, TokenClass::Key);
                    k = e;
                    continue;
                }
                em.add(k, 1, TokenClass::Operator);
                ++k;
            }
            if (k < n) em.add(k++, 1, TokenClass::Punctuation);
            return k;
        }
        if (isDigit(c) || (c == u'.' && i + 1 < n && isDigit(s[i + 1]))) return number(s, i, em);
        if (c == u'(' || c == u')') { em.add(i, 1, TokenClass::Punctuation); return i + 1; }
        em.add(i, 1, TokenClass::Operator);  // , > + ~ etc.
        return i + 1;
    }

    size_t property(std::u16string_view s, size_t i, LexState& st, Emitter& em) {
        char16_t c = s[i];
        if (c == u':') {
            em.add(i, 1, TokenClass::Punctuation);
            setCtx(st, Value);
            return i + 1;
        }
        if (scss_ && c == u'$') {
            size_t end = scanCssIdent(s, i + 1);
            em.add(i, end - i, TokenClass::Variable);
            return end;
        }
        if ((c == u'.' || c == u'#') && isCssIdentStart(s, i + 1)) {  // LESS mixin call
            size_t end = scanCssIdent(s, i + 1);
            em.add(i, end - i, TokenClass::Selector);
            return end;
        }
        if (c == u'*' || c == u'_') {  // old IE hacks
            if (isCssIdentStart(s, i + 1)) {
                size_t end = scanCssIdent(s, i + 1);
                em.add(i, end - i, TokenClass::Property);
                return end;
            }
        }
        if (isCssIdentStart(s, i)) {
            size_t end = scanCssIdent(s, i);
            em.add(i, end - i, TokenClass::Property);
            return end;
        }
        if (c == u'!') return important(s, i, em);
        em.add(i, 1, (c == u'(' || c == u')' || c == u',') ? TokenClass::Punctuation : TokenClass::Operator);
        return i + 1;
    }

    static size_t important(std::u16string_view s, size_t i, Emitter& em) {
        size_t k = skipSpaces(s, i + 1);
        if (isCssIdentStart(s, k)) {
            size_t end = scanCssIdent(s, k);
            em.add(i, end - i, TokenClass::Keyword);
            return end;
        }
        em.add(i, 1, TokenClass::Operator);
        return i + 1;
    }

    static size_t number(std::u16string_view s, size_t i, Emitter& em) {
        size_t k = i;
        if (s[k] == u'+' || s[k] == u'-') ++k;
        size_t end = scanCNumber(s, k, false);
        if (end < s.size() && s[end] == u'%') ++end;
        else if (isCssIdentStart(s, end) && s[end] != u'-') end = scanCssIdent(s, end);
        em.add(i, end - i, TokenClass::Number);
        return end;
    }

    size_t value(std::u16string_view s, size_t i, LexState& st, Emitter& em, bool prelude) {
        size_t n = s.size();
        char16_t c = s[i];
        bool prevIdent = i > 0 && (isIdentChar(s[i - 1]) || s[i - 1] == u'-');
        if ((c == u'-' || c == u'+') && !prevIdent && i + 1 < n &&
            (isDigit(s[i + 1]) || (s[i + 1] == u'.' && i + 2 < n && isDigit(s[i + 2]))))
            return number(s, i, em);
        if (isDigit(c) || (c == u'.' && i + 1 < n && isDigit(s[i + 1]))) return number(s, i, em);
        if (c == u'#') {
            size_t end = i + 1;
            while (end < n && isIdentChar(s[end])) ++end;
            em.add(i, end - i, end > i + 1 ? TokenClass::Number : TokenClass::Operator);
            return end;
        }
        if (scss_ && (c == u'$' || c == u'@') && isCssIdentStart(s, i + 1)) {
            size_t end = scanCssIdent(s, i + 1);
            em.add(i, end - i, TokenClass::Variable);
            return end;
        }
        if (c == u'!') return important(s, i, em);
        if (isCssIdentStart(s, i)) {
            size_t end = scanCssIdent(s, i);
            auto w = s.substr(i, end - i);
            size_t k = skipSpaces(s, end);
            if (end < n && s[end] == u'(') {
                em.add(i, end - i, TokenClass::Function);
                if (equalsAscii(w, "url") || equalsAscii(w, "url-prefix")) {
                    em.add(end, 1, TokenClass::Punctuation);
                    size_t a = skipSpaces(s, end + 1);
                    if (a < n && s[a] != u'"' && s[a] != u'\'') {
                        size_t close = s.find(u')', a);
                        size_t stop = close == std::u16string_view::npos ? n : close;
                        em.add(a, stop - a, TokenClass::String);
                        if (close != std::u16string_view::npos) em.add(close, 1, TokenClass::Punctuation);
                        return close == std::u16string_view::npos ? n : close + 1;
                    }
                    return end + 1;
                }
                return end;
            }
            if (prelude && k < n && s[k] == u':') em.add(i, end - i, TokenClass::Property);
            else if (prelude && preludeKeywords().contains(w)) em.add(i, end - i, TokenClass::Keyword);
            else em.add(i, end - i, TokenClass::Value);
            return end;
        }
        if (c == u'(' || c == u')' || c == u':' || c == u'[' || c == u']')
            em.add(i, 1, TokenClass::Punctuation);
        else
            em.add(i, 1, TokenClass::Operator);
        (void)st;
        return i + 1;
    }

    bool scss_;
};

}  // namespace

LexerPtr createCssLexer(const LanguageOptions& opts) {
    return std::make_unique<CssLexer>(opts.getBool("support_scss_less", false));
}

}  // namespace shl
