// Chooses and constructs the lexer for a language (native or regex backend).
#pragma once

#include <filesystem>
#include <string>

#include "Config.h"
#include "Token.h"

namespace shl {

bool hasNativeLexer(std::string_view lexerId);
std::vector<std::string> nativeLexerIds();  // registration order
LexerPtr createNativeLexer(std::string_view lexerId, const LanguageOptions& opts);

std::filesystem::path rulesPathFor(const std::filesystem::path& pluginDir, const LanguageDef& lang);

struct LexerSelection {
    LexerPtr lexer;                         // null if the language cannot be handled
    TokenizerKind kind = TokenizerKind::Native;
    std::string notice;                     // non-empty: shown in the info bar
};

LexerSelection selectLexer(const Config& cfg, const LanguageDef& lang, const std::filesystem::path& pluginDir,
                           const AnsiConverter& ansi = {});

// True if the plugin can handle files of this language at all (native lexer or
// a rules file exists). Used to build the detect string.
bool isLanguageAvailable(const LanguageDef& lang, const std::filesystem::path& pluginDir);

}  // namespace shl
