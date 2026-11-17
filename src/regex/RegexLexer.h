// Data-driven lexer driven by INI-style .rules files (see README / specs §9).
//
//   [Meta]
//   name=JSON (regex)
//
//   [Rules]                 ; initial state "main"
//   10=key:"(?:\\.|[^"\\])*"(?=\s*:)
//   20=comment:/\*
//   20.push=block_comment   ; enter state after the match
//
//   [Rules.block_comment]
//   default=comment         ; class for text no rule matches
//   eol_pop=false           ; true: the state ends at the end of the line
//   10=comment:\*/
//   10.pop=true             ; return to the previous state
//
// Rules of a state are tried in ascending numeric order at the current
// position; the first match wins. Unmatched characters get the state's
// default class. Regexes are ECMAScript (std::regex) and match within a line.
#pragma once

#include <memory>
#include <string>

#include "core/IniDocument.h"
#include "core/Token.h"

namespace shl {

struct RulesParseResult {
    LexerPtr lexer;     // null on failure
    std::string name;   // [Meta] name
    std::string error;  // human-readable reason on failure
};

RulesParseResult parseRules(const IniDocument& doc);

}  // namespace shl
