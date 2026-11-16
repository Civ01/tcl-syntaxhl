// Factories for the built-in (native) lexers.
#pragma once

#include "core/Config.h"
#include "core/Token.h"

namespace shl {

LexerPtr createJsonLexer(const LanguageOptions& opts);    // support_jsonc_comments
LexerPtr createCssLexer(const LanguageOptions& opts);     // support_scss_less
LexerPtr createPythonLexer(const LanguageOptions& opts);
LexerPtr createJsLexer(const LanguageOptions& opts);
LexerPtr createIniLexer(const LanguageOptions& opts);

}  // namespace shl
