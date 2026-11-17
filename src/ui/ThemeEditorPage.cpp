// Theme tab of the settings window: edit any .theme file.
//
// The file's own entries are edited in place (IniDocument, so comments and
// order stay); everything it does not set is shown as inherited. Inherited
// values come from the built-in palette plus, for <language>.theme files,
// the default.theme of the same set.
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <array>
#include <cstdio>

#include "ConfigDialog.h"
#include "DarkMode.h"
#include "core/IniDocument.h"
#include "core/TextUtil.h"
#include "core/Theme.h"
#include "plugin/WinUtil.h"
#include "resource.h"

namespace shl::ui {

namespace {

namespace fs = std::filesystem;

struct ViewKey {
    const char* key;
    const wchar_t* label;
};
const ViewKey kViewKeys[] = {{"background", L"View background"},
                             {"foreground", L"View foreground (plain text)"},
                             {"infobar_background", L"Notice bar background"},
                             {"infobar_foreground", L"Notice bar text"}};

struct Row {
    bool view;
    const char* key;  // INI key ([View] key or token class name)
    TokenClass cls;
    std::wstring label;
};

std::string hexColor(Color c) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%06X", static_cast<unsigned>(c & 0xFFFFFF));
    return buf;
}

Color viewColor(const Theme& t, const char* key) {
    std::string_view k(key);
    if (k == "background") return t.background;
    if (k == "foreground") return t.foreground;
    if (k == "infobar_background") return t.infobarBackground;
    return t.infobarForeground;
}

class ThemeEditorPage final : public ConfigPage {
public:
    explicit ThemeEditorPage(const ConfigContext& ctx) : ctx_(ctx) {
        for (const auto& v : kViewKeys) rows_.push_back({true, v.key, TokenClass::Default, v.label});
        for (size_t i = 1; i < kTokenClassCount; ++i) {
            auto c = static_cast<TokenClass>(i);
            rows_.push_back({false, tokenClassName(c), c, win::widen(tokenClassName(c))});
        }
    }
    ~ThemeEditorPage() override {
        if (previewFont_) DeleteObject(previewFont_);
    }

    const wchar_t* title() const override { return L"Theme"; }
    int dialogId() const override { return IDD_PAGE_THEME; }

    void init() override {
        HWND list = item(IDC_THEME_LIST);
        RECT unit{0, 0, 4, 12};
        MapDialogRect(hwnd_, &unit);
        SendMessageW(list, LB_SETITEMHEIGHT, 0, unit.bottom);
        for (size_t i = 0; i < rows_.size(); ++i) SendMessageW(list, LB_ADDSTRING, 0, static_cast<LPARAM>(i));
    }

    void load(const Config& cfg) override {
        cfg_ = cfg;
        if (loaded_) return;  // keep the file being edited across Apply
        // Start with the theme file of the current language, else default.theme.
        auto set = ctx_.themeSet.empty() ? cfg.themeSet(ctx_.dark) : ctx_.themeSet;
        auto dir = ctx_.pluginDir / "themes" / utf8Path(set);
        std::error_code ec;
        fs::path start = dir / "default.theme";
        if (!ctx_.languageId.empty() && fs::exists(dir / utf8Path(ctx_.languageId + ".theme"), ec))
            start = dir / utf8Path(ctx_.languageId + ".theme");
        if (fs::exists(start, ec)) openFile(start);
        else showSelection();
    }

    void store(Config&) override {}

    bool saveExternal(std::wstring& err) override {
        if (!dirty_) return true;
        if (!doc_.saveFile(file_)) {
            err = L"Could not write\n" + file_.wstring();
            return false;
        }
        dirty_ = false;
        updateFileLabel();
        return true;
    }

    INT_PTR handle(UINT msg, WPARAM wp, LPARAM lp) override {
        switch (msg) {
            case WM_DRAWITEM: return draw(reinterpret_cast<DRAWITEMSTRUCT*>(lp));
            case WM_COMMAND: return command(LOWORD(wp), HIWORD(wp));
        }
        return FALSE;
    }

private:
    // ------------------------------------------------------------ file
    void openFile(const fs::path& p) {
        IniDocument doc;
        std::error_code ec;
        if (fs::exists(p, ec) && !IniDocument::loadFile(p, doc, win::ansiConverter())) {
            MessageBoxW(hwnd_, (L"Could not read\n" + p.wstring()).c_str(), L"Theme", MB_ICONERROR | MB_OK);
            return;
        }
        file_ = p;
        doc_ = std::move(doc);
        dirty_ = false;
        loaded_ = true;
        // Inherited values: built-in palette (+ default.theme of the same set).
        auto setName = pathToUtf8(p.parent_path().filename());
        bool dark = iequals(setName, cfg_.themeDark) || toLowerAscii(setName).find("dark") != std::string::npos;
        base_ = Theme::builtin(dark);
        baseName_ = dark ? L"the built-in dark palette" : L"the built-in light palette";
        auto def = p.parent_path() / "default.theme";
        if (!iequals(pathToUtf8(p.filename()), "default.theme") && fs::exists(def, ec)) {
            IniDocument d;
            if (IniDocument::loadFile(def, d, win::ansiConverter())) {
                base_.apply(d);
                baseName_ = L"default.theme";
            }
        }
        updateFileLabel();
        InvalidateRect(item(IDC_THEME_LIST), nullptr, TRUE);
        if (SendMessageW(item(IDC_THEME_LIST), LB_GETCURSEL, 0, 0) == LB_ERR)
            SendMessageW(item(IDC_THEME_LIST), LB_SETCURSEL, 4 + static_cast<WPARAM>(TokenClass::Keyword) - 1, 0);
        showSelection();
    }

    void updateFileLabel() {
        std::wstring s = file_.empty() ? L"(no file loaded)" : file_.wstring();
        if (dirty_) s = L"* " + s;
        SetWindowTextW(item(IDC_THEME_FILE), s.c_str());
    }

    void chooseFile() {
        if (dirty_) {
            int r = MessageBoxW(hwnd_, L"Save changes to the current theme file first?", L"Theme",
                                MB_ICONQUESTION | MB_YESNOCANCEL);
            if (r == IDCANCEL) return;
            if (r == IDYES) {
                std::wstring err;
                if (!saveExternal(err)) {
                    MessageBoxW(hwnd_, err.c_str(), L"Theme", MB_ICONERROR | MB_OK);
                    return;
                }
            }
        }
        wchar_t buf[MAX_PATH] = {};
        std::wstring initial = (file_.empty() ? ctx_.pluginDir / "themes" : file_.parent_path()).wstring();
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof ofn;
        ofn.hwndOwner = hwnd_;
        ofn.lpstrFilter = L"Theme files (*.theme)\0*.theme\0All files (*.*)\0*.*\0";
        ofn.lpstrFile = buf;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrInitialDir = initial.c_str();
        ofn.lpstrDefExt = L"theme";
        ofn.lpstrTitle = L"Open or create a theme file";
        // A new file name creates a new (empty) theme file on save.
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&ofn)) return;
        openFile(fs::path(buf));
    }

    // ------------------------------------------------------------ model
    Theme resolved() const {
        Theme t = base_;
        t.apply(doc_);
        t.finalize();
        return t;
    }

    int selectedRow() const {
        auto i = static_cast<int>(SendMessageW(item(IDC_THEME_LIST), LB_GETCURSEL, 0, 0));
        return i < 0 || i >= static_cast<int>(rows_.size()) ? -1 : i;
    }

    std::optional<std::string> ownValue(const Row& r) const { return doc_.get(r.view ? "View" : "Tokens", r.key); }

    // Fields of the file's own token entry ("" = inherited). An @alias entry
    // is materialised from the resolved style on first edit.
    std::array<std::string, 3> tokenParts(const Row& r) const {
        std::array<std::string, 3> parts;
        auto v = ownValue(r);
        if (!v) return parts;
        auto t = trim(*v);
        if (!t.empty() && t[0] == '@') {
            Theme rt = resolved();
            const Style& s = rt.style(r.cls);
            parts[0] = hexColor(s.fg);
            parts[1] = s.bg ? hexColor(*s.bg) : "none";
            parts[2] = flagsText(s.flags);
            return parts;
        }
        size_t i = 0, pos = 0;
        while (i < 3) {
            size_t c = t.find(',', pos);
            parts[i++] = std::string(trim(t.substr(pos, c == std::string_view::npos ? t.npos : c - pos)));
            if (c == std::string_view::npos) break;
            pos = c + 1;
        }
        return parts;
    }

    static std::string flagsText(uint8_t f) {
        std::string s;
        if (f & kBold) s += "bold";
        if (f & kItalic) s += std::string(s.empty() ? "" : "+") + "italic";
        if (f & kUnderline) s += std::string(s.empty() ? "" : "+") + "underline";
        return s.empty() ? "normal" : s;
    }

    void writeToken(const Row& r, const std::array<std::string, 3>& p) {
        std::string v = p[0];
        if (!p[2].empty()) v += "," + p[1] + "," + p[2];
        else if (!p[1].empty()) v += "," + p[1];
        if (v.empty()) doc_.remove("Tokens", r.key);
        else doc_.set("Tokens", r.key, v);
        changed();
    }

    void changed() {
        dirty_ = true;
        updateFileLabel();
        InvalidateRect(item(IDC_THEME_LIST), nullptr, FALSE);
        showSelection();
    }

    // ------------------------------------------------------------ UI
    void showSelection() {
        int i = selectedRow();
        bool have = loaded_ && i >= 0;
        const Row* r = have ? &rows_[i] : nullptr;
        Theme t = resolved();
        SetWindowTextW(item(IDC_THEME_ITEM_GROUP), r ? r->label.c_str() : L"");
        EnableWindow(item(IDC_THEME_FG), have);
        bool token = have && !r->view;
        for (int id : {IDC_THEME_BG, IDC_THEME_BG_NONE, IDC_THEME_BOLD, IDC_THEME_ITALIC, IDC_THEME_UNDERLINE})
            EnableWindow(item(id), token);
        EnableWindow(item(IDC_THEME_RESET), have && ownValue(*r).has_value());
        if (token) {
            const Style& s = t.style(r->cls);
            SendMessageW(item(IDC_THEME_BG_NONE), BM_SETCHECK, s.bg ? BST_UNCHECKED : BST_CHECKED, 0);
            SendMessageW(item(IDC_THEME_BOLD), BM_SETCHECK, (s.flags & kBold) ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(item(IDC_THEME_ITALIC), BM_SETCHECK, (s.flags & kItalic) ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(item(IDC_THEME_UNDERLINE), BM_SETCHECK, (s.flags & kUnderline) ? BST_CHECKED : BST_UNCHECKED,
                         0);
        }
        std::wstring state;
        if (have) {
            auto own = ownValue(*r);
            if (!own) {
                state = L"Not set in this file: inherited from " + baseName_;
                if (!r->view && parentClass(r->cls) != TokenClass::Count)
                    state += L" (or from \"" + win::widen(tokenClassName(parentClass(r->cls))) + L"\")";
                state += L". Changing it adds an entry here.";
            } else if (!trim(*own).empty() && trim(*own)[0] == '@') {
                state = L"Same look as \"" + win::widen(std::string(trim(*own).substr(1))) +
                        L"\" (" + win::widen(*own) + L"). Changing it replaces the link.";
            } else {
                state = L"Set in this file: " + win::widen(*own);
            }
        }
        SetWindowTextW(item(IDC_THEME_STATE), state.c_str());
        for (int id : {IDC_THEME_FG, IDC_THEME_BG, IDC_THEME_PREVIEW}) InvalidateRect(item(id), nullptr, FALSE);
    }

    bool pickColor(Color& c) {
        static COLORREF custom[16] = {};
        CHOOSECOLORW cc{};
        cc.lStructSize = sizeof cc;
        cc.hwndOwner = hwnd_;
        cc.rgbResult = win::toColorRef(c);
        cc.lpCustColors = custom;
        cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
        if (!ChooseColorW(&cc)) return false;
        c = (static_cast<Color>(GetRValue(cc.rgbResult)) << 16) | (static_cast<Color>(GetGValue(cc.rgbResult)) << 8) |
            GetBValue(cc.rgbResult);
        return true;
    }

    INT_PTR command(int id, int code) {
        switch (id) {
            case IDC_THEME_LOAD: chooseFile(); return TRUE;
            case IDC_THEME_OPEN_FOLDER: {
                auto dir = (ctx_.pluginDir / L"themes").wstring();
                ShellExecuteW(hwnd_, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
            case IDC_THEME_LIST:
                if (code == LBN_SELCHANGE) showSelection();
                return TRUE;
        }
        int i = selectedRow();
        if (!loaded_ || i < 0) return FALSE;
        const Row& r = rows_[i];
        Theme t = resolved();
        switch (id) {
            case IDC_THEME_FG: {
                Color c = r.view ? viewColor(t, r.key) : t.style(r.cls).fg;
                if (!pickColor(c)) return TRUE;
                if (r.view) {
                    doc_.set("View", r.key, hexColor(c));
                    changed();
                } else {
                    auto p = tokenParts(r);
                    p[0] = hexColor(c);
                    writeToken(r, p);
                }
                return TRUE;
            }
            case IDC_THEME_BG: {
                const Style& s = t.style(r.cls);
                Color c = s.bg ? *s.bg : t.background;
                if (!pickColor(c)) return TRUE;
                auto p = tokenParts(r);
                p[1] = hexColor(c);
                writeToken(r, p);
                return TRUE;
            }
            case IDC_THEME_BG_NONE: {
                auto p = tokenParts(r);
                bool none = SendMessageW(item(IDC_THEME_BG_NONE), BM_GETCHECK, 0, 0) == BST_CHECKED;
                p[1] = none ? "none" : hexColor(t.background);
                writeToken(r, p);
                return TRUE;
            }
            case IDC_THEME_BOLD:
            case IDC_THEME_ITALIC:
            case IDC_THEME_UNDERLINE: {
                uint8_t f = 0;
                if (SendMessageW(item(IDC_THEME_BOLD), BM_GETCHECK, 0, 0) == BST_CHECKED) f |= kBold;
                if (SendMessageW(item(IDC_THEME_ITALIC), BM_GETCHECK, 0, 0) == BST_CHECKED) f |= kItalic;
                if (SendMessageW(item(IDC_THEME_UNDERLINE), BM_GETCHECK, 0, 0) == BST_CHECKED) f |= kUnderline;
                auto p = tokenParts(r);
                p[2] = flagsText(f);
                writeToken(r, p);
                return TRUE;
            }
            case IDC_THEME_RESET:
                doc_.remove(r.view ? "View" : "Tokens", r.key);
                changed();
                return TRUE;
        }
        return FALSE;
    }

    // ------------------------------------------------------------ drawing
    HFONT previewFont(uint8_t flags) {
        if (previewFont_ && previewFlags_ == flags) return previewFont_;
        if (previewFont_) DeleteObject(previewFont_);
        HDC dc = GetDC(hwnd_);
        int h = -MulDiv(cfg_.fontSize, GetDeviceCaps(dc, LOGPIXELSY), 72);
        ReleaseDC(hwnd_, dc);
        previewFont_ = CreateFontW(h, 0, 0, 0, (flags & kBold) ? FW_BOLD : FW_NORMAL, (flags & kItalic) != 0,
                                   (flags & kUnderline) != 0, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
                                   win::widen(cfg_.fontFace).c_str());
        previewFlags_ = flags;
        return previewFont_;
    }

    static void fill(HDC dc, const RECT& r, COLORREF c) {
        HBRUSH b = CreateSolidBrush(c);
        FillRect(dc, &r, b);
        DeleteObject(b);
    }

    INT_PTR draw(DRAWITEMSTRUCT* d) {
        Theme t = resolved();
        COLORREF windowBg = dark_ ? dark::palette().window : GetSysColor(COLOR_WINDOW);
        COLORREF text = dark_ ? dark::palette().text : GetSysColor(COLOR_WINDOWTEXT);
        if (d->CtlID == IDC_THEME_LIST) {
            if (d->itemID == static_cast<UINT>(-1)) return TRUE;
            const Row& r = rows_[d->itemData < rows_.size() ? d->itemData : 0];
            bool sel = (d->itemState & ODS_SELECTED) != 0;
            COLORREF bg = sel ? (dark_ ? dark::palette().selected : GetSysColor(COLOR_HIGHLIGHT))
                              : (dark_ ? dark::palette().control : GetSysColor(COLOR_WINDOW));
            fill(d->hDC, d->rcItem, bg);
            RECT sw = d->rcItem;
            int h = sw.bottom - sw.top;
            sw.left += 3;
            sw.right = sw.left + h * 2;
            InflateRect(&sw, 0, -2);
            if (loaded_) {
                Color fg = r.view ? viewColor(t, r.key) : t.style(r.cls).fg;
                Color back = r.view ? viewColor(t, r.key) : (t.style(r.cls).bg ? *t.style(r.cls).bg : t.background);
                fill(d->hDC, sw, win::toColorRef(back));
                RECT inner = sw;
                InflateRect(&inner, -(h / 3), -(h / 5));
                if (!r.view) fill(d->hDC, inner, win::toColorRef(fg));
                FrameRect(d->hDC, &sw, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
            }
            RECT tr = d->rcItem;
            tr.left = sw.right + 6;
            SetBkMode(d->hDC, TRANSPARENT);
            SetTextColor(d->hDC, sel && !dark_ ? GetSysColor(COLOR_HIGHLIGHTTEXT) : text);
            std::wstring label = r.label;
            if (loaded_ && ownValue(r)) label += L"  \u2022";  // set in this file
            DrawTextW(d->hDC, label.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            if (d->itemState & ODS_FOCUS) DrawFocusRect(d->hDC, &d->rcItem);
            return TRUE;
        }
        int i = selectedRow();
        const Row* r = (loaded_ && i >= 0) ? &rows_[i] : nullptr;
        if (d->CtlID == IDC_THEME_FG || d->CtlID == IDC_THEME_BG) {
            bool enabled = IsWindowEnabled(d->hwndItem) != FALSE;
            fill(d->hDC, d->rcItem, dark_ ? dark::palette().control : GetSysColor(COLOR_BTNFACE));
            RECT sw = d->rcItem;
            InflateRect(&sw, -3, -3);
            std::wstring caption = L"";
            if (r && enabled) {
                Color c;
                if (d->CtlID == IDC_THEME_FG) c = r->view ? viewColor(t, r->key) : t.style(r->cls).fg;
                else c = t.style(r->cls).bg ? *t.style(r->cls).bg : t.background;
                RECT box = sw;
                box.right = box.left + (box.bottom - box.top) * 2;
                fill(d->hDC, box, win::toColorRef(c));
                FrameRect(d->hDC, &box, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
                sw.left = box.right + 4;
                caption = win::widen(hexColor(c));
                if (d->CtlID == IDC_THEME_BG && !t.style(r->cls).bg) caption = L"(view)";
            }
            SetBkMode(d->hDC, TRANSPARENT);
            SetTextColor(d->hDC, enabled ? text : GetSysColor(COLOR_GRAYTEXT));
            DrawTextW(d->hDC, caption.c_str(), -1, &sw, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            FrameRect(d->hDC, &d->rcItem,
                      static_cast<HBRUSH>(GetStockObject((d->itemState & ODS_FOCUS) ? BLACK_BRUSH : GRAY_BRUSH)));
            return TRUE;
        }
        if (d->CtlID == IDC_THEME_PREVIEW) {
            fill(d->hDC, d->rcItem, r ? win::toColorRef(t.background) : windowBg);
            if (!r) return TRUE;
            Style s;
            if (r->view) {
                bool infobar = std::string_view(r->key).rfind("infobar", 0) == 0;
                if (infobar) {
                    fill(d->hDC, d->rcItem, win::toColorRef(t.infobarBackground));
                    s.fg = t.infobarForeground;
                } else {
                    s.fg = t.foreground;
                }
            } else {
                s = t.style(r->cls);
            }
            RECT tr = d->rcItem;
            InflateRect(&tr, -6, 0);
            HGDIOBJ old = SelectObject(d->hDC, previewFont(s.flags));
            const wchar_t* sample = L"Sample text 123";
            if (s.bg) {
                SetBkMode(d->hDC, OPAQUE);
                SetBkColor(d->hDC, win::toColorRef(*s.bg));
            } else {
                SetBkMode(d->hDC, TRANSPARENT);
            }
            SetTextColor(d->hDC, win::toColorRef(s.fg));
            DrawTextW(d->hDC, sample, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            SelectObject(d->hDC, old);
            FrameRect(d->hDC, &d->rcItem, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
            return TRUE;
        }
        return FALSE;
    }

    ConfigContext ctx_;
    Config cfg_;
    std::vector<Row> rows_;
    fs::path file_;
    IniDocument doc_;
    Theme base_;
    std::wstring baseName_;
    bool loaded_ = false;
    bool dirty_ = false;
    HFONT previewFont_ = nullptr;
    uint8_t previewFlags_ = 0xFF;
};

}  // namespace

std::unique_ptr<ConfigPage> createThemePage(const ConfigContext& ctx) { return std::make_unique<ThemeEditorPage>(ctx); }

}  // namespace shl::ui
