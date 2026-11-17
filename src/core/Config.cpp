#include "Config.h"

#include <cstdio>

#include "TextUtil.h"

namespace shl {

const char* tokenizerName(TokenizerKind k) { return k == TokenizerKind::Regex ? "regex" : "native"; }

bool parseTokenizer(std::string_view s, TokenizerKind& out) {
    auto v = toLowerAscii(trim(s));
    if (v == "native") { out = TokenizerKind::Native; return true; }
    if (v == "regex") { out = TokenizerKind::Regex; return true; }
    return false;
}

bool parseBool(std::string_view s, bool def) {
    auto v = toLowerAscii(trim(s));
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    return def;
}

uint64_t parseSize(std::string_view s, uint64_t def) {
    auto v = trim(s);
    if (v.empty()) return def;
    uint64_t num = 0;
    size_t i = 0;
    bool any = false;
    while (i < v.size() && v[i] >= '0' && v[i] <= '9') {
        num = num * 10 + static_cast<uint64_t>(v[i] - '0');
        ++i;
        any = true;
    }
    if (!any) return def;
    auto suffix = toLowerAscii(trim(v.substr(i)));
    if (suffix.empty() || suffix == "b") return num;
    if (suffix == "k" || suffix == "kb") return num * 1024;
    if (suffix == "m" || suffix == "mb") return num * 1024 * 1024;
    if (suffix == "g" || suffix == "gb") return num * 1024ull * 1024 * 1024;
    return def;
}

std::string formatSize(uint64_t b) {
    char buf[64];
    if (b < 1024) std::snprintf(buf, sizeof buf, "%llu bytes", static_cast<unsigned long long>(b));
    else if (b < 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f KB", b / 1024.0);
    else if (b < 1024ull * 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f MB", b / (1024.0 * 1024.0));
    else std::snprintf(buf, sizeof buf, "%.1f GB", b / (1024.0 * 1024.0 * 1024.0));
    std::string r = buf;
    // "5.0 MB" -> "5 MB"
    auto p = r.find(".0 ");
    if (p != std::string::npos) r.erase(p, 2);
    return r;
}

std::string LanguageOptions::get(std::string_view key, std::string_view def) const {
    auto it = values.find(toLowerAscii(key));
    return it == values.end() ? std::string(def) : it->second;
}

bool LanguageOptions::getBool(std::string_view key, bool def) const {
    auto it = values.find(toLowerAscii(key));
    return it == values.end() ? def : parseBool(it->second, def);
}

static LanguageDef makeLang(std::string name, std::string_view exts) {
    LanguageDef d;
    d.id = toLowerAscii(name);
    d.name = std::move(name);
    for (auto& e : splitList(exts, ',')) {
        auto x = toLowerAscii(e);
        while (!x.empty() && (x[0] == '.' || x[0] == '*')) x.erase(0, 1);
        if (!x.empty()) d.extensions.push_back(x);
    }
    d.lexerId = d.id;
    return d;
}

Config Config::defaults() {
    Config c;
    c.languages.push_back(makeLang("JSON", "json"));
    c.languages.push_back(makeLang("CSS", "css"));
    c.languages.push_back(makeLang("Python", "py,pyw"));
    c.languages.push_back(makeLang("JS", "js,mjs,cjs"));
    c.languages.push_back(makeLang("INI", "ini,cfg,inf"));
    return c;
}

Config Config::fromIni(const IniDocument& ini) {
    Config c = defaults();
    const char* G = "General";
    if (auto v = ini.get(G, "tokenizer")) parseTokenizer(*v, c.tokenizer);
    if (auto v = ini.get(G, "max_highlight_size")) c.maxHighlightSize = parseSize(*v, c.maxHighlightSize);
    if (auto v = ini.get(G, "use_lazy_highlight")) c.useLazyHighlight = parseBool(*v, c.useLazyHighlight);
    if (auto v = ini.get(G, "theme"); v && !v->empty()) c.theme = *v;
    if (auto v = ini.get(G, "theme_light"); v && !v->empty()) c.themeLight = *v;
    if (auto v = ini.get(G, "theme_dark"); v && !v->empty()) c.themeDark = *v;
    if (auto v = ini.get(G, "font_face"); v && !v->empty()) c.fontFace = *v;
    if (auto v = ini.get(G, "font_size")) {
        int n = std::atoi(v->c_str());
        if (n >= 4 && n <= 96) c.fontSize = n;
    }
    if (auto v = ini.get(G, "enable_config_dialog")) c.enableConfigDialog = parseBool(*v, false);
    if (auto v = ini.get(G, "italic_comments")) c.italicComments = parseBool(*v, true);
    if (auto v = ini.get(G, "allow_beeps")) c.allowBeeps = parseBool(*v, false);
    if (auto v = ini.get(G, "tab_width")) {
        int n = std::atoi(v->c_str());
        if (n >= 1 && n <= 32) c.tabWidth = n;
    }

    if (ini.hasSection("Languages")) {
        c.languages.clear();
        for (auto& [name, exts] : ini.entries("Languages")) {
            if (name.empty()) continue;
            c.languages.push_back(makeLang(name, exts));
        }
    }
    for (auto& lang : c.languages) {
        for (auto& [k, v] : ini.entries(lang.name)) lang.options.values[toLowerAscii(k)] = v;
        auto lex = toLowerAscii(trim(lang.options.get("lexer")));
        if (!lex.empty()) lang.lexerId = lex;
        TokenizerKind tk;
        if (parseTokenizer(lang.options.get("tokenizer"), tk)) lang.tokenizer = tk;
    }
    return c;
}

void Config::writeGeneral(IniDocument& ini) const {
    const char* G = "General";
    ini.set(G, "tokenizer", tokenizerName(tokenizer));
    ini.set(G, "max_highlight_size", std::to_string(maxHighlightSize));
    ini.set(G, "use_lazy_highlight", useLazyHighlight ? "true" : "false");
    ini.set(G, "theme", theme);
    ini.set(G, "theme_light", themeLight);
    ini.set(G, "theme_dark", themeDark);
    ini.set(G, "font_face", fontFace);
    ini.set(G, "font_size", std::to_string(fontSize));
    ini.set(G, "enable_config_dialog", enableConfigDialog ? "true" : "false");
    ini.set(G, "italic_comments", italicComments ? "true" : "false");
    ini.set(G, "tab_width", std::to_string(tabWidth));
    ini.set(G, "allow_beeps", allowBeeps ? "true" : "false");
}

const LanguageDef* Config::languageForExtension(std::string_view ext) const {
    auto e = toLowerAscii(ext);
    while (!e.empty() && e[0] == '.') e.erase(0, 1);
    if (e.empty()) return nullptr;
    for (const auto& l : languages)
        for (const auto& x : l.extensions)
            if (x == e) return &l;
    return nullptr;
}

const LanguageDef* Config::languageForFile(const std::filesystem::path& file) const {
    auto ext = file.extension().u8string();
    return languageForExtension(std::string(ext.begin(), ext.end()));
}

TokenizerKind Config::tokenizerFor(const LanguageDef& lang) const {
    return lang.tokenizer ? *lang.tokenizer : tokenizer;
}

bool Config::italicCommentsFor(const LanguageDef& lang) const {
    return lang.options.getBool("italic_comments", italicComments);
}

int Config::tabWidthFor(const LanguageDef& lang) const {
    auto v = lang.options.get("tab_width");
    int n = v.empty() ? 0 : std::atoi(v.c_str());
    return n >= 1 && n <= 32 ? n : tabWidth;
}

std::string Config::themeSet(bool darkMode) const {
    if (iequals(theme, "auto")) return darkMode ? themeDark : themeLight;
    return theme;
}

std::string Config::buildDetectString(const std::function<bool(const LanguageDef&)>& available,
                                      size_t maxLen) const {
    std::string out;
    std::vector<std::string> seen;
    for (const auto& l : languages) {
        if (available && !available(l)) continue;
        for (const auto& e : l.extensions) {
            bool dup = false;
            for (auto& s : seen) dup |= (s == e);
            if (dup) continue;
            seen.push_back(e);
            std::string upper = e;
            for (auto& ch : upper)
                if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
            std::string item = (out.empty() ? "" : " | ") + std::string("EXT=\"") + upper + "\"";
            if (maxLen && out.size() + item.size() + 1 > maxLen) return out;
            out += item;
        }
    }
    return out;
}

}  // namespace shl
