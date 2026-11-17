#include "ConfigDialog.h"

#include <commctrl.h>
#include <shellapi.h>
#include <uxtheme.h>

#include "DarkMode.h"
#include "plugin/DebugLog.h"
#include "plugin/WinUtil.h"
#include "resource.h"

namespace shl::ui {

INT_PTR CALLBACK ConfigPage::pageProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* page = reinterpret_cast<ConfigPage*>(GetWindowLongPtrW(dlg, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        page = reinterpret_cast<ConfigPage*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        page->hwnd_ = dlg;
        if (!page->dark_) EnableThemeDialogTexture(dlg, ETDT_ENABLETAB);
        return FALSE;
    }
    if (!page) return FALSE;
    if (page->dark_) {
        INT_PTR r = 0;
        if (dark::handleMessage(dlg, msg, wp, lp, r)) return r;
    }
    try {
        return page->handle(msg, wp, lp);
    } catch (...) {
        return FALSE;
    }
}

ConfigDialog::ConfigDialog(Settings& settings, ConfigContext ctx) : settings_(settings), ctx_(std::move(ctx)) {
    pages_.push_back(createGeneralPage(ctx_));
    pages_.push_back(createLanguagesPage(ctx_));
    pages_.push_back(createThemePage(ctx_));
}

ConfigDialog::~ConfigDialog() = default;

bool ConfigDialog::show(HWND owner) {
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_TAB_CLASSES | ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    DialogBoxParamW(win::g_instance, MAKEINTRESOURCEW(IDD_CONFIG), owner, &ConfigDialog::dlgProc,
                    reinterpret_cast<LPARAM>(this));
    return applied_;
}

void ConfigDialog::initDialog(HWND dlg) {
    dlg_ = dlg;
    tabs_ = GetDlgItem(dlg, IDC_CONFIG_TABS);
    RECT rc;
    GetWindowRect(tabs_, &rc);
    MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT*>(&rc), 2);
    int i = 0;
    for (auto& p : pages_) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(p->title());
        TabCtrl_InsertItem(tabs_, i++, &item);
    }
    RECT display = rc;
    TabCtrl_AdjustRect(tabs_, FALSE, &display);
    for (auto& p : pages_) {
        p->dark_ = ctx_.dark;
        CreateDialogParamW(win::g_instance, MAKEINTRESOURCEW(p->dialogId()), dlg, &ConfigPage::pageProc,
                           reinterpret_cast<LPARAM>(p.get()));
        if (!p->hwnd()) continue;
        SetWindowPos(p->hwnd(), HWND_TOP, display.left, display.top, display.right - display.left,
                     display.bottom - display.top, SWP_NOACTIVATE);
        p->init();
        p->load(settings_.config());
    }
    if (ctx_.dark) {
        dark::applyToDialog(dlg);  // pages are children: covered too
    }
    std::wstring title = L"tcl-syntaxhl settings - " + settings_.path().filename().wstring();
    SetWindowTextW(dlg, title.c_str());
    selectPage(0);
}

void ConfigDialog::selectPage(int index) {
    if (index < 0 || index >= static_cast<int>(pages_.size())) return;
    for (int i = 0; i < static_cast<int>(pages_.size()); ++i)
        if (pages_[i]->hwnd()) ShowWindow(pages_[i]->hwnd(), i == index ? SW_SHOW : SW_HIDE);
    TabCtrl_SetCurSel(tabs_, index);
    current_ = index;
}

bool ConfigDialog::apply() {
    for (int i = 0; i < static_cast<int>(pages_.size()); ++i) {
        std::wstring err;
        if (HWND bad = pages_[i]->validate(err)) {
            selectPage(i);
            MessageBoxW(dlg_, err.c_str(), L"tcl-syntaxhl settings", MB_ICONWARNING | MB_OK);
            SetFocus(bad);
            if (SendMessageW(bad, WM_GETDLGCODE, 0, 0) & DLGC_HASSETSEL) SendMessageW(bad, EM_SETSEL, 0, -1);
            return false;
        }
    }
    for (auto& p : pages_) p->store(settings_.config());
    if (!settings_.save()) {
        std::wstring msg = L"Could not write\n" + settings_.path().wstring() +
                           L"\n\nIs the plugin folder read-only? You can edit the file manually.";
        MessageBoxW(dlg_, msg.c_str(), L"tcl-syntaxhl settings", MB_ICONERROR | MB_OK);
        return false;
    }
    dbg::log("settings saved to %s", win::narrow(settings_.path().wstring()).c_str());
    for (int i = 0; i < static_cast<int>(pages_.size()); ++i) {
        std::wstring err;
        if (!pages_[i]->saveExternal(err)) {
            selectPage(i);
            MessageBoxW(dlg_, err.c_str(), L"tcl-syntaxhl settings", MB_ICONERROR | MB_OK);
            return false;
        }
    }
    applied_ = true;
    if (onApplied) onApplied();
    // Pages show the values as saved (e.g. normalised sizes).
    for (auto& p : pages_) p->load(settings_.config());
    return true;
}

INT_PTR CALLBACK ConfigDialog::dlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<ConfigDialog*>(GetWindowLongPtrW(dlg, DWLP_USER));
    if (self && self->ctx_.dark) {
        INT_PTR r = 0;
        if (dark::handleMessage(dlg, msg, wp, lp, r)) return r;
    }
    try {
        switch (msg) {
            case WM_INITDIALOG:
                self = reinterpret_cast<ConfigDialog*>(lp);
                SetWindowLongPtrW(dlg, DWLP_USER, lp);
                self->initDialog(dlg);
                return TRUE;
            case WM_NOTIFY: {
                auto* nm = reinterpret_cast<NMHDR*>(lp);
                if (self && nm->idFrom == IDC_CONFIG_TABS && nm->code == TCN_SELCHANGE)
                    self->selectPage(TabCtrl_GetCurSel(self->tabs_));
                break;
            }
            case WM_COMMAND:
                if (!self) break;
                switch (LOWORD(wp)) {
                    case IDOK:
                        if (self->apply()) EndDialog(dlg, IDOK);
                        return TRUE;
                    case IDCANCEL:
                        EndDialog(dlg, IDCANCEL);
                        return TRUE;
                    case IDC_CONFIG_APPLY:
                        self->apply();
                        return TRUE;
                    case IDC_CONFIG_OPENINI:
                        ShellExecuteW(dlg, L"open", self->settings_.path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                        return TRUE;
                }
                break;
        }
    } catch (...) {
    }
    return FALSE;
}

}  // namespace shl::ui
