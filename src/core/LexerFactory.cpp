#include "LexerFactory.h"

#include "TextUtil.h"
#include "lexers/Lexers.h"
#include "regex/RegexLexer.h"

namespace shl {

namespace {
struct NativeEntry {
    const char* id;
    LexerPtr (*create)(const LanguageOptions&);
};
// Register new native lexers here.
const NativeEntry kNative[] = {
    {"json", &createJsonLexer},
    {"css", &createCssLexer},
    {"python", &createPythonLexer},
    {"js", &createJsLexer},
    {"ini", &createIniLexer},
};
}  // namespace

bool hasNativeLexer(std::string_view id) {
    for (const auto& e : kNative)
        if (id == e.id) return true;
    return false;
}

LexerPtr createNativeLexer(std::string_view id, const LanguageOptions& opts) {
    for (const auto& e : kNative)
        if (id == e.id) return e.create(opts);
    return nullptr;
}

std::filesystem::path rulesPathFor(const std::filesystem::path& pluginDir, const LanguageDef& lang) {
    return pluginDir / "rules" / utf8Path(lang.id + ".rules");
}

static LexerPtr loadRules(const std::filesystem::path& path, const AnsiConverter& ansi, std::string& error) {
    IniDocument doc;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || !IniDocument::loadFile(path, doc, ansi)) {
        error = "rules file not found";
        return nullptr;
    }
    auto res = parseRules(doc);
    if (!res.lexer) error = res.error;
    return std::move(res.lexer);
}

LexerSelection selectLexer(const Config& cfg, const LanguageDef& lang, const std::filesystem::path& pluginDir,
                           const AnsiConverter& ansi) {
    LexerSelection sel;
    bool native = hasNativeLexer(lang.lexerId);
    auto rulesPath = rulesPathFor(pluginDir, lang);
    if (cfg.tokenizerFor(lang) == TokenizerKind::Regex || !native) {
        std::string err;
        if (auto lx = loadRules(rulesPath, ansi, err)) {
            sel.lexer = std::move(lx);
            sel.kind = TokenizerKind::Regex;
            return sel;
        }
        if (!native) return sel;  // nothing can handle it
        auto file = rulesPath.filename().u8string();
        sel.notice = "Regex tokenizer unavailable for " + lang.name + " (rules/" +
                     std::string(file.begin(), file.end()) + ": " + err + "); using the native lexer.";
    }
    sel.lexer = createNativeLexer(lang.lexerId, lang.options);
    sel.kind = TokenizerKind::Native;
    return sel;
}

bool isLanguageAvailable(const LanguageDef& lang, const std::filesystem::path& pluginDir) {
    if (hasNativeLexer(lang.lexerId)) return true;
    std::error_code ec;
    return std::filesystem::exists(rulesPathFor(pluginDir, lang), ec);
}

}  // namespace shl
