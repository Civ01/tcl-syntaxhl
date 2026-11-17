#include "OptionSchema.h"

#include <cstdlib>

#include "TextUtil.h"

namespace shl {

std::string OptionDef::normalize(std::string_view value) const {
    auto v = std::string(trim(value));
    switch (type) {
        case OptionType::Bool: {
            auto l = toLowerAscii(v);
            if (l == "1" || l == "true" || l == "yes" || l == "on") return "true";
            if (l == "0" || l == "false" || l == "no" || l == "off") return "false";
            return {};
        }
        case OptionType::Int: {
            if (v.empty()) return {};
            char* end = nullptr;
            long n = std::strtol(v.c_str(), &end, 10);
            if (*end != '\0' || n < minValue || n > maxValue) return {};
            return std::to_string(n);
        }
        case OptionType::Size: {
            const uint64_t bad = ~uint64_t(0);
            uint64_t n = parseSize(v, bad);
            return n == bad ? std::string() : formatSizeSpec(n);
        }
        case OptionType::Enum:
            for (const char* c : choices)
                if (iequals(v, c)) return c;
            return {};
        case OptionType::ThemeSet:
            for (const char* c : choices)
                if (iequals(v, c)) return c;
            return v;  // folder name, kept as written
        case OptionType::Font:
            return v;
    }
    return v;
}

bool OptionDef::sameValue(std::string_view a, std::string_view b) const {
    auto na = normalize(a), nb = normalize(b);
    if (na.empty() || nb.empty()) return trim(a) == trim(b);
    if (type == OptionType::ThemeSet || type == OptionType::Font) return iequals(na, nb);
    return na == nb;
}

const std::vector<OptionDef>& generalOptions() {
    static const std::vector<OptionDef> defs = {
        {"tokenizer", OptionType::Enum, "Tokenizer", "native lexers, or data-driven rules from rules\\<language>.rules",
         "native", 0, 0, {"native", "regex"}},
        {"max_highlight_size", OptionType::Size, "Highlight files up to",
         "larger files are shown as plain text (or lazily highlighted); bytes or K/M/G", "2M"},
        {"use_lazy_highlight", OptionType::Bool, "Highlight larger files while scrolling (lazy)",
         "instead of showing them as plain text", "false"},
        {"theme", OptionType::ThemeSet, "Theme", "\"auto\" follows Total Commander's dark mode", "auto", 0, 0,
         {"auto"}},
        {"theme_light", OptionType::ThemeSet, "Light theme", "used by theme=auto in light mode", "light"},
        {"theme_dark", OptionType::ThemeSet, "Dark theme", "used by theme=auto in dark mode", "dark"},
        {"font_face", OptionType::Font, "Font", "font of the text view", "Consolas"},
        {"font_size", OptionType::Int, "Font size", "points; Ctrl+Mouse wheel zooms at runtime", "10", 4, 96},
        {"tab_width", OptionType::Int, "Tab width", "columns per tab character (display only)", "4", 1, 32},
        {"italic_comments", OptionType::Bool, "Italic comments", "draw comments in italic when the theme says so",
         "true"},
        {"allow_beeps", OptionType::Bool, "Allow beeps", "beep when navigation hits the start/end of the file",
         "false"},
        {"debug_log", OptionType::Bool, "Write debug log", "tcl-syntaxhl.log next to the plugin (troubleshooting)",
         "false"},
    };
    return defs;
}

const OptionDef* findGeneralOption(std::string_view key) {
    for (const auto& d : generalOptions())
        if (iequals(key, d.key)) return &d;
    return nullptr;
}

std::string getGeneralValue(const Config& c, std::string_view key) {
    auto b = [](bool v) { return std::string(v ? "true" : "false"); };
    if (key == "tokenizer") return tokenizerName(c.tokenizer);
    if (key == "max_highlight_size") return formatSizeSpec(c.maxHighlightSize);
    if (key == "use_lazy_highlight") return b(c.useLazyHighlight);
    if (key == "theme") return c.theme;
    if (key == "theme_light") return c.themeLight;
    if (key == "theme_dark") return c.themeDark;
    if (key == "font_face") return c.fontFace;
    if (key == "font_size") return std::to_string(c.fontSize);
    if (key == "tab_width") return std::to_string(c.tabWidth);
    if (key == "italic_comments") return b(c.italicComments);
    if (key == "allow_beeps") return b(c.allowBeeps);
    if (key == "debug_log") return b(c.debugLog);
    return {};
}

bool setGeneralValue(Config& c, std::string_view key, std::string_view value) {
    const OptionDef* d = findGeneralOption(key);
    if (!d) return false;
    std::string v = d->normalize(value);
    if (v.empty()) return false;
    if (key == "tokenizer") return parseTokenizer(v, c.tokenizer);
    if (key == "max_highlight_size") { c.maxHighlightSize = parseSize(v, c.maxHighlightSize); return true; }
    if (key == "use_lazy_highlight") { c.useLazyHighlight = v == "true"; return true; }
    if (key == "theme") { c.theme = v; return true; }
    if (key == "theme_light") { c.themeLight = v; return true; }
    if (key == "theme_dark") { c.themeDark = v; return true; }
    if (key == "font_face") { c.fontFace = v; return true; }
    if (key == "font_size") { c.fontSize = std::atoi(v.c_str()); return true; }
    if (key == "tab_width") { c.tabWidth = std::atoi(v.c_str()); return true; }
    if (key == "italic_comments") { c.italicComments = v == "true"; return true; }
    if (key == "allow_beeps") { c.allowBeeps = v == "true"; return true; }
    if (key == "debug_log") { c.debugLog = v == "true"; return true; }
    return false;
}

const std::vector<OptionDef>& languageOverrideOptions() {
    static const std::vector<OptionDef> defs = {
        {"tokenizer", OptionType::Enum, "Tokenizer", "overrides the General tokenizer", "", 0, 0,
         {"native", "regex"}},
        {"italic_comments", OptionType::Bool, "Italic comments", "overrides the General setting", ""},
        {"tab_width", OptionType::Int, "Tab width", "overrides the General setting", "", 1, 32},
    };
    return defs;
}

const std::vector<OptionDef>& lexerOptions(std::string_view lexerId) {
    static const std::vector<OptionDef> none;
    static const std::vector<OptionDef> json = {
        {"support_jsonc_comments", OptionType::Bool, "Allow // and /* */ comments (JSONC)",
         "when off, comments are marked as errors", "false"},
    };
    static const std::vector<OptionDef> css = {
        {"support_scss_less", OptionType::Bool, "SCSS / LESS syntax",
         "// comments, $var / @var, nesting, &, #{...}, @mixin/@include", "false"},
    };
    if (lexerId == "json") return json;
    if (lexerId == "css") return css;
    return none;
}

}  // namespace shl
