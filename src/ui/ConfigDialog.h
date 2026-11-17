// Runtime settings window - SKELETON ONLY (v1).
//
// Structure for the future implementation:
//   - pages (tabs) bound to a shl::Settings instance; each page reads values
//     from settings.config() in load() and writes them back in store();
//   - OK -> store() all pages -> settings.save() (preserves INI comments).
// Reachable from the context menu only when enable_config_dialog=true.
#pragma once

#include <windows.h>

#include <memory>
#include <vector>

#include "core/Settings.h"

namespace shl::ui {

class ConfigPage {
public:
    virtual ~ConfigPage() = default;
    virtual const wchar_t* title() const = 0;
    virtual void load(const Config& cfg) = 0;   // settings -> controls
    virtual void store(Config& cfg) const = 0;  // controls -> settings
};

class ConfigDialog {
public:
    explicit ConfigDialog(Settings& settings);
    // Returns true when the user pressed OK and settings were saved.
    bool show(HWND owner);

private:
    static INT_PTR CALLBACK dlgProc(HWND, UINT, WPARAM, LPARAM);
    Settings& settings_;
    std::vector<std::unique_ptr<ConfigPage>> pages_;
};

}  // namespace shl::ui
