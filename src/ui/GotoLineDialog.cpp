#include "GotoLineDialog.h"

#include <string>

#include "DarkMode.h"
#include "plugin/WinUtil.h"
#include "resource.h"

namespace shl::ui {

std::optional<GotoTarget> parseGotoInput(std::wstring_view s, size_t maxLine) {
    auto parseNum = [](std::wstring_view v, size_t& out) {
        while (!v.empty() && iswspace(v.front())) v.remove_prefix(1);
        while (!v.empty() && iswspace(v.back())) v.remove_suffix(1);
        if (v.empty() || v.size() > 12) return false;
        size_t n = 0;
        for (wchar_t c : v) {
            if (c < L'0' || c > L'9') return false;
            n = n * 10 + static_cast<size_t>(c - L'0');
        }
        out = n;
        return true;
    };
    GotoTarget t;
    auto sep = s.find_first_of(L":,");
    if (!parseNum(s.substr(0, sep), t.line)) return std::nullopt;
    if (sep != std::wstring_view::npos && !parseNum(s.substr(sep + 1), t.column)) return std::nullopt;
    if (t.line < 1 || t.line > maxLine) return std::nullopt;
    return t;
}

namespace {
struct DlgData {
    size_t current;
    size_t count;
    bool dark;
    GotoTarget result;
};

INT_PTR CALLBACK dlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<DlgData*>(GetWindowLongPtrW(dlg, DWLP_USER));
    if (d && d->dark) {
        INT_PTR r = 0;
        if (dark::handleMessage(dlg, msg, wp, lp, r)) return r;
    }
    switch (msg) {
        case WM_INITDIALOG: {
            d = reinterpret_cast<DlgData*>(lp);
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            if (d->dark) dark::applyToDialog(dlg);
            std::wstring label = L"Line number (1 - " + std::to_wstring(d->count) + L"), or line:column:";
            SetDlgItemTextW(dlg, IDC_GOTO_LABEL, label.c_str());
            SetDlgItemTextW(dlg, IDC_GOTO_EDIT, std::to_wstring(d->current).c_str());
            HWND edit = GetDlgItem(dlg, IDC_GOTO_EDIT);
            SendMessageW(edit, EM_SETSEL, 0, -1);
            SetFocus(edit);
            return FALSE;  // focus set manually
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                wchar_t buf[64] = {};
                GetDlgItemTextW(dlg, IDC_GOTO_EDIT, buf, 63);
                auto t = parseGotoInput(buf, d->count);
                if (!t) {
                    MessageBeep(MB_ICONWARNING);
                    HWND edit = GetDlgItem(dlg, IDC_GOTO_EDIT);
                    SendMessageW(edit, EM_SETSEL, 0, -1);
                    SetFocus(edit);
                    return TRUE;
                }
                d->result = *t;
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}
}  // namespace

std::optional<GotoTarget> showGotoLineDialog(HWND owner, size_t currentLine, size_t lineCount, bool dark) {
    DlgData d{currentLine, lineCount ? lineCount : 1, dark, {}};
    INT_PTR r = DialogBoxParamW(win::g_instance, MAKEINTRESOURCEW(IDD_GOTO), owner, dlgProc,
                                reinterpret_cast<LPARAM>(&d));
    if (r != IDOK) return std::nullopt;
    return d.result;
}

}  // namespace shl::ui
