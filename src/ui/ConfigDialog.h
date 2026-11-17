// Settings window (specs §4.3): a tab control hosting one child dialog per page.
//
// OK / Apply: every page validates, then stores into Settings::config(),
// Settings::save() writes only the changed values, then onApplied runs (the
// Lister window reloads its file).
#pragma once

#include <windows.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/Settings.h"

namespace shl::ui {

class ConfigPage {
public:
    virtual ~ConfigPage() = default;
    virtual const wchar_t* title() const = 0;
    virtual int dialogId() const = 0;
    // Called once after the child dialog exists (fill combos etc.).
    virtual void init() {}
    virtual void load(const Config& cfg) = 0;  // settings -> controls
    // Returns the control to focus and sets `error`, or nullptr if valid.
    virtual HWND validate(std::wstring& error) { return nullptr; }
    virtual void store(Config& cfg) = 0;  // controls -> settings
    // WM_COMMAND / WM_NOTIFY etc. of the page; return TRUE if handled.
    virtual INT_PTR handle(UINT msg, WPARAM wp, LPARAM lp) { return FALSE; }
    // Saves data kept outside the plugin INI (e.g. a theme file) on OK/Apply,
    // after the INI was written. Return false and set `error` on failure.
    virtual bool saveExternal(std::wstring& error) { return true; }

    HWND hwnd() const { return hwnd_; }
    HWND item(int id) const { return GetDlgItem(hwnd_, id); }

    static INT_PTR CALLBACK pageProc(HWND, UINT, WPARAM, LPARAM);

protected:
    HWND hwnd_ = nullptr;
    bool dark_ = false;  // dialog drawn in dark mode
    friend class ConfigDialog;
};

struct ConfigContext {
    std::filesystem::path pluginDir;
    bool dark = false;          // Total Commander is in dark mode
    std::string themeSet;       // active theme set of the current file
    std::string languageId;     // language of the current file ("" if none)
};

class ConfigDialog {
public:
    ConfigDialog(Settings& settings, ConfigContext ctx);
    ~ConfigDialog();

    // Modal. Returns true if settings were applied at least once.
    bool show(HWND owner);

    std::function<void()> onApplied;

private:
    static INT_PTR CALLBACK dlgProc(HWND, UINT, WPARAM, LPARAM);
    void initDialog(HWND dlg);
    void selectPage(int index);
    bool apply();

    Settings& settings_;
    ConfigContext ctx_;
    std::vector<std::unique_ptr<ConfigPage>> pages_;
    HWND dlg_ = nullptr;
    HWND tabs_ = nullptr;
    int current_ = -1;
    bool applied_ = false;
};

// Page factories (ConfigPages.cpp).
std::unique_ptr<ConfigPage> createGeneralPage(const ConfigContext& ctx);
std::unique_ptr<ConfigPage> createLanguagesPage(const ConfigContext& ctx);
std::unique_ptr<ConfigPage> createThemePage(const ConfigContext& ctx);

}  // namespace shl::ui
