// Color themes: built-in palettes + themes/<set>/default.theme + <lang>.theme.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "IniDocument.h"
#include "Token.h"

namespace shl {

using Color = uint32_t;  // 0xRRGGBB

enum StyleFlags : uint8_t { kBold = 1, kItalic = 2, kUnderline = 4 };

struct Style {
    Color fg = 0;
    std::optional<Color> bg;  // none = view background
    uint8_t flags = 0;
};

bool parseColor(std::string_view s, Color& out);

struct Theme {
    Color background = 0xFFFFFF;
    Color foreground = 0x000000;
    Color infobarBackground = 0xFFF4CE;
    Color infobarForeground = 0x000000;
    Style styles[kTokenClassCount];

    const Style& style(TokenClass c) const { return styles[static_cast<size_t>(c)]; }

    static Theme builtin(bool dark);
    // Applies [View] and [Tokens] from a .theme file. Unknown or malformed
    // entries are skipped (their names are appended to `warnings`).
    // A token entry of the form "class=@other" makes `class` look exactly like
    // `other` (resolved in finalize(), so later files may still change `other`).
    void apply(const IniDocument& doc, std::vector<std::string>* warnings = nullptr);
    // Resolves @aliases and inheritance: a class that no theme file set
    // explicitly and that has a parent class (e.g. section_bracket ->
    // punctuation) takes the parent's style. Call once after all apply().
    void finalize();

    // Removes italic from comments (the italic_comments=false option).
    void disableCommentItalic() { styles[static_cast<size_t>(TokenClass::Comment)].flags &= ~kItalic; }

private:
    bool explicit_[kTokenClassCount] = {};
    int alias_[kTokenClassCount] = {};  // 0 = none, else TokenClass index + 1
};

// Class whose style is used when a theme does not set `c` explicitly
// (TokenClass::Count = no parent).
TokenClass parentClass(TokenClass c);

// Resolution order: built-in palette (light/dark) -> <set>/default.theme ->
// <set>/<languageId>.theme. Missing files are silently skipped.
Theme loadTheme(const std::filesystem::path& pluginDir, const std::string& set, const std::string& languageId,
                bool dark, const AnsiConverter& ansi = {});

}  // namespace shl
