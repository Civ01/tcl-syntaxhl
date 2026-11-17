#include "DebugLog.h"

#include <psapi.h>

#include <cstdarg>
#include <cstdio>
#include <string>

#include "WinUtil.h"

namespace shl::dbg {

namespace {

bool g_enabled = false;
bool g_hooksInstalled = false;
std::wstring g_path;

using MessageBeep_t = BOOL(WINAPI*)(UINT);
using Beep_t = BOOL(WINAPI*)(DWORD, DWORD);
using PlaySoundW_t = BOOL(WINAPI*)(LPCWSTR, HMODULE, DWORD);
using PlaySoundA_t = BOOL(WINAPI*)(LPCSTR, HMODULE, DWORD);
MessageBeep_t g_origMessageBeep = nullptr;
Beep_t g_origBeep = nullptr;
PlaySoundW_t g_origPlaySoundW = nullptr;
PlaySoundA_t g_origPlaySoundA = nullptr;

std::wstring logPath() {
    auto p = win::pluginDir() / L"tcl-syntaxhl.log";
    HANDLE h = CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        return p.wstring();
    }
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    return std::wstring(tmp) + L"tcl-syntaxhl.log";
}

void writeLine(const std::string& line) {
    HANDLE h = CreateFileW(g_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(h);
}

std::string moduleOf(void* addr) {
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(addr), &m) ||
        !m) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%p", addr);
        return buf;
    }
    wchar_t name[MAX_PATH] = {};
    GetModuleFileNameW(m, name, MAX_PATH);
    std::wstring n = name;
    auto slash = n.find_last_of(L"\\/");
    if (slash != std::wstring::npos) n = n.substr(slash + 1);
    char buf[64];
    std::snprintf(buf, sizeof buf, "+0x%llx",
                  static_cast<unsigned long long>(static_cast<char*>(addr) - reinterpret_cast<char*>(m)));
    return win::narrow(n) + buf;
}

void logBeep(const char* what, unsigned long arg) {
    if (!g_enabled) return;
    void* frames[12] = {};
    USHORT n = CaptureStackBackTrace(1, 12, frames, nullptr);
    std::string stack;
    for (USHORT i = 0; i < n; ++i) stack += (i ? " <- " : "") + moduleOf(frames[i]);
    log("*** BEEP %s(0x%lx) caller stack: %s", what, arg, stack.c_str());
}

BOOL WINAPI hookMessageBeep(UINT type) {
    logBeep("MessageBeep", type);
    return g_origMessageBeep ? g_origMessageBeep(type) : TRUE;
}
BOOL WINAPI hookBeep(DWORD freq, DWORD dur) {
    logBeep("Beep", freq);
    return g_origBeep ? g_origBeep(freq, dur) : TRUE;
}
BOOL WINAPI hookPlaySoundW(LPCWSTR s, HMODULE m, DWORD f) {
    logBeep("PlaySoundW", f);
    return g_origPlaySoundW ? g_origPlaySoundW(s, m, f) : TRUE;
}
BOOL WINAPI hookPlaySoundA(LPCSTR s, HMODULE m, DWORD f) {
    logBeep("PlaySoundA", f);
    return g_origPlaySoundA ? g_origPlaySoundA(s, m, f) : TRUE;
}

// Replaces import-table entries that point to `target` with `hook` in `mod`.
int patchModule(HMODULE mod, void* target, void* hook) {
    if (!target) return 0;
    auto* base = reinterpret_cast<BYTE*>(mod);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || !dir.Size) return 0;
    int patched = 0;
    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
        for (auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk); thunk->u1.Function; ++thunk) {
            if (reinterpret_cast<void*>(thunk->u1.Function) != target) continue;
            DWORD old = 0;
            if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) {
                thunk->u1.Function = reinterpret_cast<ULONG_PTR>(hook);
                VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
                ++patched;
            }
        }
    }
    return patched;
}

void resolveOriginals() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE winmm = GetModuleHandleW(L"winmm.dll");
    g_origMessageBeep = user32 ? reinterpret_cast<MessageBeep_t>(
                                     reinterpret_cast<void*>(GetProcAddress(user32, "MessageBeep")))
                               : nullptr;
    g_origBeep = kernel32 ? reinterpret_cast<Beep_t>(reinterpret_cast<void*>(GetProcAddress(kernel32, "Beep")))
                          : nullptr;
    if (winmm) {
        g_origPlaySoundW = reinterpret_cast<PlaySoundW_t>(reinterpret_cast<void*>(GetProcAddress(winmm, "PlaySoundW")));
        g_origPlaySoundA = reinterpret_cast<PlaySoundA_t>(reinterpret_cast<void*>(GetProcAddress(winmm, "PlaySoundA")));
    }
    // Keep this DLL loaded: patched import tables point into it.
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&resolveOriginals), &self);
}

void installHooks() {
    if (!g_hooksInstalled) resolveOriginals();

    HMODULE mods[1024];
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &needed)) return;
    int total = 0;
    for (DWORD i = 0; i < needed / sizeof(HMODULE) && i < 1024; ++i) {
        if (mods[i] == win::g_instance) continue;
        total += patchModule(mods[i], reinterpret_cast<void*>(g_origMessageBeep), reinterpret_cast<void*>(&hookMessageBeep));
        total += patchModule(mods[i], reinterpret_cast<void*>(g_origBeep), reinterpret_cast<void*>(&hookBeep));
        total += patchModule(mods[i], reinterpret_cast<void*>(g_origPlaySoundW), reinterpret_cast<void*>(&hookPlaySoundW));
        total += patchModule(mods[i], reinterpret_cast<void*>(g_origPlaySoundA), reinterpret_cast<void*>(&hookPlaySoundA));
    }
    log("beep hooks installed in %d import slots across %lu modules", total,
        static_cast<unsigned long>(needed / sizeof(HMODULE)));
}

std::string fileVersion(const wchar_t* module) {
    HMODULE m = GetModuleHandleW(module);
    if (!m) return "(not loaded)";
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(m, path, MAX_PATH);
    DWORD dummy = 0, size = GetFileVersionInfoSizeW(path, &dummy);
    std::string out = win::narrow(path);
    if (!size) return out;
    std::string data(size, '\0');
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (GetFileVersionInfoW(path, 0, size, data.data()) &&
        VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fi), &len) && fi) {
        char buf[64];
        std::snprintf(buf, sizeof buf, " %u.%u.%u.%u", HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS),
                      HIWORD(fi->dwFileVersionLS), LOWORD(fi->dwFileVersionLS));
        out += buf;
    }
    return out;
}

}  // namespace

void configure(bool enabled) {
    if (enabled && !g_enabled) {
        g_path = logPath();
        g_enabled = true;
        log("==== debug log started; process %s", fileVersion(nullptr).c_str());
        log("RichEdit: %s", fileVersion(L"msftedit.dll").c_str());
    }
    if (enabled) {
        // Re-scan on every load: modules loaded since then get hooked too
        // (already patched slots no longer match and are skipped).
        installHooks();
        g_hooksInstalled = true;
    } else if (!enabled && g_enabled) {
        log("==== debug log stopped");
        g_enabled = false;
    }
}

bool on() { return g_enabled; }

void log(const char* fmt, ...) {
    if (!g_enabled) return;
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    SYSTEMTIME st;
    GetLocalTime(&st);
    char head[64];
    std::snprintf(head, sizeof head, "%02u:%02u:%02u.%03u [%lu] ", st.wHour, st.wMinute, st.wSecond,
                  st.wMilliseconds, static_cast<unsigned long>(GetCurrentThreadId()));
    writeLine(std::string(head) + buf + "\r\n");
}

const char* messageName(UINT msg) {
    switch (msg) {
        case WM_KEYDOWN: return "WM_KEYDOWN";
        case WM_KEYUP: return "WM_KEYUP";
        case WM_CHAR: return "WM_CHAR";
        case WM_DEADCHAR: return "WM_DEADCHAR";
        case WM_SYSKEYDOWN: return "WM_SYSKEYDOWN";
        case WM_SYSKEYUP: return "WM_SYSKEYUP";
        case WM_SYSCHAR: return "WM_SYSCHAR";
        case WM_VSCROLL: return "WM_VSCROLL";
        case WM_HSCROLL: return "WM_HSCROLL";
        case WM_MOUSEWHEEL: return "WM_MOUSEWHEEL";
        case WM_GETDLGCODE: return "WM_GETDLGCODE";
        case WM_SETFOCUS: return "WM_SETFOCUS";
        case WM_KILLFOCUS: return "WM_KILLFOCUS";
        case WM_COMMAND: return "WM_COMMAND";
        case WM_MENUCHAR: return "WM_MENUCHAR";
        case WM_CONTEXTMENU: return "WM_CONTEXTMENU";
        case WM_APPCOMMAND: return "WM_APPCOMMAND";
    }
    return nullptr;
}

}  // namespace shl::dbg
