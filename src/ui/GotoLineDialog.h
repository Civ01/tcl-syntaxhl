#pragma once

#include <windows.h>

#include <cstddef>
#include <optional>
#include <string_view>

namespace shl::ui {

struct GotoTarget {
    size_t line = 1;    // 1-based
    size_t column = 0;  // 1-based; 0 = not given
};

// Parses "line" or "line:col" (also "line,col"); nullopt if invalid.
std::optional<GotoTarget> parseGotoInput(std::wstring_view s, size_t maxLine);

// Modal "Go to line" dialog. Returns nullopt when cancelled.
std::optional<GotoTarget> showGotoLineDialog(HWND owner, size_t currentLine, size_t lineCount, bool dark = false);

}  // namespace shl::ui
