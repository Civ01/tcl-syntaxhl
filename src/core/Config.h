// Typed plugin configuration (tcl-syntaxhl.ini).
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "IniDocument.h"

namespace shl {

enum class TokenizerKind { Native, Regex };

const char* tokenizerName(TokenizerKind k);
bool parseTokenizer(std::string_view s, TokenizerKind& out);
bool parseBool(std::string_view s, bool def);
// "5M", "512K", "1048576", "2G" (binary multiples). Returns def on error.
uint64_t parseSize(std::string_view s, uint64_t def);
std::string formatSize(uint64_t bytes);  // "12.4 MB"
std::string formatSizeSpec(uint64_t bytes);  // INI form: "2M", "512K", "1000"
// Language names become file names (themes, rules): [A-Za-z0-9_+-], not empty,
// not "General"/"Languages".
bool isValidLanguageName(std::string_view name);

// Per-language options from the [<LanguageName>] section (keys lowercased).
struct LanguageOptions {
    std::map<std::string, std::string> values;
    std::string get(std::string_view key, std::string_view def = {}) const;
    bool getBool(std::string_view key, bool def) const;
};

struct LanguageDef {
    std::string name;                     // as written in [Languages], e.g. "Python"
    std::string id;                       // lowercase name: theme/rules file stem
    std::vector<std::string> extensions;  // lowercase, no dots
    LanguageOptions options;
    // Lexer implementation id. Defaults to `id`; override with "lexer=" in the
    // language section to reuse a lexer (e.g. [JSON5] lexer=json).
    std::string lexerId;
    std::optional<TokenizerKind> tokenizer;  // per-language override
};

// Builds a language from its [Languages] entry ("Python", "py,pyw").
LanguageDef makeLanguage(std::string name, std::string_view extensions);
// Re-derives lexerId / tokenizer from lang.options (after the options changed).
void refreshLanguage(LanguageDef& lang);

struct Config {
    TokenizerKind tokenizer = TokenizerKind::Native;
    uint64_t maxHighlightSize = 2ull * 1024 * 1024;
    bool useLazyHighlight = false;
    std::string theme = "auto";
    std::string themeLight = "light";
    std::string themeDark = "dark";
    std::string fontFace = "Consolas";
    int fontSize = 10;
    int tabWidth = 4;            // columns per tab stop (display only)
    bool allowBeeps = false;     // RichEdit beeps (e.g. PgDn at the end of the file)
    bool debugLog = false;       // write tcl-syntaxhl.log (troubleshooting)
    bool italicComments = true;  // false: never draw comments in italic
    std::vector<LanguageDef> languages;

    static Config defaults();
    static Config fromIni(const IniDocument& ini);

    const LanguageDef* languageForExtension(std::string_view ext) const;
    const LanguageDef* languageForFile(const std::filesystem::path& file) const;
    TokenizerKind tokenizerFor(const LanguageDef& lang) const;
    // [General] italic_comments, overridable per language ([INI] italic_comments=...).
    bool italicCommentsFor(const LanguageDef& lang) const;
    // [General] tab_width, overridable per language. Clamped to 1..32.
    int tabWidthFor(const LanguageDef& lang) const;
    std::string themeSet(bool darkMode) const;

    // EXT="JSON" | EXT="PY" ... for languages accepted by `available`,
    // truncated (by whole entries) to fit maxLen-1 characters.
    std::string buildDetectString(const std::function<bool(const LanguageDef&)>& available, size_t maxLen) const;
};

}  // namespace shl
