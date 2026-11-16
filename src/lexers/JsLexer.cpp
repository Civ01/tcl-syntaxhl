// JavaScript (ES2023) lexer.
//
// State:
//   mode bits 0-2: 0 = code, 1 = block comment, 2 = '...' continued (trailing
//                  backslash), 3 = "..." continued, 4 = inside template literal
//   mode bit 4   : a regex literal may start here (previous token allows it)
//   mode bit 5   : first line already processed (for #! hashbang)
//   extra/depth  : stack of 2-bit entries for template nesting:
//                  1 = template, 2 = ${ expression, 3 = plain brace in expr
#include "LineLexer.h"
#include "Lexers.h"

namespace shl {
namespace {

constexpr uint32_t kKindMask = 7, kCode = 0, kBlockComment = 1, kSingleCont = 2, kDoubleCont = 3,
                   kTemplate = 4;
constexpr uint32_t kRegexOk = 16, kSeenFirstLine = 32;
constexpr uint64_t kStTemplate = 1, kStExpr = 2, kStBrace = 3;
constexpr uint32_t kMaxStack = 32;

const WordSet& keywords() {
    static const WordSet w{"async",    "await",   "break",   "case",   "catch",  "class",
                           "const",    "continue", "debugger", "default", "delete", "do",
                           "else",     "export",  "extends", "finally", "for",   "function",
                           "if",       "import",  "in",      "instanceof", "let", "new",
                           "of",       "return",  "static",  "super",  "switch", "this",
                           "throw",    "try",     "typeof",  "var",    "void",   "while",
                           "with",     "yield",   "get",     "set",    "from",   "as"};
    return w;
}
const WordSet& regexAfterKeywords() {
    static const WordSet w{"return", "typeof", "instanceof", "in", "of", "new", "delete", "void",
                           "throw", "case", "do", "else", "yield", "await"};
    return w;
}
const WordSet& constants() {
    static const WordSet w{"true", "false", "null", "undefined", "NaN", "Infinity"};
    return w;
}
const WordSet& builtins() {
    static const WordSet w{"console", "window", "document", "globalThis", "Object", "Array",
                           "String", "Number", "Boolean", "Symbol", "BigInt", "Math", "JSON",
                           "Date", "RegExp", "Map", "Set", "WeakMap", "WeakSet", "Promise",
                           "Proxy", "Reflect", "Error", "TypeError", "RangeError", "SyntaxError",
                           "ReferenceError", "Intl", "ArrayBuffer", "DataView", "Uint8Array",
                           "Int32Array", "Float64Array", "require", "module", "exports",
                           "process", "arguments", "setTimeout", "setInterval", "clearTimeout",
                           "clearInterval", "parseInt", "parseFloat", "isNaN", "isFinite"};
    return w;
}

class JsLexer final : public LineLexer {
protected:
    void lexLine(std::u16string_view s, LexState& st, Emitter& em) override {
        size_t n = s.size(), i = 0;
        if (!(st.mode & kSeenFirstLine)) {
            st.mode |= kSeenFirstLine | kRegexOk;
            if (startsWith(s, 0, u"#!")) { em.add(0, n, TokenClass::Comment); return; }
        }
        int expectName = 0;  // 1 = function name, 2 = class name
        while (i < n) {
            uint32_t kind = st.mode & kKindMask;
            if (kind == kBlockComment) {
                size_t end = s.find(u"*/", i);
                if (end == std::u16string_view::npos) { em.add(i, n - i, TokenClass::Comment); return; }
                em.add(i, end + 2 - i, TokenClass::Comment);
                i = end + 2;
                setKind(st, kCode);
                continue;
            }
            if (kind == kSingleCont || kind == kDoubleCont) {
                i = stringBody(s, i, kind == kDoubleCont ? u'"' : u'\'', st, em);
                continue;
            }
            if (kind == kTemplate) { i = templateBody(s, i, st, em); continue; }

            char16_t c = s[i];
            if (isSpace(c)) { ++i; continue; }
            if (c == u'/' && i + 1 < n && s[i + 1] == u'/') { em.add(i, n - i, TokenClass::Comment); return; }
            if (c == u'/' && i + 1 < n && s[i + 1] == u'*') {
                em.add(i, 2, TokenClass::Comment);
                i += 2;
                setKind(st, kBlockComment);
                continue;
            }
            if (c == u'\'' || c == u'"') {
                em.add(i, 1, TokenClass::String);
                i = stringBody(s, i + 1, c, st, em);
                setRegexOk(st, false);
                expectName = 0;
                continue;
            }
            if (c == u'`') {
                em.add(i, 1, TokenClass::Template);
                push(st, kStTemplate);
                setKind(st, kTemplate);
                ++i;
                expectName = 0;
                continue;
            }
            if (c == u'/' && (st.mode & kRegexOk)) {
                size_t end = regexEnd(s, i);
                if (end != 0) {
                    em.add(i, end - i, TokenClass::Regex);
                    i = end;
                    setRegexOk(st, false);
                    continue;
                }
            }
            if (isDigit(c) || (c == u'.' && i + 1 < n && isDigit(s[i + 1]))) {
                size_t end = scanCNumber(s, i, true);
                if (end < n && s[end] == u'n') ++end;
                em.add(i, end - i, TokenClass::Number);
                i = end;
                setRegexOk(st, false);
                expectName = 0;
                continue;
            }
            if (isIdentStart(c) || c == u'$' || c == u'#') {
                size_t end = i + 1;
                while (end < n && (isIdentChar(s[end]) || s[end] == u'$')) ++end;
                auto w = s.substr(i, end - i);
                bool member = i > 0 && s[i - 1] == u'.';
                TokenClass cls = TokenClass::Identifier;
                if (expectName == 1) cls = TokenClass::Function;
                else if (expectName == 2) cls = TokenClass::Type;
                else if (!member && keywords().contains(w)) cls = TokenClass::Keyword;
                else if (!member && constants().contains(w)) cls = TokenClass::Constant;
                else if (!member && builtins().contains(w)) cls = TokenClass::Builtin;
                else {
                    size_t k = skipSpaces(s, end);
                    if (k < n && s[k] == u'(') cls = TokenClass::Function;
                }
                expectName = 0;
                if (cls == TokenClass::Keyword) {
                    if (equalsAscii(w, "function")) expectName = 1;
                    else if (equalsAscii(w, "class")) expectName = 2;
                }
                setRegexOk(st, cls == TokenClass::Keyword && regexAfterKeywords().contains(w));
                em.add(i, end - i, cls);
                i = end;
                continue;
            }
            // Punctuation / operators
            expectName = (c == u'*' && expectName == 1) ? 1 : 0;  // function* name
            if (c == u'{') {
                if (st.depth > 0) push(st, kStBrace);
                em.add(i++, 1, TokenClass::Punctuation);
                setRegexOk(st, true);
                continue;
            }
            if (c == u'}') {
                uint64_t t = top(st);
                if (t == kStExpr) {
                    pop(st);
                    em.add(i++, 1, TokenClass::Template);
                    setKind(st, kTemplate);
                    continue;
                }
                if (t == kStBrace) pop(st);
                em.add(i++, 1, TokenClass::Punctuation);
                setRegexOk(st, true);
                continue;
            }
            static const std::u16string_view punct = u"()[],;.";
            bool isPunct = punct.find(c) != std::u16string_view::npos;
            em.add(i, 1, isPunct ? TokenClass::Punctuation : TokenClass::Operator);
            setRegexOk(st, !(c == u')' || c == u']'));
            ++i;
        }
    }

private:
    static void setKind(LexState& st, uint32_t k) { st.mode = (st.mode & ~kKindMask) | k; }
    static void setRegexOk(LexState& st, bool v) { st.mode = v ? (st.mode | kRegexOk) : (st.mode & ~kRegexOk); }
    static uint64_t top(const LexState& st) {
        if (st.depth == 0) return 0;
        if (st.depth > kMaxStack) return kStBrace;
        return (st.extra >> (2 * (st.depth - 1))) & 3;
    }
    static void push(LexState& st, uint64_t v) {
        if (st.depth < kMaxStack) {
            uint64_t shift = 2 * st.depth;
            st.extra = (st.extra & ~(uint64_t(3) << shift)) | (v << shift);
        }
        ++st.depth;
    }
    static void pop(LexState& st) {
        if (st.depth > 0) --st.depth;
    }

    // Body of a quoted string from i; returns position after the closing quote,
    // or end of line (string continued if the line ends with a backslash).
    size_t stringBody(std::u16string_view s, size_t i, char16_t q, LexState& st, Emitter& em) {
        size_t n = s.size(), run = i;
        while (i < n) {
            if (s[i] == u'\\') {
                if (i + 1 >= n) {
                    em.add(run, n - run, TokenClass::String);
                    setKind(st, q == u'"' ? kDoubleCont : kSingleCont);
                    return n;
                }
                em.add(run, i - run, TokenClass::String);
                size_t len = escapeLength(s, i);
                em.add(i, len, TokenClass::StringEscape);
                i += len;
                run = i;
                continue;
            }
            if (s[i] == q) {
                em.add(run, i + 1 - run, TokenClass::String);
                setKind(st, kCode);
                setRegexOk(st, false);
                return i + 1;
            }
            ++i;
        }
        em.add(run, n - run, TokenClass::String);  // unterminated: ends at EOL
        setKind(st, kCode);
        return n;
    }

    size_t templateBody(std::u16string_view s, size_t i, LexState& st, Emitter& em) {
        size_t n = s.size(), run = i;
        while (i < n) {
            char16_t c = s[i];
            if (c == u'\\' && i + 1 < n) {
                em.add(run, i - run, TokenClass::Template);
                size_t len = escapeLength(s, i);
                em.add(i, len, TokenClass::StringEscape);
                i += len;
                run = i;
                continue;
            }
            if (c == u'`') {
                em.add(run, i + 1 - run, TokenClass::Template);
                pop(st);
                setKind(st, kCode);
                setRegexOk(st, false);
                return i + 1;
            }
            if (c == u'$' && i + 1 < n && s[i + 1] == u'{') {
                em.add(run, i + 2 - run, TokenClass::Template);
                push(st, kStExpr);
                setKind(st, kCode);
                setRegexOk(st, true);
                return i + 2;
            }
            ++i;
        }
        em.add(run, n - run, TokenClass::Template);
        return n;
    }

    static size_t escapeLength(std::u16string_view s, size_t i) {
        size_t n = s.size();
        char16_t c = s[i + 1];
        auto hex = [&](size_t start, size_t count) {
            size_t k = start;
            while (k < n && k < start + count && isHexDigit(s[k])) ++k;
            return k - i;
        };
        if (c == u'x') return hex(i + 2, 2);
        if (c == u'u') {
            if (i + 2 < n && s[i + 2] == u'{') {
                size_t close = s.find(u'}', i + 3);
                return close == std::u16string_view::npos ? 2 : close + 1 - i;
            }
            return hex(i + 2, 4);
        }
        return 2;
    }

    // Returns end of a regex literal starting at '/', or 0 if none on this line.
    static size_t regexEnd(std::u16string_view s, size_t i) {
        size_t n = s.size(), k = i + 1;
        bool inClass = false;
        if (k >= n || s[k] == u'/' || s[k] == u'*') return 0;
        while (k < n) {
            char16_t c = s[k];
            if (c == u'\\') { k += 2; continue; }
            if (c == u'[') inClass = true;
            else if (c == u']') inClass = false;
            else if (c == u'/' && !inClass) {
                ++k;
                while (k < n && isAlpha(s[k])) ++k;
                return k;
            }
            ++k;
        }
        return 0;
    }
};

}  // namespace

LexerPtr createJsLexer(const LanguageOptions&) { return std::make_unique<JsLexer>(); }

}  // namespace shl
