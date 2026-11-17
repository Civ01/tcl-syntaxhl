// End-to-end smoke test for tcl-syntaxhl.wlx64.
//
// Loads the plugin DLL like Total Commander does (host window of class
// "TLister"), opens sample files and checks the RichEdit content and colors
// against what the core library computes for the same file/theme.
//
//   plugin_smoke <path-to-tcl-syntaxhl.wlx64> <samples-dir>
//
// The plugin directory must contain the dist/ files (ini, themes, rules); the
// test temporarily rewrites the plugin INI for some cases and restores it.
#include <windows.h>
#include <richedit.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "core/Config.h"
#include "core/IniDocument.h"
#include "core/LexerFactory.h"
#include "core/TextDecoder.h"
#include "core/TextUtil.h"
#include "core/Theme.h"
#include "plugin/listplug.h"
#include "ui/resource.h"

namespace fs = std::filesystem;
using namespace shl;

namespace {

template <class F>
F proc(HMODULE m, const char* name) {
    return reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, name)));
}

using LoadW_t = HWND(__stdcall*)(HWND, WCHAR*, int);
using LoadNextW_t = int(__stdcall*)(HWND, HWND, WCHAR*, int);
using Close_t = void(__stdcall*)(HWND);
using Detect_t = void(__stdcall*)(char*, int);
using SearchW_t = int(__stdcall*)(HWND, WCHAR*, int);
using Command_t = int(__stdcall*)(HWND, int, int);

struct Api {
    LoadW_t load = nullptr;
    LoadNextW_t loadNext = nullptr;
    Close_t close = nullptr;
    Detect_t detect = nullptr;
    SearchW_t search = nullptr;
    Command_t command = nullptr;
} api;

int g_failures = 0, g_checks = 0;
int g_lastPercent = -1;
HWND g_host = nullptr;
fs::path g_pluginDir, g_samples, g_tmp;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void section(const char* name) { std::printf("[%s]\n", name); }

void pump(int ms) {
    DWORD end = GetTickCount() + static_cast<DWORD>(ms);
    MSG msg;
    while (GetTickCount() < end) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(5);
    }
}

// Simulates Lister subclassing the plugin window to catch hotkeys.
std::vector<std::pair<UINT, WPARAM>> g_pluginKeys;
WNDPROC g_pluginOrigProc = nullptr;
LRESULT CALLBACK listerSubclass(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN || m == WM_CHAR) g_pluginKeys.emplace_back(m, w);
    return CallWindowProcW(g_pluginOrigProc, h, m, w, l);
}

// ---- settings window automation
HWND findSettingsDialog() {
    HWND found = nullptr;
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND h, LPARAM p) -> BOOL {
            wchar_t cls[32] = {}, title[128] = {};
            GetClassNameW(h, cls, 32);
            GetWindowTextW(h, title, 128);
            if (wcscmp(cls, L"#32770") == 0 && wcsncmp(title, L"tcl-syntaxhl settings", 21) == 0) {
                *reinterpret_cast<HWND*>(p) = h;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&found));
    return found;
}

HWND findDescendant(HWND root, int id) {
    struct S { int id; HWND hit; } st{id, nullptr};
    EnumChildWindows(
        root,
        [](HWND h, LPARAM p) -> BOOL {
            auto* s = reinterpret_cast<S*>(p);
            if (GetDlgCtrlID(h) == s->id) { s->hit = h; return FALSE; }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&st));
    return st.hit;
}

std::function<void(HWND)> g_dialogScript;
bool g_dialogSeen = false;

void CALLBACK dialogTimer(HWND, UINT, UINT_PTR id, DWORD) {
    HWND dlg = findSettingsDialog();
    if (!dlg) return;
    KillTimer(nullptr, id);
    g_dialogSeen = true;
    if (g_dialogScript) g_dialogScript(dlg);
}

LRESULT CALLBACK hostProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_COMMAND && HIWORD(w) == itm_percent) {
        g_lastPercent = LOWORD(w);
        return 0;
    }
    if (m == WM_SIZE) {
        // Like Lister: keep the plugin window filling the client area.
        HWND child = GetWindow(h, GW_CHILD);
        if (child) MoveWindow(child, 0, 0, LOWORD(l), HIWORD(l), TRUE);
    }
    return DefWindowProcW(h, m, w, l);
}

HWND findChild(HWND parent, const wchar_t* cls) { return FindWindowExW(parent, nullptr, cls, nullptr); }

std::u16string editText(HWND edit) {
    GETTEXTLENGTHEX gl{GTL_NUMCHARS | GTL_PRECISE, 1200};
    LRESULT len = SendMessageW(edit, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&gl), 0);
    std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
    GETTEXTEX gt{static_cast<DWORD>((len + 1) * sizeof(wchar_t)), GT_DEFAULT, 1200, nullptr, nullptr};
    SendMessageW(edit, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&gt), reinterpret_cast<LPARAM>(buf.data()));
    buf.resize(wcslen(buf.c_str()));
    return std::u16string(buf.begin(), buf.end());
}

COLORREF colorAt(HWND edit, LONG cp) {
    CHARRANGE cr{cp, cp + 1};
    SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof cf;
    SendMessageW(edit, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    return cf.crTextColor;
}

void diag(HWND plugin, const char* when) {
    HWND edit = findChild(plugin, MSFTEDIT_CLASS);
    RECT rc{}, prc{}, hrc{};
    GetClientRect(edit, &rc);
    GetClientRect(plugin, &prc);
    GetClientRect(g_host, &hrc);
    POINTL tl{0, 0}, br{rc.right - 1, rc.bottom - 1};
    LRESULT top = SendMessageW(edit, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&tl));
    LRESULT bottom = SendMessageW(edit, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&br));
    GETTEXTLENGTHEX gl{GTL_NUMCHARS | GTL_PRECISE, 1200};
    LRESULT len = SendMessageW(edit, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&gl), 0);
    IUnknown* ole = nullptr;
    LRESULT hasOle = SendMessageW(edit, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&ole));
    if (ole) ole->Release();
    std::printf("    diag[%s]: edit %ldx%ld plugin %ldx%ld host %ldx%ld visible=%d len=%lld top=%lld bottom=%lld "
                "first=%lld lines=%lld ole=%lld\n",
                when, rc.right, rc.bottom, prc.right, prc.bottom, hrc.right, hrc.bottom, IsWindowVisible(edit) ? 1 : 0,
                (long long)len, (long long)top, (long long)bottom, (long long)SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0),
                (long long)SendMessageW(edit, EM_GETLINECOUNT, 0, 0), (long long)hasOle);
}

bool italicAt(HWND edit, LONG cp) {
    CHARRANGE cr{cp, cp + 1};
    SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof cf;
    SendMessageW(edit, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    return (cf.dwEffects & CFE_ITALIC) != 0;
}

COLORREF ref(Color c) { return RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }

struct Expected {
    std::u16string text;
    std::vector<Token> tokens;
    Theme theme;
};

Config pluginConfig() {
    IniDocument doc;
    return IniDocument::loadFile(g_pluginDir / "tcl-syntaxhl.ini", doc) ? Config::fromIni(doc) : Config::defaults();
}

Expected expectedFor(const fs::path& file, bool dark) {
    Expected e;
    auto cfg = pluginConfig();
    auto* lang = cfg.languageForFile(file);
    std::string bytes;
    readFileBytes(file, bytes);
    e.text = normalizeLineEndings(decodeText(bytes).text);
    if (!lang) return e;
    auto sel = selectLexer(cfg, *lang, g_pluginDir);
    if (sel.lexer) {
        LexState st;
        sel.lexer->lex(e.text, st, e.tokens);
    }
    e.theme = loadTheme(g_pluginDir, cfg.themeSet(dark), lang->id, dark);
    return e;
}

// Compares colors of (up to `limit`) tokens whose start lies in [lo, hi).
int compareColors(HWND edit, const Expected& e, size_t lo = 0, size_t hi = SIZE_MAX, size_t limit = 400,
                  bool expectPlain = false) {
    int bad = 0, n = 0;
    for (const auto& t : e.tokens) {
        if (t.start < lo || t.start >= hi) continue;
        if (n++ >= static_cast<int>(limit)) break;
        COLORREF want = ref(expectPlain ? e.theme.foreground : e.theme.style(t.cls).fg);
        COLORREF got = colorAt(edit, static_cast<LONG>(t.start));
        if (want != got) {
            if (bad < 5)
                std::printf("    token @%u len %u class %s: want %06lX got %06lX\n", t.start, t.length,
                            tokenClassName(t.cls), static_cast<unsigned long>(want), static_cast<unsigned long>(got));
            ++bad;
        }
    }
    return n == 0 ? -1 : bad;
}

std::u16string trimTrailingCr(std::u16string s) {
    while (!s.empty() && s.back() == u'\r') s.pop_back();
    return s;
}

HWND open(const fs::path& file, int flags = 0) {
    std::wstring w = file.wstring();
    HWND h = api.load(g_host, w.data(), flags);
    if (h) {
        RECT rc;
        GetClientRect(g_host, &rc);
        MoveWindow(h, 0, 0, rc.right, rc.bottom, TRUE);
        pump(150);
    }
    return h;
}

void close(HWND h) {
    if (h) api.close(h);
    pump(20);
}

// Temporarily replaces the plugin INI.
struct IniOverride {
    fs::path path = g_pluginDir / "tcl-syntaxhl.ini";
    std::string original;
    explicit IniOverride(const std::function<void(IniDocument&)>& edit) {
        readFileBytes(path, original);
        auto doc = IniDocument::parse(std::string_view(original));
        edit(doc);
        doc.saveFile(path);
    }
    ~IniOverride() {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f.write(original.data(), static_cast<std::streamsize>(original.size()));
    }
};

void testSample(const char* name, int flags = 0) {
    auto file = g_samples / name;
    bool dark = (flags & lcp_darkmode) != 0;
    HWND h = open(file, flags);
    check(h != nullptr, std::string("ListLoadW ") + name);
    if (!h) return;
    HWND edit = findChild(h, MSFTEDIT_CLASS);
    check(edit != nullptr, "RichEdit child exists");
    auto e = expectedFor(file, dark);
    check(trimTrailingCr(editText(edit)) == trimTrailingCr(e.text), std::string(name) + ": text matches file");
    int bad = compareColors(edit, e);
    check(bad == 0, std::string(name) + ": token colors match theme (" + std::to_string(bad) + " mismatches)");
    HWND info = findChild(h, L"TclSyntaxHL_InfoBar");
    check(info && !IsWindowVisible(info), std::string(name) + ": no info bar");
    close(h);
}

void writeFile(const fs::path& p, const std::string& data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::printf("usage: plugin_smoke <tcl-syntaxhl.wlx64> <samples-dir>\n");
        return 2;
    }
    fs::path plugin = argv[1];
    g_pluginDir = fs::absolute(plugin).parent_path();
    g_samples = fs::absolute(argv[2]);
    g_tmp = fs::temp_directory_path() / "tcl_syntaxhl_smoke";
    fs::create_directories(g_tmp);

    HMODULE mod = LoadLibraryW(plugin.wstring().c_str());
    if (!mod) {
        std::printf("cannot load %ls (error %lu)\n", plugin.wstring().c_str(), GetLastError());
        return 2;
    }
    api.load = proc<LoadW_t>(mod, "ListLoadW");
    api.loadNext = proc<LoadNextW_t>(mod, "ListLoadNextW");
    api.close = proc<Close_t>(mod, "ListCloseWindow");
    api.detect = proc<Detect_t>(mod, "ListGetDetectString");
    api.search = proc<SearchW_t>(mod, "ListSearchTextW");
    api.command = proc<Command_t>(mod, "ListSendCommand");
    for (const char* n : {"ListLoad", "ListLoadNext", "ListSearchText", "ListSetDefaultParams"})
        check(GetProcAddress(mod, n) != nullptr, std::string("export ") + n);
    if (!api.load || !api.loadNext || !api.close || !api.detect || !api.search || !api.command) {
        std::printf("missing exports\n");
        return 2;
    }

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = hostProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TLister";
    RegisterClassExW(&wc);
    g_host = CreateWindowExW(0, L"TLister", L"smoke", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 900, 600, nullptr,
                             nullptr, wc.hInstance, nullptr);
    ShowWindow(g_host, SW_SHOWNOACTIVATE);
    pump(50);

    section("detect string");
    {
        char buf[2048] = {};
        api.detect(buf, sizeof buf);
        std::string s = buf;
        std::printf("  %s\n", buf);
        for (const char* ext : {"JSON", "CSS", "PY", "PYW", "JS", "MJS", "CJS", "INI", "CFG", "INF"})
            check(s.find(std::string("EXT=\"") + ext + "\"") != std::string::npos, std::string("detect has ") + ext);
        char tiny[16] = {};
        api.detect(tiny, sizeof tiny);
        check(std::strlen(tiny) < sizeof tiny, "detect string respects maxlen");
    }

    section("samples (light)");
    for (const char* n : {"sample.json", "sample.css", "sample.py", "sample.js", "sample.ini"}) testSample(n);

    section("samples (dark mode flag)");
    for (const char* n : {"sample.json", "sample.py"}) testSample(n, lcp_darkmode);

    section("unsupported file / lcp_forceshow");
    {
        auto txt = g_tmp / "plain.txt";
        writeFile(txt, "hello\r\nworld = 1 // not highlighted\r\n");
        HWND h = open(txt);
        check(h == nullptr, "ListLoadW returns NULL for .txt");
        close(h);
        h = open(txt, lcp_forceshow);
        check(h != nullptr, "lcp_forceshow: .txt is shown");
        if (h) {
            HWND edit = findChild(h, MSFTEDIT_CLASS);
            check(trimTrailingCr(editText(edit)) == u"hello\rworld = 1 // not highlighted", "forced: text shown as is");
            Theme t = loadTheme(g_pluginDir, "light", "", false);
            check(colorAt(edit, 6) == ref(t.foreground), "forced: plain colors");
            check(api.command(h, lc_newparams, lcp_wraptext) == LISTPLUGIN_OK, "forced: lc_newparams");
            close(h);
        }
    }

    section("search / select all / percent");
    {
        HWND h = open(g_samples / "sample.py");
        HWND edit = findChild(h, MSFTEDIT_CLASS);
        auto text = expectedFor(g_samples / "sample.py", false).text;
        auto sel = [&] {
            CHARRANGE cr{};
            SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
            return cr;
        };
        std::wstring needle = L"return";
        size_t first = text.find(u"return"), second = text.find(u"return", first + 1);
        check(api.search(h, needle.data(), lcs_findfirst) == LISTPLUGIN_OK, "find first");
        check(sel().cpMin == static_cast<LONG>(first) && sel().cpMax == static_cast<LONG>(first + 6), "first match selected");
        check(api.search(h, needle.data(), 0) == LISTPLUGIN_OK, "find next");
        check(sel().cpMin == static_cast<LONG>(second), "second match selected");
        check(api.search(h, needle.data(), lcs_backwards) == LISTPLUGIN_OK, "find backwards");
        check(sel().cpMin == static_cast<LONG>(first), "backwards returns to first match");
        std::wstring upper = L"RETURN";
        check(api.search(h, upper.data(), lcs_findfirst | lcs_matchcase) == LISTPLUGIN_ERROR, "match case");
        {
            // Restore a known caret for the next checks.
            CHARRANGE z{0, 0};
            SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&z));
        }
        check(api.search(h, upper.data(), lcs_findfirst) == LISTPLUGIN_OK, "case-insensitive");
        std::wstring part = L"retur";
        check(api.search(h, part.data(), lcs_findfirst | lcs_wholewords) == LISTPLUGIN_ERROR, "whole words");
        std::wstring none = L"zzz-not-there";
        check(api.search(h, none.data(), lcs_findfirst) == LISTPLUGIN_ERROR, "not found -> error");

        check(api.command(h, lc_selectall, 0) == LISTPLUGIN_OK, "lc_selectall");
        CHARRANGE all = sel();
        check(all.cpMin == 0 && all.cpMax >= static_cast<LONG>(trimTrailingCr(text).size()), "everything selected");
        check(api.command(h, lc_copy, 0) == LISTPLUGIN_OK, "lc_copy");
        check(api.command(h, lc_newparams, lcp_wraptext) == LISTPLUGIN_OK, "lc_newparams wrap");
        check(api.command(h, lc_newparams, 0) == LISTPLUGIN_OK, "lc_newparams nowrap");
        close(h);

        // Percent reporting needs a file longer than the window.
        std::string big;
        for (int i = 0; i < 2000; ++i) big += "key" + std::to_string(i) + "=value " + std::to_string(i) + "\n";
        writeFile(g_tmp / "long.ini", big);
        h = open(g_tmp / "long.ini");
        g_lastPercent = -1;
        diag(h, "long.ini loaded");
        api.command(h, lc_setpercent, 100);
        pump(500);
        diag(h, "after setpercent 100");
        check(g_lastPercent == 100, "percent reported after lc_setpercent 100 (got " + std::to_string(g_lastPercent) + ")");
        int at100 = g_lastPercent;
        api.command(h, lc_setpercent, 30);
        pump(500);
        check(g_lastPercent >= 28 && g_lastPercent <= 32, "percent ~30 after lc_setpercent 30 (got " +
                                                              std::to_string(g_lastPercent) + ")");
        api.command(h, lc_setpercent, 0);
        pump(500);
        {
            // lcs_findfirst starts at the first displayed line, not at the file start.
            api.command(h, lc_setpercent, 50);
            pump(200);
            HWND ed = findChild(h, MSFTEDIT_CLASS);
            POINTL tl{0, 0};
            auto top = static_cast<LONG>(SendMessageW(ed, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&tl)));
            std::wstring k = L"value 1";
            check(api.search(h, k.data(), lcs_findfirst) == LISTPLUGIN_OK, "find first from middle");
            CHARRANGE cr{};
            SendMessageW(ed, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
            check(cr.cpMin >= top - 40, "find first starts at first displayed line (match " + std::to_string(cr.cpMin) +
                                            ", top " + std::to_string(top) + ")");
            api.command(h, lc_setpercent, 0);
            pump(200);
        }
        check(g_lastPercent == 0, "percent 0 after lc_setpercent 0 (got " + std::to_string(g_lastPercent) + ", before " +
                                      std::to_string(at100) + ")");
        close(h);
    }

    section("keyboard / beeps / tab width");
    {
        HWND h = open(g_samples / "sample.ini");
        HWND edit = findChild(h, MSFTEDIT_CLASS);
        g_pluginOrigProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&listerSubclass)));
        g_pluginKeys.clear();
        SendMessageW(edit, WM_KEYDOWN, 'N', 0x00310001);
        SendMessageW(edit, WM_CHAR, 'n', 0x00310001);
        SendMessageW(edit, WM_KEYDOWN, VK_ESCAPE, 0x00010001);
        SendMessageW(edit, WM_KEYDOWN, VK_NEXT, 0x01510001);  // navigation stays in the view
        pump(100);
        auto count = [](UINT m, WPARAM w) {
            int n = 0;
            for (auto& k : g_pluginKeys) n += (k.first == m && k.second == w);
            return n;
        };
        check(count(WM_KEYDOWN, 'N') == 1, "N keydown reaches the plugin window once");
        check(count(WM_CHAR, 'n') == 1, "'n' char reaches the plugin window once");
        check(count(WM_KEYDOWN, VK_ESCAPE) == 1, "Esc reaches the plugin window once");
        check(count(WM_KEYDOWN, VK_NEXT) == 0, "PgDn is handled by the view");
        LRESULT style = SendMessageW(edit, EM_GETEDITSTYLE, 0, 0);
        check((style & SES_ALLOWBEEPS) == 0, "RichEdit beeps disabled by default");
        SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_pluginOrigProc));
        close(h);

        auto tabX = [&](const char* width) {
            IniOverride ov([&](IniDocument& d) { d.set("General", "tab_width", width); });
            auto p = g_tmp / "tab.ini";
            writeFile(p, "\tX=1\n");
            HWND t = open(p);
            HWND ed = findChild(t, MSFTEDIT_CLASS);
            POINTL a{}, b{};
            SendMessageW(ed, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&a), 0);
            SendMessageW(ed, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&b), 1);
            close(t);
            return b.x - a.x;
        };
        long w4 = tabX("4"), w8 = tabX("8");
        check(w4 > 0 && w8 > w4 * 3 / 2 && w8 < w4 * 5 / 2,
              "tab_width 8 is about twice tab_width 4 (" + std::to_string(w4) + " / " + std::to_string(w8) + " px)");
    }

    section("navigation at the end of the text / debug log");
    {
        auto logFile = g_pluginDir / "tcl-syntaxhl.log";
        std::error_code ec;
        fs::remove(logFile, ec);
        IniOverride ov([](IniDocument& d) { d.set("General", "debug_log", "true"); });
        std::string big;
        for (int i = 0; i < 300; ++i) big += "k" + std::to_string(i) + "=v\n";
        writeFile(g_tmp / "nav.ini", big);
        HWND h = open(g_tmp / "nav.ini");
        HWND edit = findChild(h, MSFTEDIT_CLASS);
        GETTEXTLENGTHEX gl{GTL_NUMCHARS | GTL_PRECISE, 1200};
        LONG len = static_cast<LONG>(SendMessageW(edit, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&gl), 0));
        SendMessageW(edit, EM_SETSEL, len, len);
        SendMessageW(edit, WM_KEYDOWN, VK_NEXT, 0x01510001);
        SendMessageW(edit, WM_KEYUP, VK_NEXT, 0xC1510001);
        SendMessageW(edit, WM_KEYDOWN, VK_DOWN, 0x01500001);
        CHARRANGE cr{};
        SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
        check(cr.cpMin == len && cr.cpMax == len, "caret stays at the end");
        SendMessageW(edit, EM_SETSEL, 0, 0);
        SendMessageW(edit, WM_KEYDOWN, VK_PRIOR, 0x01490001);
        SendMessageW(edit, WM_KEYDOWN, VK_NEXT, 0x01510001);  // not at a limit: RichEdit moves the caret
        SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
        check(cr.cpMin > 0, "PgDn still moves the caret when not at the end");
        MessageBeep(MB_OK);  // from this exe: must be caught by the hook
        pump(50);
        close(h);
        std::string logText;
        readFileBytes(logFile, logText);
        check(logText.find("debug log started") != std::string::npos, "debug log written next to the DLL");
        check(logText.find("edit  <- WM_KEYDOWN wp=0x22") != std::string::npos, "key messages logged");
        check(logText.find("at limit: swallowed") != std::string::npos, "limit keys swallowed (logged)");
        check(logText.find("*** BEEP MessageBeep") != std::string::npos &&
                  logText.find("plugin_smoke.exe") != std::string::npos,
              "MessageBeep intercepted with caller module");
        fs::remove(logFile, ec);
    }

    section("mouse wheel (direct, not animated)");
    {
        std::string big;
        for (int i = 0; i < 500; ++i) big += "k" + std::to_string(i) + "=v\n";
        writeFile(g_tmp / "wheel.ini", big);
        HWND h = open(g_tmp / "wheel.ini");
        HWND edit = findChild(h, MSFTEDIT_CLASS);
        UINT perNotch = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &perNotch, 0);
        auto first = [&] { return static_cast<long>(SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0)); };
        SendMessageW(edit, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-120)), 0);
        check(first() == static_cast<long>(perNotch), "one notch down scrolls exactly " + std::to_string(perNotch) +
                                                          " lines at once (got " + std::to_string(first()) + ")");
        for (int i = 0; i < 3; ++i) SendMessageW(edit, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-40)), 0);
        check(first() == 2 * static_cast<long>(perNotch), "three 1/3 notches add up to one notch");
        SendMessageW(edit, WM_MOUSEWHEEL, MAKEWPARAM(0, 240), 0);
        check(first() == 0, "two notches up return to the top");
        close(h);
    }

    section("ListLoadNext");
    {
        HWND h = open(g_samples / "sample.json");
        std::wstring next = (g_samples / "sample.css").wstring();
        check(api.loadNext(g_host, h, next.data(), 0) == LISTPLUGIN_OK, "load next .css");
        pump(150);
        HWND edit = findChild(h, MSFTEDIT_CLASS);
        auto e = expectedFor(g_samples / "sample.css", false);
        check(trimTrailingCr(editText(edit)) == trimTrailingCr(e.text), "content replaced");
        check(compareColors(edit, e) == 0, "css colors after load next");
        std::wstring txt = (g_tmp / "plain.txt").wstring();
        check(api.loadNext(g_host, h, txt.data(), 0) == LISTPLUGIN_ERROR, "load next unsupported -> error");
        close(h);
    }

    section("encodings");
    {
        std::u16string sample = u"{\"text\": \"café 中文 \U0001F600\", \"n\": 1}\r\n";
        std::string utf8 = utf16ToUtf8(sample);
        std::string le = "\xFF\xFE", be = "\xFE\xFF";
        for (char16_t c : sample) {
            le.push_back(static_cast<char>(c & 0xFF));
            le.push_back(static_cast<char>(c >> 8));
            be.push_back(static_cast<char>(c >> 8));
            be.push_back(static_cast<char>(c & 0xFF));
        }
        struct Case { const char* name; std::string data; } cases[] = {
            {"utf8.json", utf8}, {"utf8bom.json", "\xEF\xBB\xBF" + utf8}, {"utf16le.json", le}, {"utf16be.json", be}};
        for (auto& c : cases) {
            auto p = g_tmp / c.name;
            writeFile(p, c.data);
            HWND h = open(p);
            check(h != nullptr, std::string("load ") + c.name);
            if (!h) continue;
            HWND edit = findChild(h, MSFTEDIT_CLASS);
            check(trimTrailingCr(editText(edit)) == trimTrailingCr(normalizeLineEndings(sample)),
                  std::string(c.name) + ": decoded text");
            check(compareColors(edit, expectedFor(p, false)) == 0, std::string(c.name) + ": colors");
            close(h);
        }
        // ANSI (non-UTF-8 bytes) must still load.
        auto p = g_tmp / "ansi.ini";
        writeFile(p, "[S]\nname=caf\xE9\n");
        HWND h = open(p);
        check(h != nullptr, "load ANSI file");
        close(h);
    }

    section("large file -> plain text + info bar");
    {
        std::string big = "[\n";
        while (big.size() < 6u * 1024 * 1024) big += "  {\"id\": 12345, \"name\": \"item\", \"ok\": true},\n";
        big += "  {}\n]\n";
        auto p = g_tmp / "big.json";
        writeFile(p, big);
        HWND h = open(p);
        check(h != nullptr, "load 6 MB json");
        if (h) {
            HWND info = findChild(h, L"TclSyntaxHL_InfoBar");
            check(info && IsWindowVisible(info), "info bar visible");
            wchar_t cls[64];
            HWND edit = findChild(h, MSFTEDIT_CLASS);
            GetClassNameW(edit, cls, 64);
            auto e = expectedFor(p, false);
            check(compareColors(edit, e, 0, 5000, 50, true) == 0, "plain text colors (foreground only)");
            close(h);
        }
    }

    section("timing (informational)");
    {
        std::string big = "[\n";
        while (big.size() < 1900u * 1024) big += "  {\"id\": 12345, \"name\": \"item\", \"ok\": true, \"v\": [1, 2.5, null]},\n";
        big += "  {}\n]\n";
        auto p = g_tmp / "full.json";
        writeFile(p, big);
        DWORD t0 = GetTickCount();
        std::wstring w = p.wstring();
        HWND h = api.load(g_host, w.data(), 0);
        DWORD t1 = GetTickCount();
        check(h != nullptr, "load 1.9 MB json (full highlighting)");
        std::printf("  1.9 MB JSON, full highlighting: %lu ms\n", static_cast<unsigned long>(t1 - t0));
        if (h) {
            auto e = expectedFor(p, false);
            check(compareColors(findChild(h, MSFTEDIT_CLASS), e, e.text.size() - 2000, SIZE_MAX) == 0,
                  "end of 1.9 MB file highlighted");
        }
        close(h);
    }

    section("lazy highlighting");
    {
        IniOverride ov([](IniDocument& d) {
            d.set("General", "use_lazy_highlight", "true");
            d.set("General", "max_highlight_size", "64K");
        });
        std::string py;
        for (int i = 0; i < 6000; ++i)
            py += "def f" + std::to_string(i) + "(x):\n    \"\"\"doc " + std::to_string(i) + "\"\"\"\n    return x + " +
                  std::to_string(i) + "  # c\n";
        auto p = g_tmp / "lazy.py";
        writeFile(p, py);
        HWND h = open(p);
        check(h != nullptr, "load lazy file");
        if (h) {
            pump(300);
            HWND edit = findChild(h, MSFTEDIT_CLASS);
            diag(h, "lazy loaded");
            HWND info = findChild(h, L"TclSyntaxHL_InfoBar");
            check(info && !IsWindowVisible(info), "no info bar in lazy mode");
            auto e = expectedFor(p, false);
            check(compareColors(edit, e, 0, 600) == 0, "top of file highlighted");
            size_t mid = e.text.size() / 2;
            check(compareColors(edit, e, mid, mid + 400, 50, true) == 0, "middle not highlighted yet");
            api.command(h, lc_setpercent, 50);
            pump(400);
            POINTL tl{0, 0};
            auto top = static_cast<size_t>(SendMessageW(edit, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&tl)));
            check(top > e.text.size() / 3 && top < e.text.size() * 2 / 3, "lc_setpercent 50 scrolls to the middle");
            check(compareColors(edit, e, top, top + 400) == 0, "visible middle highlighted after scrolling there");
            api.command(h, lc_setpercent, 100);
            pump(400);
            check(compareColors(edit, e, e.text.size() - 400, SIZE_MAX) == 0, "end highlighted after scrolling");
            close(h);
        }
    }

    section("regex tokenizer");
    {
        IniOverride ov([](IniDocument& d) { d.set("General", "tokenizer", "regex"); });
        HWND h = open(g_samples / "sample.json");
        check(h != nullptr, "load json with regex tokenizer");
        if (h) {
            auto e = expectedFor(g_samples / "sample.json", false);
            check(compareColors(findChild(h, MSFTEDIT_CLASS), e) == 0, "regex colors");
            HWND info = findChild(h, L"TclSyntaxHL_InfoBar");
            check(info && !IsWindowVisible(info), "no notice for json");
            close(h);
        }
        for (const char* n : {"sample.ini", "sample.css", "sample.js"}) {
            h = open(g_samples / n);
            check(h != nullptr, std::string("load with regex tokenizer: ") + n);
            if (!h) continue;
            check(compareColors(findChild(h, MSFTEDIT_CLASS), expectedFor(g_samples / n, false)) == 0,
                  std::string("regex colors: ") + n);
            HWND info = findChild(h, L"TclSyntaxHL_InfoBar");
            check(info && !IsWindowVisible(info), std::string("no notice: ") + n);
            close(h);
        }
        h = open(g_samples / "sample.py");
        check(h != nullptr, "python falls back to native");
        if (h) {
            HWND info = findChild(h, L"TclSyntaxHL_InfoBar");
            check(info && IsWindowVisible(info), "fallback notice shown");
            check(compareColors(findChild(h, MSFTEDIT_CLASS), expectedFor(g_samples / "sample.py", false)) == 0,
                  "native colors for python");
            close(h);
        }
    }

    section("italic_comments");
    {
        auto py = g_samples / "sample.py";
        auto e = expectedFor(py, false);
        LONG commentPos = -1;
        for (const auto& t : e.tokens)
            if (t.cls == TokenClass::Comment) { commentPos = static_cast<LONG>(t.start); break; }
        check(commentPos >= 0, "sample.py has a comment");
        HWND h = open(py);
        if (h) {
            check(italicAt(findChild(h, MSFTEDIT_CLASS), commentPos), "comments italic by default");
            close(h);
        }
        {
            IniOverride ov([](IniDocument& d) { d.set("General", "italic_comments", "false"); });
            h = open(py);
            if (h) {
                check(!italicAt(findChild(h, MSFTEDIT_CLASS), commentPos), "italic_comments=false");
                close(h);
            }
        }
        {
            IniOverride ov([](IniDocument& d) {
                d.set("General", "italic_comments", "false");
                d.set("Python", "italic_comments", "true");
            });
            h = open(py);
            if (h) {
                check(italicAt(findChild(h, MSFTEDIT_CLASS), commentPos), "per-language override wins");
                close(h);
            }
        }
    }

    section("extension added in [Languages] (JS=...,pac)");
    {
        auto pac = g_samples / "sample.pac";
        HWND h = open(pac);
        check(h == nullptr, ".pac is not handled with the default INI");
        close(h);
        IniOverride ov([](IniDocument& d) { d.set("Languages", "JS", "js,mjs,cjs,pac"); });
        char buf[2048] = {};
        api.detect(buf, sizeof buf);
        check(std::string(buf).find("EXT=\"PAC\"") != std::string::npos, "detect string now contains PAC");
        h = open(pac);
        check(h != nullptr, ".pac loads after adding it to JS=");
        if (h) {
            auto e = expectedFor(pac, false);
            check(!e.tokens.empty(), ".pac is lexed as JS");
            check(compareColors(findChild(h, MSFTEDIT_CLASS), e) == 0, ".pac colors (JS)");
            close(h);
        }
    }

    section("settings window");
    {
        IniOverride ov([](IniDocument&) {});  // restore the INI afterwards
        auto py = g_samples / "sample.py";
        auto e = expectedFor(py, false);
        LONG commentPos = -1;
        for (const auto& t : e.tokens)
            if (t.cls == TokenClass::Comment) { commentPos = static_cast<LONG>(t.start); break; }
        HWND h = open(py);
        check(h != nullptr, "open sample.py");
        std::string log;
        g_dialogSeen = false;
        g_dialogScript = [&](HWND dlg) {
            auto set = [&](int id, const wchar_t* text) { SetWindowTextW(findDescendant(dlg, id), text); };
            auto setCheck = [&](int id, bool on) {
                SendMessageW(findDescendant(dlg, id), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
            };
            set(IDC_GEN_TABWIDTH, L"8");
            set(IDC_GEN_MAXSIZE, L"3M");
            setCheck(IDC_GEN_ITALIC, false);
            // Languages page: select JSON, add an extension, enable JSONC.
            HWND list = findDescendant(dlg, IDC_LANG_LIST);
            SendMessageW(list, LB_SELECTSTRING, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(L"JSON"));
            SendMessageW(GetParent(list), WM_COMMAND, MAKEWPARAM(IDC_LANG_LIST, LBN_SELCHANGE),
                         reinterpret_cast<LPARAM>(list));
            set(IDC_LANG_EXTS, L"json, .JSONC");
            HWND jsonc = findDescendant(dlg, IDC_LANG_OPTION_FIRST);
            wchar_t label[128] = {};
            GetWindowTextW(jsonc, label, 128);
            log = utf16ToUtf8(std::u16string(label, label + wcslen(label)));
            setCheck(IDC_LANG_OPTION_FIRST, true);
            SendMessageW(dlg, WM_COMMAND, IDOK, 0);
        };
        SetTimer(nullptr, 0, 100, dialogTimer);
        PostMessageW(h, WM_COMMAND, IDM_SETTINGS, 0);
        pump(1500);
        check(g_dialogSeen, "settings window opened via IDM_SETTINGS");
        check(!findSettingsDialog(), "settings window closed by OK");
        check(log.find("JSONC") != std::string::npos, "lexer option checkbox generated for JSON (" + log + ")");
        std::string ini;
        readFileBytes(g_pluginDir / "tcl-syntaxhl.ini", ini);
        check(ini.find("tab_width=8") != std::string::npos, "tab_width saved");
        check(ini.find("max_highlight_size=3M") != std::string::npos, "size saved as 3M");
        check(ini.find("italic_comments=false") != std::string::npos, "italic_comments saved");
        check(ini.find("JSON=json,jsonc") != std::string::npos, "extensions normalised and saved");
        check(ini.find("support_jsonc_comments=true") != std::string::npos, "lexer option saved");
        check(ini.find("; Tokenizer backend") != std::string::npos, "comments preserved");
        check(ini.find("use_lazy_highlight=false") != std::string::npos, "untouched values kept");
        if (h) {
            check(!italicAt(findChild(h, MSFTEDIT_CLASS), commentPos), "view reloaded with the new settings");
            close(h);
        }
        g_dialogScript = nullptr;
    }

    section("theme editor");
    {
        IniOverride ov([](IniDocument&) {});
        auto themeFile = g_pluginDir / "themes" / "light" / "python.theme";
        std::string themeBackup;
        readFileBytes(themeFile, themeBackup);
        auto py = g_samples / "sample.py";
        auto e = expectedFor(py, false);
        LONG kwPos = -1;
        for (const auto& t : e.tokens)
            if (t.cls == TokenClass::Keyword) { kwPos = static_cast<LONG>(t.start); break; }
        HWND h = open(py);
        auto boldAt = [&](LONG cp) {
            HWND ed = findChild(h, MSFTEDIT_CLASS);
            CHARRANGE cr{cp, cp + 1};
            SendMessageW(ed, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
            CHARFORMAT2W cf{};
            cf.cbSize = sizeof cf;
            SendMessageW(ed, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
            return (cf.dwEffects & CFE_BOLD) != 0;
        };
        check(!boldAt(kwPos), "keywords not bold before");
        std::wstring fileLabel;
        bool colorDialogSeen = false;
        g_dialogSeen = false;
        g_dialogScript = [&](HWND dlg) {
            wchar_t buf[MAX_PATH] = {};
            GetWindowTextW(findDescendant(dlg, IDC_THEME_FILE), buf, MAX_PATH);
            fileLabel = buf;
            HWND list = findDescendant(dlg, IDC_THEME_LIST);
            int keywordRow = 4 + static_cast<int>(TokenClass::Keyword) - 1;
            SendMessageW(list, LB_SETCURSEL, keywordRow, 0);
            SendMessageW(GetParent(list), WM_COMMAND, MAKEWPARAM(IDC_THEME_LIST, LBN_SELCHANGE),
                         reinterpret_cast<LPARAM>(list));
            SendMessageW(findDescendant(dlg, IDC_THEME_BOLD), BM_CLICK, 0, 0);
            // Foreground button opens the color picker: accept it unchanged.
            SetTimer(nullptr, 0, 200, [](HWND, UINT, UINT_PTR id, DWORD) {
                HWND found = nullptr;
                EnumThreadWindows(
                    GetCurrentThreadId(),
                    [](HWND w, LPARAM p) -> BOOL {
                        wchar_t cls[32] = {}, title[64] = {};
                        GetClassNameW(w, cls, 32);
                        GetWindowTextW(w, title, 64);
                        if (wcscmp(cls, L"#32770") == 0 && IsWindowVisible(w) &&
                            wcsncmp(title, L"tcl-syntaxhl settings", 21) != 0) {
                            *reinterpret_cast<HWND*>(p) = w;
                            return FALSE;
                        }
                        return TRUE;
                    },
                    reinterpret_cast<LPARAM>(&found));
                if (found) {
                    KillTimer(nullptr, id);
                    PostMessageW(found, WM_COMMAND, IDOK, 0);
                }
            });
            SendMessageW(findDescendant(dlg, IDC_THEME_FG), BM_CLICK, 0, 0);
            colorDialogSeen = true;
            SendMessageW(dlg, WM_COMMAND, IDOK, 0);
        };
        SetTimer(nullptr, 0, 100, dialogTimer);
        PostMessageW(h, WM_COMMAND, IDM_SETTINGS, 0);
        pump(3000);
        check(g_dialogSeen && colorDialogSeen, "theme editor driven");
        check(fileLabel.find(L"python.theme") != std::wstring::npos, "editor opens the current language's theme");
        std::string saved;
        readFileBytes(themeFile, saved);
        check(saved.find("keyword=") != std::string::npos && saved.find(",bold") != std::string::npos,
              "keyword bold written to python.theme");
        check(saved.find("decorator=") != std::string::npos, "existing entries kept");
        check(saved.find("; Python overrides") != std::string::npos, "comments kept");
        check(boldAt(kwPos), "view reloaded: keywords now bold");
        close(h);
        writeFile(themeFile, themeBackup);
        g_dialogScript = nullptr;
    }

    section("dark mode dialogs");
    {
        HWND h = open(g_samples / "sample.ini", lcp_darkmode);
        COLORREF dlgColor = 0, editColor = 0;
        g_dialogSeen = false;
        g_dialogScript = [&](HWND dlg) {
            HDC dc = GetDC(dlg);
            auto color = [&](UINT msg, HWND ctl) {
                auto br = reinterpret_cast<HBRUSH>(SendMessageW(dlg, msg, reinterpret_cast<WPARAM>(dc),
                                                                reinterpret_cast<LPARAM>(ctl)));
                LOGBRUSH lb{};
                if (br) GetObjectW(br, sizeof lb, &lb);
                return lb.lbColor;
            };
            dlgColor = color(WM_CTLCOLORDLG, dlg);
            HWND page = GetParent(findDescendant(dlg, IDC_GEN_MAXSIZE));
            auto br = reinterpret_cast<HBRUSH>(SendMessageW(page, WM_CTLCOLOREDIT, reinterpret_cast<WPARAM>(dc),
                                                            reinterpret_cast<LPARAM>(findDescendant(dlg, IDC_GEN_MAXSIZE))));
            LOGBRUSH lb{};
            if (br) GetObjectW(br, sizeof lb, &lb);
            editColor = lb.lbColor;
            ReleaseDC(dlg, dc);
            SendMessageW(dlg, WM_COMMAND, IDCANCEL, 0);
        };
        SetTimer(nullptr, 0, 100, dialogTimer);
        PostMessageW(h, WM_COMMAND, IDM_SETTINGS, 0);
        pump(1500);
        check(g_dialogSeen, "settings window opened in dark mode");
        check(dlgColor == RGB(32, 32, 32), "dark dialog background");
        check(editColor == RGB(43, 43, 43), "dark edit background");
        close(h);
        g_dialogScript = nullptr;
    }

    section("options: jsonc + scss");
    {
        IniOverride ov([](IniDocument& d) {
            d.set("JSON", "support_jsonc_comments", "true");
            d.set("CSS", "support_scss_less", "true");
            d.set("Languages", "CSS", "css,scss,less");
        });
        writeFile(g_tmp / "c.json", "{\n  // comment\n  \"a\": 1 /* b */\n}\n");
        writeFile(g_tmp / "s.scss", "$c: #fff;\n.a { &:hover { color: $c; } // x\n}\n");
        for (const char* n : {"c.json", "s.scss"}) {
            HWND h = open(g_tmp / n);
            check(h != nullptr, std::string("load ") + n);
            if (!h) continue;
            check(compareColors(findChild(h, MSFTEDIT_CLASS), expectedFor(g_tmp / n, false)) == 0,
                  std::string(n) + " colors");
            close(h);
        }
    }

    DestroyWindow(g_host);
    pump(50);
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    fs::remove_all(g_tmp);
    return g_failures == 0 ? 0 : 1;
}
