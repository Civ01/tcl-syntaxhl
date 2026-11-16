// Python 3.12 lexer.
//
// State (mode) while inside a string that continues on the next line:
//   bits 0-1: 0 = none, 1 = ''' triple, 2 = """ triple, 3 = single-line string
//             continued with a trailing backslash
//   bit 2   : raw string (no escapes)
//   bit 3   : for kind 3, the quote is '"' (else '\'')
#include "LineLexer.h"
#include "Lexers.h"

namespace shl {
namespace {

constexpr uint32_t kKindMask = 3, kTripleSingle = 1, kTripleDouble = 2, kContinued = 3;
constexpr uint32_t kRaw = 4, kDoubleQuote = 8;

const WordSet& keywords() {
    static const WordSet w{"and",   "as",     "assert", "async",  "await",    "break", "class",
                           "continue", "def", "del",    "elif",   "else",     "except", "finally",
                           "for",   "from",   "global", "if",     "import",   "in",    "is",
                           "lambda", "nonlocal", "not", "or",     "pass",     "raise", "return",
                           "try",   "while",  "with",   "yield"};
    return w;
}
const WordSet& constants() {
    static const WordSet w{"True", "False", "None", "Ellipsis", "NotImplemented", "__debug__"};
    return w;
}
const WordSet& builtins() {
    static const WordSet w{
        "abs", "aiter", "all", "anext", "any", "ascii", "bin", "bool", "breakpoint", "bytearray",
        "bytes", "callable", "chr", "classmethod", "compile", "complex", "delattr", "dict", "dir",
        "divmod", "enumerate", "eval", "exec", "filter", "float", "format", "frozenset", "getattr",
        "globals", "hasattr", "hash", "help", "hex", "id", "input", "int", "isinstance",
        "issubclass", "iter", "len", "list", "locals", "map", "max", "memoryview", "min", "next",
        "object", "oct", "open", "ord", "pow", "print", "property", "range", "repr", "reversed",
        "round", "set", "setattr", "slice", "sorted", "staticmethod", "str", "sum", "super",
        "tuple", "type", "vars", "zip", "__import__", "self", "cls",
        "BaseException", "Exception", "ArithmeticError", "AssertionError", "AttributeError",
        "EOFError", "ImportError", "ModuleNotFoundError", "IndexError", "KeyError",
        "KeyboardInterrupt", "LookupError", "MemoryError", "NameError", "NotImplementedError",
        "OSError", "OverflowError", "RecursionError", "RuntimeError", "StopIteration",
        "StopAsyncIteration", "SyntaxError", "SystemExit", "TypeError", "ValueError",
        "ZeroDivisionError", "FileNotFoundError", "PermissionError", "TimeoutError",
        "UnicodeError", "UnicodeDecodeError", "UnicodeEncodeError", "Warning", "DeprecationWarning",
        "UserWarning", "RuntimeWarning"};
    return w;
}

class PythonLexer final : public LineLexer {
protected:
    void lexLine(std::u16string_view s, LexState& st, Emitter& em) override {
        size_t n = s.size(), i = 0;
        if ((st.mode & kKindMask) != 0) {
            i = continueString(s, 0, st, em);
            if (i >= n) return;
        }
        // Logical-line start position (after indentation) for decorators and
        // soft keywords. Only valid when the line did not start inside a string.
        size_t lineStart = skipSpaces(s, i);
        bool atLineStart = true;
        int expectName = 0;  // 1 = function name (after def), 2 = class name

        while (i < n) {
            char16_t c = s[i];
            if (isSpace(c)) { ++i; continue; }
            bool lineStartHere = atLineStart && i == lineStart;
            atLineStart = false;
            if (c == u'#') { em.add(i, n - i, TokenClass::Comment); return; }

            if (c == u'@' && lineStartHere) {
                size_t k = i + 1;
                while (k < n && (isIdentChar(s[k]) || s[k] == u'.')) ++k;
                if (k > i + 1) { em.add(i, k - i, TokenClass::Decorator); i = k; continue; }
            }

            if (isIdentStart(c)) {
                size_t end = scanIdent(s, i);
                // String prefix?
                if (end < n && (s[end] == u'\'' || s[end] == u'"') && isStringPrefix(s.substr(i, end - i))) {
                    i = startString(s, i, end, st, em);
                    expectName = 0;
                    continue;
                }
                auto w = s.substr(i, end - i);
                TokenClass cls = TokenClass::Identifier;
                if (expectName == 1) cls = TokenClass::Function;
                else if (expectName == 2) cls = TokenClass::Type;
                else if (keywords().contains(w)) cls = TokenClass::Keyword;
                else if (constants().contains(w)) cls = TokenClass::Constant;
                else if (lineStartHere && isSoftKeyword(s, w, end)) cls = TokenClass::Keyword;
                else if (builtins().contains(w)) cls = TokenClass::Builtin;
                expectName = 0;
                if (cls == TokenClass::Keyword) {
                    if (equalsAscii(w, "def")) expectName = 1;
                    else if (equalsAscii(w, "class")) expectName = 2;
                }
                em.add(i, end - i, cls);
                i = end;
                continue;
            }
            expectName = 0;
            if (c == u'\'' || c == u'"') { i = startString(s, i, i, st, em); continue; }
            if (isDigit(c) || (c == u'.' && i + 1 < n && isDigit(s[i + 1]))) {
                size_t end = scanCNumber(s, i, true);
                if (end < n && (s[end] == u'j' || s[end] == u'J' || s[end] == u'l' || s[end] == u'L')) ++end;
                em.add(i, end - i, TokenClass::Number);
                i = end;
                continue;
            }
            if (c == u'\\') { ++i; continue; }  // explicit line join
            static const std::u16string_view punct = u"()[]{},;.";
            em.add(i, 1, punct.find(c) != std::u16string_view::npos ? TokenClass::Punctuation : TokenClass::Operator);
            ++i;
        }
    }

private:
    static bool isStringPrefix(std::u16string_view p) {
        if (p.size() > 2) return false;
        bool r = false, b = false, f = false, u = false;
        for (char16_t c : p) {
            switch (c | 0x20) {
                case u'r': if (r) return false; r = true; break;
                case u'b': if (b) return false; b = true; break;
                case u'f': if (f) return false; f = true; break;
                case u'u': if (u) return false; u = true; break;
                default: return false;
            }
        }
        if (u && p.size() > 1) return false;
        if (b && f) return false;
        return true;
    }

    static bool isSoftKeyword(std::u16string_view s, std::u16string_view w, size_t end) {
        bool mc = equalsAscii(w, "match") || equalsAscii(w, "case");
        bool ty = equalsAscii(w, "type");
        if (!mc && !ty) return false;
        size_t k = skipSpaces(s, end);
        if (k == end || k >= s.size()) return false;  // needs a following token
        char16_t c = s[k];
        if (ty) {
            // type Name = ...   /  type Name[T] = ...
            if (!isIdentStart(c)) return false;
            size_t e = skipSpaces(s, scanIdent(s, k));
            return e < s.size() && (s[e] == u'=' || s[e] == u'[');
        }
        // match/case followed by an expression, not an assignment/attribute/call-only use.
        if (c == u'=' || c == u'.' || c == u',' || c == u')' || c == u':' || c == u';') return false;
        return s.back() == u':' || s.find(u':', k) != std::u16string_view::npos;
    }

    // Start of a string at `quotePos`; prefix occupies [prefixStart, quotePos).
    size_t startString(std::u16string_view s, size_t prefixStart, size_t quotePos, LexState& st, Emitter& em) {
        bool raw = false;
        for (size_t k = prefixStart; k < quotePos; ++k)
            if ((s[k] | 0x20) == u'r') raw = true;
        char16_t q = s[quotePos];
        bool triple = startsWith(s, quotePos, q == u'"' ? u"\"\"\"" : u"'''");
        size_t bodyStart = quotePos + (triple ? 3 : 1);
        em.add(prefixStart, bodyStart - prefixStart, TokenClass::String);
        st.mode = triple ? (q == u'"' ? kTripleDouble : kTripleSingle) : kContinued;
        if (raw) st.mode |= kRaw;
        if (q == u'"') st.mode |= kDoubleQuote;
        return continueString(s, bodyStart, st, em, !triple);
    }

    // Scans string body from i in the current string state. Returns end position.
    // `singleLineStart`: the string just started on this line (kind 3).
    size_t continueString(std::u16string_view s, size_t i, LexState& st, Emitter& em, bool = false) {
        size_t n = s.size();
        uint32_t kind = st.mode & kKindMask;
        bool raw = st.mode & kRaw;
        char16_t q = (kind == kTripleDouble || (kind == kContinued && (st.mode & kDoubleQuote))) ? u'"' : u'\'';
        bool triple = kind != kContinued;
        size_t run = i;
        while (i < n) {
            char16_t c = s[i];
            if (c == u'\\') {
                if (i + 1 >= n) {
                    // Backslash at end of line: string continues.
                    em.add(run, n - run, TokenClass::String);
                    return n;
                }
                size_t len = raw ? 2 : escapeLength(s, i);
                if (!raw) {
                    em.add(run, i - run, TokenClass::String);
                    em.add(i, len, TokenClass::StringEscape);
                    run = i + len;
                }
                i += len;
                continue;
            }
            if (c == q && (!triple || startsWith(s, i, q == u'"' ? u"\"\"\"" : u"'''"))) {
                size_t end = i + (triple ? 3 : 1);
                em.add(run, end - run, TokenClass::String);
                st.mode = 0;
                return end;
            }
            ++i;
        }
        em.add(run, n - run, TokenClass::String);
        if (!triple) st.mode = 0;  // unterminated single-line string ends at EOL
        return n;
    }

    static size_t escapeLength(std::u16string_view s, size_t i) {
        size_t n = s.size();
        char16_t c = s[i + 1];
        auto hex = [&](size_t count) {
            size_t k = i + 2;
            while (k < n && k < i + 2 + count && isHexDigit(s[k])) ++k;
            return k - i;
        };
        if (c == u'x') return hex(2);
        if (c == u'u') return hex(4);
        if (c == u'U') return hex(8);
        if (c == u'N' && i + 2 < n && s[i + 2] == u'{') {
            size_t close = s.find(u'}', i + 3);
            return close == std::u16string_view::npos ? 2 : close + 1 - i;
        }
        if (c >= u'0' && c <= u'7') {
            size_t k = i + 1;
            while (k < n && k < i + 4 && s[k] >= u'0' && s[k] <= u'7') ++k;
            return k - i;
        }
        return 2;
    }
};

}  // namespace

LexerPtr createPythonLexer(const LanguageOptions&) { return std::make_unique<PythonLexer>(); }

}  // namespace shl
