// Token types shared by all lexers (portable, no platform headers).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace shl {

// Token classes. APPEND ONLY: names are used in .theme and .rules files.
enum class TokenClass : uint8_t {
    Default,
    Keyword,
    Builtin,
    Constant,
    Identifier,
    Function,
    Type,
    Decorator,
    Number,
    String,
    StringEscape,
    Comment,
    Operator,
    Punctuation,
    Key,
    Section,
    Value,
    Selector,
    Property,
    AtRule,
    Variable,
    Regex,
    Template,
    Error,
    Count
};

constexpr size_t kTokenClassCount = static_cast<size_t>(TokenClass::Count);

// Lowercase, snake_case name of a class ("string_escape").
const char* tokenClassName(TokenClass c);
// Parses a class name (case-insensitive); returns false if unknown.
bool parseTokenClass(std::string_view name, TokenClass& out);

struct Token {
    uint32_t start = 0;   // offset in UTF-16 code units
    uint32_t length = 0;  // length in UTF-16 code units
    TokenClass cls = TokenClass::Default;

    bool operator==(const Token& o) const {
        return start == o.start && length == o.length && cls == o.cls;
    }
};

// Opaque lexer state carried across line boundaries. Small and copyable so it
// can be checkpointed for lazy highlighting.
struct LexState {
    uint32_t mode = 0;
    uint32_t depth = 0;
    uint64_t extra = 0;

    bool operator==(const LexState& o) const {
        return mode == o.mode && depth == o.depth && extra == o.extra;
    }
    bool operator!=(const LexState& o) const { return !(*this == o); }
};

class ILexer {
public:
    virtual ~ILexer() = default;
    // Lex `text` starting in `state`. Appends tokens (ascending, non-overlapping,
    // offsets relative to text.data()) to `out` and leaves `state` describing the
    // end of `text`. Lexing must be resumable at any line start: splitting the
    // text at line starts and carrying `state` yields identical tokens.
    virtual void lex(std::u16string_view text, LexState& state, std::vector<Token>& out) = 0;
};

using LexerPtr = std::unique_ptr<ILexer>;

}  // namespace shl
