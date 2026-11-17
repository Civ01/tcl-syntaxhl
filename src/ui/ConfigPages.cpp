// Pages of the settings window: General, Languages, Theme.
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <optional>
#include <set>

#include "ConfigDialog.h"
#include "core/LexerFactory.h"
#include "core/OptionSchema.h"
#include "core/TextUtil.h"
#include "core/Theme.h"
#include "plugin/WinUtil.h"
#include "resource.h"

namespace shl::ui {

namespace {

// ------------------------------------------------------------------ helpers
std::wstring getText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}
std::string getTextUtf8(HWND h) { return win::narrow(getText(h)); }
void setText(HWND h, std::string_view utf8) { SetWindowTextW(h, win::widen(utf8).c_str()); }

void comboAdd(HWND combo, const std::wstring& text) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
}
// Selects the item equal to `text` (case-insensitive); adds it if missing.
void comboSelect(HWND combo, const std::wstring& text) {
    LRESULT i = SendMessageW(combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(text.c_str()));
    if (i == CB_ERR) {
        LONG style = GetWindowLongW(combo, GWL_STYLE);
        if ((style & 0x3) == CBS_DROPDOWN) {  // editable: just show the text
            SetWindowTextW(combo, text.c_str());
            return;
        }
        i = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
}
std::wstring comboText(HWND combo) {
    LRESULT i = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (i == CB_ERR) return getText(combo);  // editable combo
    LRESULT n = SendMessageW(combo, CB_GETLBTEXTLEN, static_cast<WPARAM>(i), 0);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(s.data()));
    s.resize(static_cast<size_t>(n));
    return s;
}
bool checked(HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; }
void setChecked(HWND h, bool v) { SendMessageW(h, BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0); }

std::vector<std::wstring> fixedPitchFonts() {
    std::set<std::wstring> names;
    HDC dc = GetDC(nullptr);
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(
        dc, &lf,
        [](const LOGFONTW* f, const TEXTMETRICW*, DWORD, LPARAM p) -> int {
            if ((f->lfPitchAndFamily & 0x3) == FIXED_PITCH && f->lfFaceName[0] != L'@')
                reinterpret_cast<std::set<std::wstring>*>(p)->insert(f->lfFaceName);
            return 1;
        },
        reinterpret_cast<LPARAM>(&names), 0);
    ReleaseDC(nullptr, dc);
    return {names.begin(), names.end()};
}

void copyToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        memcpy(GlobalLock(mem), text.c_str(), bytes);
        GlobalUnlock(mem);
        if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
    }
    CloseClipboard();
}

// ------------------------------------------------------------------ General
class GeneralPage final : public ConfigPage {
public:
    explicit GeneralPage(const ConfigContext& ctx) : ctx_(ctx) {}
    const wchar_t* title() const override { return L"General"; }
    int dialogId() const override { return IDD_PAGE_GENERAL; }

    void init() override {
        for (const char* c : findGeneralOption("tokenizer")->choices) comboAdd(item(IDC_GEN_TOKENIZER), win::widen(c));
        auto sets = listThemeSets(ctx_.pluginDir);
        comboAdd(item(IDC_GEN_THEME), L"auto");
        for (const auto& s : sets) {
            comboAdd(item(IDC_GEN_THEME), win::widen(s));
            comboAdd(item(IDC_GEN_THEME_LIGHT), win::widen(s));
            comboAdd(item(IDC_GEN_THEME_DARK), win::widen(s));
        }
        for (const auto& f : fixedPitchFonts()) comboAdd(item(IDC_GEN_FONT), f);
        auto* fs = findGeneralOption("font_size");
        auto* tw = findGeneralOption("tab_width");
        SendMessageW(item(IDC_GEN_FONTSIZE_SPIN), UDM_SETRANGE32, fs->minValue, fs->maxValue);
        SendMessageW(item(IDC_GEN_TABWIDTH_SPIN), UDM_SETRANGE32, tw->minValue, tw->maxValue);
    }

    void load(const Config& c) override {
        comboSelect(item(IDC_GEN_TOKENIZER), win::widen(tokenizerName(c.tokenizer)));
        setText(item(IDC_GEN_MAXSIZE), formatSizeSpec(c.maxHighlightSize));
        setChecked(item(IDC_GEN_LAZY), c.useLazyHighlight);
        comboSelect(item(IDC_GEN_THEME), win::widen(c.theme));
        comboSelect(item(IDC_GEN_THEME_LIGHT), win::widen(c.themeLight));
        comboSelect(item(IDC_GEN_THEME_DARK), win::widen(c.themeDark));
        comboSelect(item(IDC_GEN_FONT), win::widen(c.fontFace));
        SendMessageW(item(IDC_GEN_FONTSIZE_SPIN), UDM_SETPOS32, 0, c.fontSize);
        SendMessageW(item(IDC_GEN_TABWIDTH_SPIN), UDM_SETPOS32, 0, c.tabWidth);
        setChecked(item(IDC_GEN_ITALIC), c.italicComments);
        setChecked(item(IDC_GEN_BEEPS), c.allowBeeps);
        setChecked(item(IDC_GEN_DEBUGLOG), c.debugLog);
    }

    HWND validate(std::wstring& err) override {
        Config probe;
        if (!setGeneralValue(probe, "max_highlight_size", getTextUtf8(item(IDC_GEN_MAXSIZE)))) {
            err = L"\"Highlight files up to\" must be a size such as 2M, 512K or 1000000.";
            return item(IDC_GEN_MAXSIZE);
        }
        if (!setGeneralValue(probe, "font_size", getTextUtf8(item(IDC_GEN_FONTSIZE)))) {
            err = L"Font size must be between 4 and 96.";
            return item(IDC_GEN_FONTSIZE);
        }
        if (!setGeneralValue(probe, "tab_width", getTextUtf8(item(IDC_GEN_TABWIDTH)))) {
            err = L"Tab width must be between 1 and 32.";
            return item(IDC_GEN_TABWIDTH);
        }
        if (getText(item(IDC_GEN_FONT)).empty()) {
            err = L"Please choose a font.";
            return item(IDC_GEN_FONT);
        }
        return nullptr;
    }

    void store(Config& c) override {
        setGeneralValue(c, "tokenizer", win::narrow(comboText(item(IDC_GEN_TOKENIZER))));
        setGeneralValue(c, "max_highlight_size", getTextUtf8(item(IDC_GEN_MAXSIZE)));
        c.useLazyHighlight = checked(item(IDC_GEN_LAZY));
        setGeneralValue(c, "theme", win::narrow(comboText(item(IDC_GEN_THEME))));
        setGeneralValue(c, "theme_light", win::narrow(comboText(item(IDC_GEN_THEME_LIGHT))));
        setGeneralValue(c, "theme_dark", win::narrow(comboText(item(IDC_GEN_THEME_DARK))));
        setGeneralValue(c, "font_face", win::narrow(comboText(item(IDC_GEN_FONT))));
        setGeneralValue(c, "font_size", getTextUtf8(item(IDC_GEN_FONTSIZE)));
        setGeneralValue(c, "tab_width", getTextUtf8(item(IDC_GEN_TABWIDTH)));
        c.italicComments = checked(item(IDC_GEN_ITALIC));
        c.allowBeeps = checked(item(IDC_GEN_BEEPS));
        c.debugLog = checked(item(IDC_GEN_DEBUGLOG));
    }

private:
    ConfigContext ctx_;
};

// ------------------------------------------------------------------ Add language dialog
struct AddLanguageData {
    const std::vector<LanguageDef>* existing;
    std::string name, lexer, extensions;
};

const wchar_t* kRulesOnly = L"(rules file only)";

INT_PTR CALLBACK addLanguageProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<AddLanguageData*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            HWND lexer = GetDlgItem(dlg, IDC_ADD_LEXER);
            for (const auto& id : nativeLexerIds()) comboAdd(lexer, win::widen(id));
            comboAdd(lexer, kRulesOnly);
            SendMessageW(lexer, CB_SETCURSEL, 0, 0);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            if (LOWORD(wp) == IDOK) {
                std::string name = win::narrow(getText(GetDlgItem(dlg, IDC_ADD_NAME)));
                std::string exts = win::narrow(getText(GetDlgItem(dlg, IDC_ADD_EXTS)));
                const wchar_t* problem = nullptr;
                if (!isValidLanguageName(name))
                    problem = L"The name may only contain letters, digits, _ + - and must not be General or Languages.\n"
                              L"It is also used for the theme and rules file names.";
                else
                    for (const auto& l : *d->existing)
                        if (iequals(l.name, name)) problem = L"A language with this name already exists.";
                if (!problem && splitList(exts, ',').empty()) problem = L"Please enter at least one extension.";
                if (problem) {
                    MessageBoxW(dlg, problem, L"Add language", MB_ICONWARNING | MB_OK);
                    return TRUE;
                }
                d->name = name;
                d->extensions = exts;
                std::wstring lx = comboText(GetDlgItem(dlg, IDC_ADD_LEXER));
                d->lexer = lx == kRulesOnly ? std::string() : win::narrow(lx);
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

// ------------------------------------------------------------------ Languages
class LanguagesPage final : public ConfigPage {
public:
    explicit LanguagesPage(const ConfigContext& ctx) : ctx_(ctx) {}
    const wchar_t* title() const override { return L"Languages"; }
    int dialogId() const override { return IDD_PAGE_LANGUAGES; }

    void init() override {
        for (const auto& id : nativeLexerIds()) comboAdd(item(IDC_LANG_LEXER), win::widen(id));
        comboAdd(item(IDC_LANG_LEXER), kRulesOnly);
        for (const wchar_t* t : {L"(default)", L"native", L"regex"}) comboAdd(item(IDC_LANG_TOKENIZER), t);
        for (const wchar_t* t : {L"(default)", L"on", L"off"}) comboAdd(item(IDC_LANG_ITALIC), t);
        // Combos are sorted alphabetically by CB_ADDSTRING only with CBS_SORT; ours are not.
    }

    void load(const Config& c) override {
        langs_ = c.languages;
        refreshList(sel_ >= 0 && sel_ < static_cast<int>(langs_.size()) ? sel_ : 0);
    }

    HWND validate(std::wstring& err) override {
        commit();
        for (int i = 0; i < static_cast<int>(langs_.size()); ++i) {
            auto& l = langs_[i];
            if (l.extensions.empty()) {
                select(i);
                err = L"Language " + win::widen(l.name) + L" has no file extensions.";
                return item(IDC_LANG_EXTS);
            }
            auto tw = l.options.get("tab_width");
            if (!tw.empty() && languageOverride("tab_width")->normalize(tw).empty()) {
                select(i);
                err = L"Tab width of " + win::widen(l.name) + L" must be between 1 and 32, or empty.";
                return item(IDC_LANG_TABWIDTH);
            }
        }
        return nullptr;
    }

    void store(Config& c) override {
        commit();
        c.languages = langs_;
    }

    INT_PTR handle(UINT msg, WPARAM wp, LPARAM) override {
        if (msg != WM_COMMAND) return FALSE;
        int id = LOWORD(wp), code = HIWORD(wp);
        switch (id) {
            case IDC_LANG_LIST:
                if (code == LBN_SELCHANGE) {
                    commit();
                    int i = static_cast<int>(SendMessageW(item(IDC_LANG_LIST), LB_GETCURSEL, 0, 0));
                    show(i);
                }
                return TRUE;
            case IDC_LANG_LEXER:
                if (code == CBN_SELCHANGE) {
                    commit();
                    show(sel_);  // rebuild lexer options
                }
                return TRUE;
            case IDC_LANG_EXTS:
                if (code == EN_KILLFOCUS) {
                    commit();
                    updateDetect();
                }
                return TRUE;
            case IDC_LANG_ADD: addLanguage(); return TRUE;
            case IDC_LANG_REMOVE: removeLanguage(); return TRUE;
            case IDC_LANG_COPY_DETECT:
                commit();
                updateDetect();
                copyToClipboard(hwnd_, getText(item(IDC_LANG_DETECT)));
                return TRUE;
        }
        return FALSE;
    }

private:
    static const OptionDef* languageOverride(const char* key) {
        for (const auto& d : languageOverrideOptions())
            if (std::string_view(d.key) == key) return &d;
        return nullptr;
    }

    void refreshList(int select) {
        SendMessageW(item(IDC_LANG_LIST), LB_RESETCONTENT, 0, 0);
        for (const auto& l : langs_)
            SendMessageW(item(IDC_LANG_LIST), LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(win::widen(l.name).c_str()));
        sel_ = -1;
        show(langs_.empty() ? -1 : std::clamp(select, 0, static_cast<int>(langs_.size()) - 1));
        updateDetect();
    }

    void select(int i) {
        SendMessageW(item(IDC_LANG_LIST), LB_SETCURSEL, static_cast<WPARAM>(i), 0);
        show(i);
    }

    // Shows language i in the detail controls (no commit).
    void show(int i) {
        sel_ = i;
        bool any = i >= 0;
        for (int id : {IDC_LANG_EXTS, IDC_LANG_LEXER, IDC_LANG_TOKENIZER, IDC_LANG_ITALIC, IDC_LANG_TABWIDTH,
                       IDC_LANG_REMOVE})
            EnableWindow(item(id), any);
        destroyOptionChecks();
        if (!any) {
            SetWindowTextW(item(IDC_LANG_TITLE), L"");
            return;
        }
        SendMessageW(item(IDC_LANG_LIST), LB_SETCURSEL, static_cast<WPARAM>(i), 0);
        const auto& l = langs_[i];
        SetWindowTextW(item(IDC_LANG_TITLE), win::widen(l.name).c_str());
        std::string exts;
        for (const auto& e : l.extensions) exts += (exts.empty() ? "" : ",") + e;
        setText(item(IDC_LANG_EXTS), exts);
        comboSelect(item(IDC_LANG_LEXER), hasNativeLexer(l.lexerId) ? win::widen(l.lexerId) : kRulesOnly);
        auto tk = toLowerAscii(l.options.get("tokenizer"));
        comboSelect(item(IDC_LANG_TOKENIZER), tk == "native" || tk == "regex" ? win::widen(tk) : L"(default)");
        auto it = l.options.get("italic_comments");
        auto itn = it.empty() ? std::string() : languageOverride("italic_comments")->normalize(it);
        comboSelect(item(IDC_LANG_ITALIC), itn == "true" ? L"on" : itn == "false" ? L"off" : L"(default)");
        setText(item(IDC_LANG_TABWIDTH), l.options.get("tab_width"));
        buildOptionChecks(l);
    }

    void destroyOptionChecks() {
        for (HWND h : optionChecks_) DestroyWindow(h);
        optionChecks_.clear();
        optionDefs_.clear();
    }

    void buildOptionChecks(const LanguageDef& l) {
        HWND group = item(IDC_LANG_OPTIONS_GROUP);
        RECT gr;
        GetWindowRect(group, &gr);
        MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&gr), 2);
        RECT unit{0, 0, 8, 10};
        MapDialogRect(hwnd_, &unit);  // 8 x 10 DLU in pixels
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd_, WM_GETFONT, 0, 0));
        int y = gr.top + unit.bottom * 12 / 10;
        int id = IDC_LANG_OPTION_FIRST;
        const auto& defs = lexerOptions(hasNativeLexer(l.lexerId) ? l.lexerId : std::string());
        if (defs.empty()) {
            HWND h = CreateWindowExW(0, L"STATIC", L"(this lexer has no options)", WS_CHILD | WS_VISIBLE,
                                     gr.left + unit.right, y, gr.right - gr.left - 2 * unit.right, unit.bottom, hwnd_,
                                     nullptr, win::g_instance, nullptr);
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
            optionChecks_.push_back(h);
            return;
        }
        for (const auto& d : defs) {
            HWND h = CreateWindowExW(0, L"BUTTON", win::widen(d.label).c_str(),
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, gr.left + unit.right, y,
                                     gr.right - gr.left - 2 * unit.right, unit.bottom, hwnd_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(id++)), win::g_instance, nullptr);
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
            setChecked(h, l.options.getBool(d.key, parseBool(d.defaultValue, false)));
            optionChecks_.push_back(h);
            optionDefs_.push_back(&d);
            y += unit.bottom * 14 / 10;
        }
    }

    // Detail controls -> langs_[sel_].
    void commit() {
        if (sel_ < 0 || sel_ >= static_cast<int>(langs_.size())) return;
        auto& l = langs_[sel_];
        auto& o = l.options.values;
        l.extensions.clear();
        for (auto& e : splitList(getTextUtf8(item(IDC_LANG_EXTS)), ',')) {
            auto x = toLowerAscii(e);
            while (!x.empty() && (x[0] == '.' || x[0] == '*')) x.erase(0, 1);
            if (!x.empty() && std::find(l.extensions.begin(), l.extensions.end(), x) == l.extensions.end())
                l.extensions.push_back(x);
        }
        std::wstring lexer = comboText(item(IDC_LANG_LEXER));
        if (lexer == kRulesOnly) {
            if (hasNativeLexer(l.id)) o["lexer"] = "none";
            else o.erase("lexer");
        } else if (win::narrow(lexer) == l.id) {
            o.erase("lexer");
        } else {
            o["lexer"] = win::narrow(lexer);
        }
        std::wstring tk = comboText(item(IDC_LANG_TOKENIZER));
        if (tk == L"(default)") o.erase("tokenizer");
        else o["tokenizer"] = win::narrow(tk);
        std::wstring it = comboText(item(IDC_LANG_ITALIC));
        if (it == L"(default)") o.erase("italic_comments");
        else o["italic_comments"] = it == L"on" ? "true" : "false";
        std::string tw(trim(getTextUtf8(item(IDC_LANG_TABWIDTH))));
        if (tw.empty()) o.erase("tab_width");
        else o["tab_width"] = tw;
        for (size_t k = 0; k < optionDefs_.size(); ++k) {
            const OptionDef* d = optionDefs_[k];
            bool now = l.options.getBool(d->key, parseBool(d->defaultValue, false));
            bool ui = checked(optionChecks_[k]);
            if (ui != now) o[d->key] = ui ? "true" : "false";
        }
        refreshLanguage(l);
    }

    void updateDetect() {
        Config tmp;
        tmp.languages = langs_;
        auto s = tmp.buildDetectString([&](const LanguageDef& l) { return isLanguageAvailable(l, ctx_.pluginDir); },
                                       2048);
        setText(item(IDC_LANG_DETECT), s);
    }

    void addLanguage() {
        commit();
        AddLanguageData d{&langs_, {}, {}, {}};
        if (DialogBoxParamW(win::g_instance, MAKEINTRESOURCEW(IDD_ADD_LANGUAGE), hwnd_, addLanguageProc,
                            reinterpret_cast<LPARAM>(&d)) != IDOK)
            return;
        auto l = makeLanguage(d.name, d.extensions);
        if (d.lexer.empty()) {
            if (hasNativeLexer(l.id)) l.options.values["lexer"] = "none";
        } else if (d.lexer != l.id) {
            l.options.values["lexer"] = d.lexer;
        }
        refreshLanguage(l);
        langs_.push_back(std::move(l));
        refreshList(static_cast<int>(langs_.size()) - 1);
    }

    void removeLanguage() {
        if (sel_ < 0) return;
        std::wstring q = L"Remove " + win::widen(langs_[sel_].name) +
                         L" and its options from the INI file?\n(Theme and rules files are not deleted.)";
        if (MessageBoxW(hwnd_, q.c_str(), L"Remove language", MB_ICONQUESTION | MB_YESNO) != IDYES) return;
        langs_.erase(langs_.begin() + sel_);
        int next = sel_;
        sel_ = -1;  // nothing to commit
        refreshList(next);
    }

    ConfigContext ctx_;
    std::vector<LanguageDef> langs_;
    int sel_ = -1;
    std::vector<HWND> optionChecks_;
    std::vector<const OptionDef*> optionDefs_;
};

// ------------------------------------------------------------------ Theme (phase 2: editor)
class ThemePage final : public ConfigPage {
public:
    explicit ThemePage(const ConfigContext& ctx) : ctx_(ctx) {}
    const wchar_t* title() const override { return L"Theme"; }
    int dialogId() const override { return IDD_PAGE_THEME; }
    void init() override {
        SetWindowTextW(item(IDC_THEME_INFO),
                       L"Colors are defined in theme sets: folders under \"themes\" next to the plugin. "
                       L"default.theme holds the base palette; <language>.theme files override it for one language "
                       L"(e.g. python.theme). The active set is chosen on the General page.\n\n"
                       L"A color editor will be added here. Until then, edit the .theme files with any text editor; "
                       L"changes apply the next time a file is opened.");
    }
    void load(const Config&) override {}
    void store(Config&) override {}
    INT_PTR handle(UINT msg, WPARAM wp, LPARAM) override {
        if (msg == WM_COMMAND && LOWORD(wp) == IDC_THEME_OPEN_FOLDER) {
            auto dir = (ctx_.pluginDir / L"themes").wstring();
            ShellExecuteW(hwnd_, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        return FALSE;
    }

private:
    ConfigContext ctx_;
};

}  // namespace

std::unique_ptr<ConfigPage> createGeneralPage(const ConfigContext& ctx) { return std::make_unique<GeneralPage>(ctx); }
std::unique_ptr<ConfigPage> createLanguagesPage(const ConfigContext& ctx) {
    return std::make_unique<LanguagesPage>(ctx);
}
std::unique_ptr<ConfigPage> createThemePage(const ConfigContext& ctx) { return std::make_unique<ThemePage>(ctx); }

}  // namespace shl::ui
