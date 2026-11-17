// Turns tokens + theme into RichEdit content/formatting.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "core/Theme.h"
#include "core/Token.h"

namespace shl::view {

struct FontSpec {
    std::wstring face = L"Consolas";
    int pointSize = 10;
    int tabTwips = 720;
};

// Average character width of the font in twips (for tab stops).
int measureCharTwips(HWND hwnd, const FontSpec& font);

// Builds a complete RTF document. `tokens` may be empty (plain text).
// `text` uses '\r' line terminators (see normalizeLineEndings).
std::string buildRtf(std::u16string_view text, const std::vector<Token>& tokens, const Theme& theme,
                     const FontSpec& font);

// Replaces the control's content with RTF.
bool streamRtf(HWND edit, const std::string& rtf);

// Replaces content with plain text in the default font/color (fast path for
// big files: no RTF parsing). Tab stops are set for the whole document.
void setPlainText(HWND edit, std::u16string_view text, const Theme& theme, const FontSpec& font);

// Applies token colors to existing text without moving the view/selection.
void applyTokens(HWND edit, const std::vector<Token>& tokens, const Theme& theme);

}  // namespace shl::view
