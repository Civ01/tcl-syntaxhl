#include "DarkMode.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <string>

namespace shl::ui::dark {

namespace {

enum PreferredAppMode { Default, AllowDark, ForceDark, ForceLight };
using SetPreferredAppMode_t = PreferredAppMode(WINAPI*)(PreferredAppMode);
using AllowDarkModeForWindow_t = bool(WINAPI*)(HWND, bool);
using FlushMenuThemes_t = void(WINAPI*)();

HMODULE uxtheme() {
    static HMODULE m = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return m;
}

// Undocumented uxtheme exports (Windows 10 1809+), by ordinal.
template <class F>
F ordinal(int n) {
    HMODULE m = uxtheme();
    return m ? reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, MAKEINTRESOURCEA(n)))) : nullptr;
}

bool windowsBuildAtLeast(DWORD build) {
    using RtlGetVersion_t = LONG(WINAPI*)(OSVERSIONINFOW*);
    static DWORD current = [] {
        OSVERSIONINFOW vi{sizeof vi};
        if (auto f = reinterpret_cast<RtlGetVersion_t>(
                reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))))
            f(&vi);
        return vi.dwBuildNumber;
    }();
    return current >= build;
}

HBRUSH brush(COLORREF c) {
    // Small cache: the palette has few colors.
    static COLORREF keys[8];
    static HBRUSH values[8];
    static int count = 0;
    for (int i = 0; i < count; ++i)
        if (keys[i] == c) return values[i];
    HBRUSH b = CreateSolidBrush(c);
    if (count < 8) {
        keys[count] = c;
        values[count++] = b;
    }
    return b;
}

std::wstring className(HWND h) {
    wchar_t buf[64] = {};
    GetClassNameW(h, buf, 63);
    return buf;
}

// Owner-painted tab strip (the tab control has no dark theme).
LRESULT CALLBACK tabProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        const Palette& p = palette();
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        FillRect(dc, &rc, brush(p.window));
        int count = TabCtrl_GetItemCount(h), sel = TabCtrl_GetCurSel(h);
        RECT body = rc;
        TabCtrl_AdjustRect(h, FALSE, &body);
        InflateRect(&body, 2, 2);
        HPEN pen = CreatePen(PS_SOLID, 1, p.border);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, body.left, body.top, body.right, body.bottom);
        HGDIOBJ oldFont = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0)));
        SetBkMode(dc, TRANSPARENT);
        for (int i = 0; i < count; ++i) {
            RECT r;
            TabCtrl_GetItemRect(h, i, &r);
            if (i == sel) r.bottom += 1;
            FillRect(dc, &r, brush(i == sel ? p.selected : p.control));
            Rectangle(dc, r.left, r.top, r.right, r.bottom);
            wchar_t text[64] = {};
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = text;
            item.cchTextMax = 63;
            TabCtrl_GetItem(h, i, &item);
            SetTextColor(dc, i == sel ? p.text : p.dimText);
            DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(dc, oldFont);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        EndPaint(h, &ps);
        return 0;
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, tabProc, 1);
    return DefSubclassProc(h, msg, wp, lp);
}

}  // namespace

const Palette& palette() {
    static const Palette p;
    return p;
}

void initProcess() {
    static bool done = false;
    if (done) return;
    done = true;
    if (!windowsBuildAtLeast(18362)) return;  // 1903+: SetPreferredAppMode
    if (auto setMode = ordinal<SetPreferredAppMode_t>(135)) setMode(AllowDark);
    if (auto flush = ordinal<FlushMenuThemes_t>(136)) flush();
}

void allowForWindow(HWND hwnd, bool dark) {
    initProcess();
    if (!windowsBuildAtLeast(17763)) return;
    if (auto allow = ordinal<AllowDarkModeForWindow_t>(133)) allow(hwnd, dark);
}

void applyToControl(HWND ctl) {
    allowForWindow(ctl, true);
    std::wstring cls = className(ctl);
    if (_wcsicmp(cls.c_str(), L"Button") == 0) {
        LONG type = GetWindowLongW(ctl, GWL_STYLE) & BS_TYPEMASK;
        bool push = type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON;
        // Themed check boxes / group boxes ignore WM_CTLCOLORSTATIC text colors.
        if (push) SetWindowTheme(ctl, L"DarkMode_Explorer", nullptr);
        else SetWindowTheme(ctl, L"", L"");
    } else if (_wcsicmp(cls.c_str(), L"Edit") == 0 || _wcsicmp(cls.c_str(), L"ComboBox") == 0) {
        SetWindowTheme(ctl, L"DarkMode_CFD", nullptr);
    } else if (_wcsicmp(cls.c_str(), L"ListBox") == 0 || _wcsicmp(cls.c_str(), L"msctls_updown32") == 0 ||
               _wcsicmp(cls.c_str(), L"SysListView32") == 0) {
        SetWindowTheme(ctl, L"DarkMode_Explorer", nullptr);
    } else if (_wcsicmp(cls.c_str(), WC_TABCONTROLW) == 0) {
        SetWindowSubclass(ctl, tabProc, 1, 0);
    }
    SendMessageW(ctl, WM_THEMECHANGED, 0, 0);
}

void applyToDialog(HWND dlg) {
    allowForWindow(dlg, true);
    BOOL on = TRUE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE: 20 (Win10 2004+/Win11), 19 on older builds.
    if (FAILED(DwmSetWindowAttribute(dlg, 20, &on, sizeof on))) DwmSetWindowAttribute(dlg, 19, &on, sizeof on);
    EnumChildWindows(
        dlg, [](HWND h, LPARAM) -> BOOL {
            applyToControl(h);
            return TRUE;
        },
        0);
}

bool handleMessage(HWND, UINT msg, WPARAM wp, LPARAM, INT_PTR& result) {
    const Palette& p = palette();
    HDC dc = reinterpret_cast<HDC>(wp);
    switch (msg) {
        case WM_CTLCOLORDLG:
            result = reinterpret_cast<INT_PTR>(brush(p.window));
            return true;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            SetTextColor(dc, p.text);
            SetBkColor(dc, p.window);
            result = reinterpret_cast<INT_PTR>(brush(p.window));
            return true;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            SetTextColor(dc, p.text);
            SetBkColor(dc, p.control);
            result = reinterpret_cast<INT_PTR>(brush(p.control));
            return true;
    }
    return false;
}

}  // namespace shl::ui::dark
