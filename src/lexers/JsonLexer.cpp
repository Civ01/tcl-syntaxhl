// JSON / JSONC lexer.
//
// State: mode bit0 = inside block comment, bit1 = expecting an object key.
// depth = container nesting depth; extra bit i = container at level i is an
// object (levels beyond 64 are treated as objects).
#include "LineLexer.h"
#include "Lexers.h"

namespace shl {
namespace {

constexpr uint32_t kInComment = 1;
constexpr uint32_t kExpectKey = 2;

class JsonLexer final : public LineLexer {
public:
    explicit JsonLexer(bool jsonc) : jsonc_(jsonc) {}

protected:
    void lexLine(std::u16string_view s, LexState& st, Emitter& em) override {
        const TokenClass commentCls = jsonc_ ? TokenClass::Comment : TokenClass::Error;
        size_t i = 0, n = s.size();
        while (i < n) {
            if (st.mode & kInComment) {
                size_t end = s.find(u"*/", i);
                if (end == std::u16string_view::npos) {
                    em.add(i, n - i, commentCls);
                    return;
                }
                em.add(i, end + 2 - i, commentCls);
                i = end + 2;
                st.mode &= ~kInComment;
                continue;
            }
            char16_t c = s[i];
            if (isSpace(c)) { ++i; continue; }
            if (c == u'"') { i = string(s, i, st, em); continue; }
            if (c == u'{' || c == u'[') {
                push(st, c == u'{');
                setExpectKey(st, c == u'{');
                em.add(i++, 1, TokenClass::Punctuation);
                continue;
            }
            if (c == u'}' || c == u']') {
                if (st.depth > 0) --st.depth;
                setExpectKey(st, false);
                em.add(i++, 1, TokenClass::Punctuation);
                continue;
            }
            if (c == u':') { setExpectKey(st, false); em.add(i++, 1, TokenClass::Punctuation); continue; }
            if (c == u',') { setExpectKey(st, topIsObject(st)); em.add(i++, 1, TokenClass::Punctuation); continue; }
            if (c == u'/' && i + 1 < n && s[i + 1] == u'/') { em.add(i, n - i, commentCls); return; }
            if (c == u'/' && i + 1 < n && s[i + 1] == u'*') {
                st.mode |= kInComment;
                em.add(i, 2, commentCls);
                i += 2;
                continue;
            }
            if (c == u'-' || isDigit(c)) {
                size_t k = i + (c == u'-' ? 1 : 0);
                size_t end = k < n && isDigit(s[k]) ? scanCNumber(s, k, false) : k;
                bool bad = end == k || (end < n && isIdentChar(s[end]));
                if (bad) end = scanIdent(s, end == k ? k : end);
                if (end == i) end = i + 1;
                em.add(i, end - i, bad ? TokenClass::Error : TokenClass::Number);
                setExpectKey(st, false);
                i = end;
                continue;
            }
            if (isIdentStart(c)) {
                size_t end = scanIdent(s, i);
                auto w = s.substr(i, end - i);
                bool lit = equalsAscii(w, "true") || equalsAscii(w, "false") || equalsAscii(w, "null");
                em.add(i, end - i, lit ? TokenClass::Constant : TokenClass::Error);
                setExpectKey(st, false);
                i = end;
                continue;
            }
            em.add(i++, 1, TokenClass::Error);
        }
    }

private:
    static bool topIsObject(const LexState& st) {
        if (st.depth == 0) return false;
        if (st.depth > 64) return true;
        return (st.extra >> (st.depth - 1)) & 1;
    }
    static void push(LexState& st, bool object) {
        if (st.depth < 64) {
            uint64_t bit = uint64_t(1) << st.depth;
            st.extra = object ? (st.extra | bit) : (st.extra & ~bit);
        }
        ++st.depth;
    }
    static void setExpectKey(LexState& st, bool v) {
        st.mode = v ? (st.mode | kExpectKey) : (st.mode & ~kExpectKey);
    }

    size_t string(std::u16string_view s, size_t i, LexState& st, Emitter& em) {
        size_t n = s.size(), start = i;
        // Find the end first; an unterminated string is an error to end of line.
        size_t k = i + 1;
        while (k < n && s[k] != u'"') k += (s[k] == u'\\' && k + 1 < n) ? 2 : 1;
        if (k >= n) {
            em.add(start, n - start, TokenClass::Error);
            return n;
        }
        bool isKey = topIsObject(st) && (st.mode & kExpectKey);
        TokenClass cls = isKey ? TokenClass::Key : TokenClass::String;
        size_t runStart = i;
        size_t j = i + 1;
        while (j < k) {
            if (s[j] == u'\\') {
                em.add(runStart, j - runStart, cls);
                size_t escLen = 2;
                if (j + 1 < k && s[j + 1] == u'u') {
                    escLen = 2;
                    while (escLen < 6 && j + escLen < k && isHexDigit(s[j + escLen])) ++escLen;
                }
                em.add(j, escLen, TokenClass::StringEscape);
                j += escLen;
                runStart = j;
            } else {
                ++j;
            }
        }
        em.add(runStart, k + 1 - runStart, cls);
        if (isKey) setExpectKey(st, false);
        return k + 1;
    }

    bool jsonc_;
};

}  // namespace

LexerPtr createJsonLexer(const LanguageOptions& opts) {
    return std::make_unique<JsonLexer>(opts.getBool("support_jsonc_comments", false));
}

}  // namespace shl
