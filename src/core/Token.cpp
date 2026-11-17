#include "Token.h"

#include "TextUtil.h"

namespace shl {

namespace {
const char* const kNames[kTokenClassCount] = {
    "default",  "keyword",     "builtin",  "constant",    "identifier", "function",
    "type",     "decorator",   "number",   "string",      "string_escape", "comment",
    "operator", "punctuation", "key",      "section",     "value",      "selector",
    "property", "at_rule",     "variable", "regex",       "template",   "error",      "section_bracket",
};
}  // namespace

const char* tokenClassName(TokenClass c) {
    auto i = static_cast<size_t>(c);
    return i < kTokenClassCount ? kNames[i] : "default";
}

bool parseTokenClass(std::string_view name, TokenClass& out) {
    std::string n = toLowerAscii(trim(name));
    for (size_t i = 0; i < kTokenClassCount; ++i) {
        if (n == kNames[i]) {
            out = static_cast<TokenClass>(i);
            return true;
        }
    }
    return false;
}

}  // namespace shl
