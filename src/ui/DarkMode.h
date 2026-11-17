// Dark-mode support for the plugin's dialogs (settings, go to line, ...).
//
// Used when Total Commander runs in dark mode (lcp_darkmode). Combines the
// documented parts (DWM dark title bar, WM_CTLCOLOR* colors) with the
// system's dark control themes ("DarkMode_Explorer", "DarkMode_CFD") and the
// uxtheme "allow dark mode" entry points used by Explorer/Notepad (looked up
// by ordinal and skipped if unavailable). Controls without a dark theme
// (check boxes, group boxes, tab control) are drawn with dark colors instead.
#pragma once

#include <windows.h>

namespace shl::ui::dark {

struct Palette {
    COLORREF window = RGB(32, 32, 32);
    COLORREF control = RGB(43, 43, 43);
    COLORREF text = RGB(230, 230, 230);
    COLORREF dimText = RGB(150, 150, 150);
    COLORREF border = RGB(90, 90, 90);
    COLORREF selected = RGB(60, 60, 60);
};
const Palette& palette();

// Enables dark popup menus / scrollbars for the process (once).
void initProcess();
// Dark title bar + dark themes for every child control of `dlg`.
void applyToDialog(HWND dlg);
// Dark theme for one control (also for controls created later).
void applyToControl(HWND ctl);
// Allows dark context menus etc. for a plain window.
void allowForWindow(HWND hwnd, bool dark);

// Handles WM_CTLCOLOR* / WM_ERASEBKGND for a dark dialog. Returns true and
// sets `result` if the message was handled.
bool handleMessage(HWND dlg, UINT msg, WPARAM wp, LPARAM lp, INT_PTR& result);

}  // namespace shl::ui::dark
