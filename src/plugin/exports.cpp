// Total Commander Lister plugin entry points. No exception may cross this ABI.
#include <windows.h>

#include <cstring>

#include "WinUtil.h"
#include "core/Config.h"
#include "core/IniDocument.h"
#include "core/LexerFactory.h"
#include "listplug.h"
#include "view/ListerWindow.h"

using shl::view::ListerWindow;

BOOL APIENTRY DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        shl::win::g_instance = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}

extern "C" {

void __stdcall ListSetDefaultParams(ListDefaultParamStruct*) {
    // The plugin keeps its own INI next to the DLL (see specs §3); nothing to do.
}

void __stdcall ListGetDetectString(char* detect, int maxlen) {
    if (!detect || maxlen <= 0) return;
    detect[0] = '\0';
    try {
        shl::IniDocument doc;
        auto cfg = shl::IniDocument::loadFile(shl::win::iniPath(), doc, shl::win::ansiConverter())
                       ? shl::Config::fromIni(doc)
                       : shl::Config::defaults();
        auto dir = shl::win::pluginDir();
        auto s = cfg.buildDetectString(
            [&](const shl::LanguageDef& l) { return shl::isLanguageAvailable(l, dir); },
            static_cast<size_t>(maxlen));
        strncpy_s(detect, static_cast<size_t>(maxlen), s.c_str(), _TRUNCATE);
    } catch (...) {
        detect[0] = '\0';
    }
}

HWND __stdcall ListLoadW(HWND parent, WCHAR* file, int flags) {
    if (!file) return nullptr;
    try {
        auto* w = ListerWindow::create(parent, file, flags);
        return w ? w->hwnd() : nullptr;
    } catch (...) {
        return nullptr;
    }
}

HWND __stdcall ListLoad(HWND parent, char* file, int flags) {
    if (!file) return nullptr;
    try {
        auto w = shl::win::ansiToWide(file);
        return ListLoadW(parent, w.data(), flags);
    } catch (...) {
        return nullptr;
    }
}

int __stdcall ListLoadNextW(HWND, HWND pluginWin, WCHAR* file, int flags) {
    if (!file) return LISTPLUGIN_ERROR;
    try {
        auto* w = ListerWindow::fromHwnd(pluginWin);
        return w && w->load(file, flags) ? LISTPLUGIN_OK : LISTPLUGIN_ERROR;
    } catch (...) {
        return LISTPLUGIN_ERROR;
    }
}

int __stdcall ListLoadNext(HWND parent, HWND pluginWin, char* file, int flags) {
    if (!file) return LISTPLUGIN_ERROR;
    try {
        auto w = shl::win::ansiToWide(file);
        return ListLoadNextW(parent, pluginWin, w.data(), flags);
    } catch (...) {
        return LISTPLUGIN_ERROR;
    }
}

void __stdcall ListCloseWindow(HWND listWin) {
    if (listWin && IsWindow(listWin)) DestroyWindow(listWin);
}

int __stdcall ListSearchTextW(HWND listWin, WCHAR* text, int flags) {
    if (!text) return LISTPLUGIN_ERROR;
    try {
        auto* w = ListerWindow::fromHwnd(listWin);
        return w ? w->search(text, flags) : LISTPLUGIN_ERROR;
    } catch (...) {
        return LISTPLUGIN_ERROR;
    }
}

int __stdcall ListSearchText(HWND listWin, char* text, int flags) {
    if (!text) return LISTPLUGIN_ERROR;
    try {
        auto w = shl::win::ansiToWide(text);
        return ListSearchTextW(listWin, w.data(), flags);
    } catch (...) {
        return LISTPLUGIN_ERROR;
    }
}

int __stdcall ListSendCommand(HWND listWin, int command, int parameter) {
    try {
        auto* w = ListerWindow::fromHwnd(listWin);
        return w ? w->command(command, parameter) : LISTPLUGIN_ERROR;
    } catch (...) {
        return LISTPLUGIN_ERROR;
    }
}

}  // extern "C"
