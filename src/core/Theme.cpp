#include "Theme.h"

#include "TextUtil.h"

namespace shl {

bool parseColor(std::string_view s, Color& out) {
    auto v = trim(s);
    if (!v.empty() && v[0] == '#') v.remove_prefix(1);
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    if (v.size() == 3) {
        Color c = 0;
        for (char ch : v) {
            int h = hex(ch);
            if (h < 0) return false;
            c = (c << 8) | static_cast<Color>(h * 17);
        }
        out = c;
        return true;
    }
    if (v.size() != 6) return false;
    Color c = 0;
    for (char ch : v) {
        int h = hex(ch);
        if (h < 0) return false;
        c = (c << 4) | static_cast<Color>(h);
    }
    out = c;
    return true;
}

namespace {
struct Builtin {
    TokenClass cls;
    Color light;
    Color dark;
    uint8_t flags;
};
// Base palettes (light ~ VS light, dark ~ VS dark+).
const Builtin kBuiltin[] = {
    {TokenClass::Keyword, 0x0000FF, 0x569CD6, 0},
    {TokenClass::Builtin, 0x267F99, 0x4EC9B0, 0},
    {TokenClass::Constant, 0x0000FF, 0x569CD6, 0},
    {TokenClass::Function, 0x795E26, 0xDCDCAA, 0},
    {TokenClass::Type, 0x267F99, 0x4EC9B0, 0},
    {TokenClass::Decorator, 0xAF00DB, 0xC586C0, 0},
    {TokenClass::Number, 0x098658, 0xB5CEA8, 0},
    {TokenClass::String, 0xA31515, 0xCE9178, 0},
    {TokenClass::StringEscape, 0xEE0000, 0xD7BA7D, 0},
    {TokenClass::Comment, 0x008000, 0x6A9955, kItalic},
    {TokenClass::Operator, 0x000000, 0xD4D4D4, 0},
    {TokenClass::Punctuation, 0x000000, 0xD4D4D4, 0},
    {TokenClass::Key, 0x0451A5, 0x9CDCFE, 0},
    {TokenClass::Section, 0x800000, 0x569CD6, kBold},
    {TokenClass::Value, 0x000000, 0xCE9178, 0},
    {TokenClass::Selector, 0x800000, 0xD7BA7D, 0},
    {TokenClass::Property, 0xE50000, 0x9CDCFE, 0},
    {TokenClass::AtRule, 0xAF00DB, 0xC586C0, 0},
    {TokenClass::Variable, 0x001080, 0x9CDCFE, 0},
    {TokenClass::Regex, 0x811F3F, 0xD16969, 0},
    {TokenClass::Template, 0xA31515, 0xCE9178, 0},
    {TokenClass::Error, 0xFF0000, 0xF44747, kUnderline},
};
}  // namespace

Theme Theme::builtin(bool dark) {
    Theme t;
    t.background = dark ? 0x1E1E1E : 0xFFFFFF;
    t.foreground = dark ? 0xD4D4D4 : 0x000000;
    t.infobarBackground = dark ? 0x3C3C00 : 0xFFF4CE;
    t.infobarForeground = dark ? 0xF0F0F0 : 0x000000;
    for (auto& s : t.styles) s.fg = t.foreground;
    for (const auto& b : kBuiltin) {
        auto& s = t.styles[static_cast<size_t>(b.cls)];
        s.fg = dark ? b.dark : b.light;
        s.flags = b.flags;
    }
    return t;
}

void Theme::apply(const IniDocument& doc, std::vector<std::string>* warnings) {
    auto warn = [&](const std::string& w) {
        if (warnings) warnings->push_back(w);
    };
    Color oldFg = foreground;
    struct ViewKey { const char* name; Color* dst; };
    ViewKey view[] = {{"background", &background},
                      {"foreground", &foreground},
                      {"infobar_background", &infobarBackground},
                      {"infobar_foreground", &infobarForeground}};
    for (auto& [k, v] : doc.entries("View")) {
        bool known = false;
        for (auto& vk : view) {
            if (iequals(k, vk.name)) {
                known = true;
                if (!parseColor(v, *vk.dst)) warn("View." + k);
            }
        }
        if (!known) warn("View." + k);
    }
    // Classes still using the old default foreground follow a changed one.
    if (foreground != oldFg)
        for (auto& s : styles)
            if (s.fg == oldFg) s.fg = foreground;

    for (auto& [k, v] : doc.entries("Tokens")) {
        TokenClass cls;
        if (!parseTokenClass(k, cls) || cls == TokenClass::Count) { warn("Tokens." + k); continue; }
        size_t ci = static_cast<size_t>(cls);
        auto tv = trim(v);
        if (!tv.empty() && tv[0] == '@') {
            TokenClass target;
            if (!parseTokenClass(tv.substr(1), target) || target == cls) { warn("Tokens." + k); continue; }
            alias_[ci] = static_cast<int>(target) + 1;
            explicit_[ci] = true;
            continue;
        }
        Style& s = styles[ci];
        auto parts = std::vector<std::string>();
        {
            size_t pos = 0;
            std::string_view sv(v);
            while (true) {
                size_t c = sv.find(',', pos);
                parts.emplace_back(trim(sv.substr(pos, c == std::string_view::npos ? sv.npos : c - pos)));
                if (c == std::string_view::npos) break;
                pos = c + 1;
            }
        }
        Style ns = s;
        bool ok = true;
        if (parts.size() > 0 && !parts[0].empty()) ok &= parseColor(parts[0], ns.fg);
        if (parts.size() > 1 && !parts[1].empty()) {
            if (iequals(parts[1], "none")) ns.bg.reset();
            else {
                Color bg;
                if (parseColor(parts[1], bg)) ns.bg = bg;
                else ok = false;
            }
        }
        if (parts.size() > 2 && !parts[2].empty()) {
            ns.flags = 0;
            for (auto& f : splitList(parts[2], '+')) {
                auto lf = toLowerAscii(f);
                if (lf == "bold") ns.flags |= kBold;
                else if (lf == "italic") ns.flags |= kItalic;
                else if (lf == "underline") ns.flags |= kUnderline;
                else if (lf == "normal" || lf == "none") {}
                else ok = false;
            }
        }
        if (ok) {
            s = ns;
            explicit_[ci] = true;
            alias_[ci] = 0;
        } else {
            warn("Tokens." + k);
        }
    }
    styles[static_cast<size_t>(TokenClass::Default)].fg = foreground;
}

TokenClass parentClass(TokenClass c) {
    switch (c) {
        case TokenClass::SectionBracket: return TokenClass::Punctuation;
        default: return TokenClass::Count;
    }
}

void Theme::finalize() {
    Style resolved[kTokenClassCount];
    bool done[kTokenClassCount] = {};
    // Iterative resolution with a depth guard (aliases may chain or cycle).
    for (size_t c = 0; c < kTokenClassCount; ++c) {
        size_t cur = c;
        for (int depth = 0; depth < 16; ++depth) {
            if (alias_[cur]) { cur = static_cast<size_t>(alias_[cur] - 1); continue; }
            TokenClass parent = parentClass(static_cast<TokenClass>(cur));
            if (!explicit_[cur] && parent != TokenClass::Count) { cur = static_cast<size_t>(parent); continue; }
            break;
        }
        resolved[c] = styles[cur];
        done[c] = true;
    }
    for (size_t c = 0; c < kTokenClassCount; ++c)
        if (done[c]) styles[c] = resolved[c];
}

Theme loadTheme(const std::filesystem::path& pluginDir, const std::string& set, const std::string& languageId,
                bool dark, const AnsiConverter& ansi) {
    Theme t = Theme::builtin(dark);
    auto dir = pluginDir / "themes" / utf8Path(set);
    for (const auto& name : {std::string("default"), languageId}) {
        if (name.empty()) continue;
        IniDocument doc;
        std::error_code ec;
        auto p = dir / utf8Path(name + ".theme");
        if (std::filesystem::exists(p, ec) && IniDocument::loadFile(p, doc, ansi)) t.apply(doc);
    }
    t.finalize();
    return t;
}

}  // namespace shl
