#include "ConfigDialog.h"

#include <commctrl.h>

#include "plugin/WinUtil.h"
#include "resource.h"

namespace shl::ui {

namespace {
// Placeholder pages: titles only. Real controls are added later.
class PlaceholderPage final : public ConfigPage {
public:
    explicit PlaceholderPage(const wchar_t* t) : title_(t) {}
    const wchar_t* title() const override { return title_; }
    void load(const Config&) override {}
    void store(Config&) const override {}

private:
    const wchar_t* title_;
};
}  // namespace

ConfigDialog::ConfigDialog(Settings& settings) : settings_(settings) {
    pages_.push_back(std::make_unique<PlaceholderPage>(L"General"));
    pages_.push_back(std::make_unique<PlaceholderPage>(L"Languages"));
    pages_.push_back(std::make_unique<PlaceholderPage>(L"Theme"));
}

bool ConfigDialog::show(HWND owner) {
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_TAB_CLASSES};
    InitCommonControlsEx(&icc);
    INT_PTR r = DialogBoxParamW(win::g_instance, MAKEINTRESOURCEW(IDD_CONFIG), owner, &ConfigDialog::dlgProc,
                                reinterpret_cast<LPARAM>(this));
    if (r != IDOK) return false;
    for (const auto& p : pages_) p->store(settings_.config());
    // Nothing is editable yet, so there is nothing to save.
    return false;
}

INT_PTR CALLBACK ConfigDialog::dlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<ConfigDialog*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
        case WM_INITDIALOG: {
            self = reinterpret_cast<ConfigDialog*>(lp);
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            HWND tabs = GetDlgItem(dlg, IDC_CONFIG_TABS);
            int i = 0;
            for (const auto& p : self->pages_) {
                p->load(self->settings_.config());
                TCITEMW item{};
                item.mask = TCIF_TEXT;
                item.pszText = const_cast<wchar_t*>(p->title());
                TabCtrl_InsertItem(tabs, i++, &item);
            }
            SetDlgItemTextW(dlg, IDC_CONFIG_PATH, self->settings_.path().wstring().c_str());
            // Keep the placeholder text above the tab control.
            SetWindowPos(tabs, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, LOWORD(wp));
                return TRUE;
            }
            break;
    }
    return FALSE;
}

}  // namespace shl::ui
