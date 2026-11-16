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
    void apply(const IniDocument& doc, std::vector<std::string>* warnings = nullptr);
};

// Resolution order: built-in palette (light/dark) -> <set>/default.theme ->
// <set>/<languageId>.theme. Missing files are silently skipped.
Theme loadTheme(const std::filesystem::path& pluginDir, const std::string& set, const std::string& languageId,
                bool dark, const AnsiConverter& ansi = {});

}  // namespace shl
