#include "ListerWindow.h"

#include <commdlg.h>  // FR_* flags
#include <richedit.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>

#include "core/IniDocument.h"
#include "core/LexerFactory.h"
#include "core/Settings.h"
#include "core/TextDecoder.h"
#include "core/TextUtil.h"
#include "plugin/WinUtil.h"
#include "plugin/listplug.h"
#include "ui/ConfigDialog.h"
#include "ui/GotoLineDialog.h"
#include "ui/resource.h"

namespace shl::view {

namespace {
const wchar_t kContainerClass[] = L"TclSyntaxHL_Lister";
const wchar_t kPropSelf[] = L"TclSyntaxHL_Self";
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTimerMs = 100;
constexpr size_t kLazyLineMarginBefore = 30, kLazyLineMarginAfter = 60;
constexpr LONG kLazyCharWindow = 32 * 1024;
constexpr size_t kMaxPendingTokens = 2'000'000;

bool ensureRichEdit() {
    static HMODULE mod = LoadLibraryW(L"Msftedit.dll");
    return mod != nullptr;
}

bool registerContainerClass() {
    static ATOM atom = 0;
    if (atom) return true;
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = win::g_instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kContainerClass;
    atom = RegisterClassExW(&wc);
    return atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

std::wstring classNameOf(HWND h) {
    wchar_t buf[128] = {};
    GetClassNameW(h, buf, 127);
    return buf;
}
}  // namespace

ListerWindow* ListerWindow::fromHwnd(HWND h) {
    if (!h) return nullptr;
    return static_cast<ListerWindow*>(GetPropW(h, kPropSelf));
}

ListerWindow* ListerWindow::create(HWND parent, const std::wstring& file, int showFlags) {
    if (!ensureRichEdit() || !registerContainerClass()) return nullptr;
    auto* w = new ListerWindow();
    if (!w->createWindows(parent)) {
        delete w;
        return nullptr;
    }
    if (!w->load(file, showFlags)) {
        DestroyWindow(w->hwnd_);  // deletes w in WM_NCDESTROY
        return nullptr;
    }
    ShowWindow(w->hwnd_, SW_SHOW);
    if (!w->quickView_) SetFocus(w->edit_);
    return w;
}

bool ListerWindow::createWindows(HWND parent) {
    lister_ = parent;
    quickView_ = classNameOf(parent) != L"TLister";
    RECT rc{};
    GetClientRect(parent, &rc);
    hwnd_ = CreateWindowExW(0, kContainerClass, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, rc.right, rc.bottom, parent,
                            nullptr, win::g_instance, nullptr);
    if (!hwnd_) return false;
    SetPropW(hwnd_, kPropSelf, this);
    SetWindowLongPtrW(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ListerWindow::containerProc));

    if (!info_.create(hwnd_)) return false;
    info_.onClosed = [this] { layout(); };

    edit_ = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY |
                                ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL,
                            0, 0, rc.right, rc.bottom, hwnd_, nullptr, win::g_instance, nullptr);
    if (!edit_) return false;
    SetPropW(edit_, kPropSelf, this);
    origEditProc_ = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(edit_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ListerWindow::editProc)));
    SendMessageW(edit_, EM_SETUNDOLIMIT, 0, 0);
    SendMessageW(edit_, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
    SendMessageW(edit_, EM_AUTOURLDETECT, FALSE, 0);
    SendMessageW(edit_, EM_SETEVENTMASK, 0, 0);
    SetTimer(hwnd_, kTimerId, kTimerMs, nullptr);
    return true;
}

bool ListerWindow::load(const std::wstring& file, int showFlags) {
    namespace fs = std::filesystem;
    const auto& ansi = win::ansiConverter();
    auto dir = win::pluginDir();
    auto ini = win::iniPath();

    // --- everything that can fail happens before the view is touched ---
    Config cfg;
    IniDocument doc;
    cfg = IniDocument::loadFile(ini, doc, ansi) ? Config::fromIni(doc) : Config::defaults();
    const LanguageDef* lang = cfg.languageForFile(fs::path(file));
    if (!lang) return false;
    auto sel = selectLexer(cfg, *lang, dir, ansi);
    if (!sel.lexer) return false;
    std::string bytes;
    uint64_t size = 0;
    if (!win::readFile(file, bytes, size)) return false;
    auto decoded = decodeText(bytes, ansi);
    bytes.clear();
    bytes.shrink_to_fit();

    // --- commit ---
    lazy_.reset();
    pending_.clear();
    file_ = file;
    showFlags_ = showFlags;
    cfg_ = std::move(cfg);
    lang = cfg_.languageForFile(fs::path(file));
    lexer_ = std::move(sel.lexer);
    text_ = normalizeLineEndings(decoded.text);
    decoded.text.clear();
    decoded.text.shrink_to_fit();

    lineStarts_.clear();
    lineStarts_.push_back(0);
    for (size_t i = 0; i < text_.size(); ++i)
        if (text_[i] == u'\r' && i + 1 < text_.size()) lineStarts_.push_back(static_cast<uint32_t>(i + 1));

    dark_ = (showFlags & lcp_darkmode) != 0;
    theme_ = loadTheme(dir, cfg_.themeSet(dark_), lang->id, dark_, ansi);
    font_.face = win::widen(cfg_.fontFace);
    font_.pointSize = cfg_.fontSize;
    font_.tabTwips = measureCharTwips(edit_, font_) * cfg_.tabWidth;

    std::vector<std::wstring> notices;
    if (size <= cfg_.maxHighlightSize) mode_ = RenderMode::Full;
    else if (cfg_.useLazyHighlight) mode_ = RenderMode::Lazy;
    else {
        mode_ = RenderMode::Plain;
        notices.push_back(L"Syntax highlighting disabled: file is " + win::widen(formatSize(size)) + L", limit is " +
                          win::widen(formatSize(cfg_.maxHighlightSize)) + L" (max_highlight_size in " +
                          ini.filename().wstring() + L").");
    }
    if (!sel.notice.empty()) notices.push_back(win::widen(sel.notice));

    // Size/wrap first: re-laying out a big formatted document afterwards is slow.
    info_.setColors(win::toColorRef(theme_.infobarBackground), win::toColorRef(theme_.infobarForeground));
    {
        std::wstring joined;
        for (auto& n : notices) joined += (joined.empty() ? L"" : L"   |   ") + n;
        info_.setText(joined);
    }
    layout();
    SendMessageW(edit_, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L""));
    applyWrap((showFlags & lcp_wraptext) != 0);

    SendMessageW(edit_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(edit_, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(win::toColorRef(theme_.background)));
    SendMessageW(edit_, EM_SETZOOM, 0, 0);
    if (mode_ == RenderMode::Full) {
        std::vector<Token> tokens;
        LexState st;
        lexer_->lex(text_, st, tokens);
        auto rtf = buildRtf(text_, tokens, theme_, font_);
        tokens.clear();
        tokens.shrink_to_fit();
        if (!streamRtf(edit_, rtf)) setPlainText(edit_, text_, theme_, font_);
    } else {
        setPlainText(edit_, text_, theme_, font_);
    }
    SendMessageW(edit_, EM_SETSEL, 0, 0);
    SendMessageW(edit_, WM_SETREDRAW, TRUE, 0);

    SetWindowTheme(edit_, dark_ ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    InvalidateRect(edit_, nullptr, TRUE);

    if (mode_ == RenderMode::Lazy) lazy_ = std::make_unique<LazyModel>(text_, *lexer_);
    lastPercent_ = -1;
    lastTopCp_ = lastBottomCp_ = -1;
    viewChanged();
    return true;
}

void ListerWindow::layout() {
    if (!hwnd_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int ih = info_.height();
    info_.move(0, 0, w);
    MoveWindow(edit_, 0, ih, w, std::max(0, h - ih), TRUE);
}

void ListerWindow::applyWrap(bool wrap) {
    wrap_ = wrap;
    SendMessageW(edit_, EM_SETTARGETDEVICE, 0, wrap ? 0 : 1);
    ShowScrollBar(edit_, SB_HORZ, !wrap);
}

// Note: RichEdit scroll positions (EM_GETSCROLLPOS, scrollbar range) are
// 16-bit and wrap on tall documents, so all positioning here is done with
// character positions and line numbers instead.
LONG ListerWindow::topCp() const {
    POINTL p{0, 0};
    return static_cast<LONG>(SendMessageW(edit_, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&p)));
}

LONG ListerWindow::bottomCp() const {
    RECT rc;
    GetClientRect(edit_, &rc);
    POINTL p{rc.right > 0 ? rc.right - 1 : 0, rc.bottom > 0 ? rc.bottom - 1 : 0};
    return static_cast<LONG>(SendMessageW(edit_, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&p)));
}

void ListerWindow::scrollTopToDisplayLine(LONG displayLine) {
    // EM_LINESCROLL is exact on Windows; iterate anyway in case an
    // implementation scrolls by an approximate pixel amount.
    for (int i = 0; i < 8; ++i) {
        LONG first = static_cast<LONG>(SendMessageW(edit_, EM_GETFIRSTVISIBLELINE, 0, 0));
        if (first == displayLine) return;
        SendMessageW(edit_, EM_LINESCROLL, 0, displayLine - first);
        if (static_cast<LONG>(SendMessageW(edit_, EM_GETFIRSTVISIBLELINE, 0, 0)) == first) return;  // at a limit
    }
}

void ListerWindow::viewChanged() {
    paintVisible();
    reportPercent();
    lastTopCp_ = topCp();
    lastBottomCp_ = bottomCp();
}

void ListerWindow::paintVisible() {
    if (!lazy_) return;
    LONG cpTop = topCp(), cpBottom = bottomCp();
    if (cpBottom < cpTop) std::swap(cpTop, cpBottom);
    size_t first = lazy_->lineOfOffset(static_cast<size_t>(std::max<LONG>(cpTop, 0)));
    size_t last = lazy_->lineOfOffset(static_cast<size_t>(std::max<LONG>(cpBottom, 0)));
    first = first > kLazyLineMarginBefore ? first - kLazyLineMarginBefore : 0;
    last += kLazyLineMarginAfter;

    auto& pending = pending_;
    auto fresh = lazy_->takeTokens(first, last);
    if (!fresh.empty()) {
        size_t mid = pending.size();
        pending.insert(pending.end(), fresh.begin(), fresh.end());
        std::inplace_merge(pending.begin(), pending.begin() + static_cast<ptrdiff_t>(mid), pending.end(),
                           [](const Token& a, const Token& b) { return a.start < b.start; });
    }
    if (pending.empty()) return;
    // Apply only tokens near the visible character window; keep the rest
    // (only relevant for extremely long lines) for later scrolling.
    LONG lo = std::max<LONG>(0, cpTop - kLazyCharWindow), hi = cpBottom + kLazyCharWindow;
    auto cmp = [](const Token& t, LONG v) { return static_cast<LONG>(t.start + t.length) <= v; };
    auto b = std::lower_bound(pending.begin(), pending.end(), lo, cmp);
    auto e = b;
    while (e != pending.end() && static_cast<LONG>(e->start) < hi) ++e;
    if (b != e) {
        std::vector<Token> now(b, e);
        pending.erase(b, e);
        applyTokens(edit_, now, theme_);
    }
    if (pending.size() > kMaxPendingTokens) pending.clear();  // pathological input: give up on the rest
}

void ListerWindow::reportPercent() {
    size_t total = lineStarts_.size();
    size_t top = lineOfOffset(static_cast<size_t>(std::max<LONG>(topCp(), 0)));
    size_t bottom = lineOfOffset(static_cast<size_t>(std::max<LONG>(bottomCp(), 0)));
    size_t visible = bottom >= top ? bottom - top + 1 : 1;
    size_t range = total > visible ? total - visible : 0;
    int pct = range > 0 ? static_cast<int>(std::min<size_t>(100, top * 100 / range)) : 0;
    if (pct == lastPercent_) return;
    lastPercent_ = pct;
    PostMessageW(lister_, WM_COMMAND, MAKEWPARAM(pct, itm_percent), reinterpret_cast<LPARAM>(hwnd_));
}

void ListerWindow::setPercent(int pct) {
    size_t total = lineStarts_.size();
    size_t top = lineOfOffset(static_cast<size_t>(std::max<LONG>(topCp(), 0)));
    size_t bottom = lineOfOffset(static_cast<size_t>(std::max<LONG>(bottomCp(), 0)));
    size_t visible = bottom >= top ? bottom - top + 1 : 1;
    size_t range = total > visible ? total - visible : 0;
    if (pct <= 0 || pct >= 100) {
        SendMessageW(edit_, WM_VSCROLL, pct <= 0 ? SB_TOP : SB_BOTTOM, 0);
        viewChanged();
        return;
    }
    size_t target = range * static_cast<size_t>(pct) / 100;
    LONG displayLine = static_cast<LONG>(SendMessageW(edit_, EM_EXLINEFROMCHAR, 0, lineStarts_[target]));
    scrollTopToDisplayLine(displayLine);
    viewChanged();
}

size_t ListerWindow::lineOfOffset(size_t off) const {
    auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), static_cast<uint32_t>(off));
    return it == lineStarts_.begin() ? 0 : static_cast<size_t>(it - lineStarts_.begin()) - 1;
}

size_t ListerWindow::currentLine() const {
    CHARRANGE cr{};
    SendMessageW(edit_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    return lineOfOffset(static_cast<size_t>(std::max<LONG>(cr.cpMin, 0))) + 1;
}

void ListerWindow::scrollToCenter(LONG cp) {
    LONG first = static_cast<LONG>(SendMessageW(edit_, EM_GETFIRSTVISIBLELINE, 0, 0));
    LONG last = static_cast<LONG>(SendMessageW(edit_, EM_EXLINEFROMCHAR, 0, bottomCp()));
    LONG visible = std::max<LONG>(1, last - first + 1);
    LONG line = static_cast<LONG>(SendMessageW(edit_, EM_EXLINEFROMCHAR, 0, cp));
    scrollTopToDisplayLine(std::max<LONG>(0, line - visible / 2));
}

void ListerWindow::gotoLine() {
    auto t = ui::showGotoLineDialog(hwnd_, currentLine(), lineStarts_.size());
    if (!t) return;
    size_t start = lineStarts_[t->line - 1];
    size_t end = t->line < lineStarts_.size() ? lineStarts_[t->line] - 1 : text_.size();
    if (end == text_.size() && end > start && text_[end - 1] == u'\r') --end;
    CHARRANGE cr;
    if (t->column > 0) {
        LONG cp = static_cast<LONG>(start + std::min(t->column - 1, end - start));
        cr = CHARRANGE{cp, cp};
    } else {
        cr = CHARRANGE{static_cast<LONG>(start), static_cast<LONG>(end)};
    }
    SendMessageW(edit_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    SendMessageW(edit_, EM_SCROLLCARET, 0, 0);
    scrollToCenter(cr.cpMin);
    viewChanged();
    SetFocus(edit_);
}

int ListerWindow::search(const std::wstring& text, int flags) {
    if (text.empty()) return LISTPLUGIN_ERROR;
    CHARRANGE cur{};
    SendMessageW(edit_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cur));
    bool backwards = (flags & lcs_backwards) != 0;
    bool first = (flags & lcs_findfirst) != 0;
    FINDTEXTEXW ft{};
    ft.lpstrText = text.c_str();
    LONG textLen = static_cast<LONG>(text_.size());
    if (backwards) {
        ft.chrg.cpMin = first ? textLen : cur.cpMin;
        ft.chrg.cpMax = 0;
    } else {
        ft.chrg.cpMin = first ? 0 : cur.cpMax;
        ft.chrg.cpMax = -1;
    }
    WPARAM f = backwards ? 0 : FR_DOWN;
    if (flags & lcs_matchcase) f |= FR_MATCHCASE;
    if (flags & lcs_wholewords) f |= FR_WHOLEWORD;
    LRESULT r = SendMessageW(edit_, EM_FINDTEXTEXW, f, reinterpret_cast<LPARAM>(&ft));
    if (r < 0) return LISTPLUGIN_ERROR;
    SendMessageW(edit_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&ft.chrgText));
    SendMessageW(edit_, EM_SCROLLCARET, 0, 0);
    viewChanged();
    return LISTPLUGIN_OK;
}

int ListerWindow::command(int cmd, int param) {
    switch (cmd) {
        case lc_copy:
            SendMessageW(edit_, WM_COPY, 0, 0);
            return LISTPLUGIN_OK;
        case lc_selectall:
            SendMessageW(edit_, EM_SETSEL, 0, -1);
            return LISTPLUGIN_OK;
        case lc_setpercent:
            setPercent(param);
            return LISTPLUGIN_OK;
        case lc_newparams: {
            bool dark = (param & lcp_darkmode) != 0;
            if (dark != dark_) {
                int pct = lastPercent_;
                if (!load(file_, param)) return LISTPLUGIN_ERROR;
                if (pct > 0) setPercent(pct);
            } else {
                showFlags_ = param;
                applyWrap((param & lcp_wraptext) != 0);
                viewChanged();
            }
            return LISTPLUGIN_OK;
        }
    }
    return LISTPLUGIN_ERROR;
}

void ListerWindow::showContextMenu(int x, int y) {
    HMENU m = CreatePopupMenu();
    CHARRANGE cr{};
    SendMessageW(edit_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    AppendMenuW(m, MF_STRING | (cr.cpMax > cr.cpMin ? 0 : MF_GRAYED), IDM_COPY, L"&Copy\tCtrl+C");
    AppendMenuW(m, MF_STRING, IDM_SELECTALL, L"Select &all\tCtrl+A");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_GOTO, L"&Go to line...\tCtrl+G");
    if (cfg_.enableConfigDialog) {
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, IDM_SETTINGS, L"&Settings...");
    }
    if (x == -1 && y == -1) {  // keyboard invoked: use caret position
        POINTL p{};
        SendMessageW(edit_, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&p), cr.cpMin);
        POINT pt{p.x, p.y};
        ClientToScreen(edit_, &pt);
        x = pt.x;
        y = pt.y;
    }
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, hwnd_, nullptr);
    DestroyMenu(m);
    switch (cmd) {
        case IDM_COPY: SendMessageW(edit_, WM_COPY, 0, 0); break;
        case IDM_SELECTALL: SendMessageW(edit_, EM_SETSEL, 0, -1); break;
        case IDM_GOTO: gotoLine(); break;
        case IDM_SETTINGS: openSettings(); break;
    }
}

void ListerWindow::openSettings() {
    Settings s;
    s.load(win::iniPath(), win::ansiConverter());
    ui::ConfigDialog dlg(s);
    if (dlg.show(hwnd_)) load(file_, showFlags_);
}

LRESULT CALLBACK ListerWindow::containerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = fromHwnd(hwnd);
    if (self) {
        try {
            switch (msg) {
                case WM_SIZE: self->layout(); self->viewChanged(); return 0;
                case WM_SETFOCUS: SetFocus(self->edit_); return 0;
                case WM_ERASEBKGND: return 1;
                case WM_TIMER:
                    if (wp == kTimerId) {
                        if (self->topCp() != self->lastTopCp_ || self->bottomCp() != self->lastBottomCp_)
                            self->viewChanged();
                        return 0;
                    }
                    break;
                case WM_DESTROY:
                    KillTimer(hwnd, kTimerId);
                    break;
                case WM_NCDESTROY: {
                    RemovePropW(hwnd, kPropSelf);
                    if (self->edit_) RemovePropW(self->edit_, kPropSelf);
                    self->hwnd_ = nullptr;
                    delete self;
                    return DefWindowProcW(hwnd, msg, wp, lp);
                }
            }
        } catch (...) {
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK ListerWindow::editProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = fromHwnd(hwnd);
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    WNDPROC orig = self->origEditProc_;
    bool handled = false;
    LRESULT r = 0;
    try {
        r = self->handleEditMessage(hwnd, msg, wp, lp, handled);
    } catch (...) {
        handled = false;
    }
    if (msg == WM_NCDESTROY) {
        RemovePropW(hwnd, kPropSelf);
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
    }
    return handled ? r : CallWindowProcW(orig, hwnd, msg, wp, lp);
}

LRESULT ListerWindow::handleEditMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool& handled) {
    switch (msg) {
        case WM_KEYDOWN: {
            bool ctrl = GetKeyState(VK_CONTROL) < 0;
            bool alt = GetKeyState(VK_MENU) < 0;
            if (ctrl && !alt && wp == 'G') {
                handled = true;
                gotoLine();
                return 0;
            }
            if (ctrl && !alt && (wp == 'C' || wp == 'A' || wp == VK_INSERT)) return 0;  // RichEdit handles
            switch (wp) {
                case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
                case VK_PRIOR: case VK_NEXT: case VK_HOME: case VK_END: {
                    LRESULT res = CallWindowProcW(origEditProc_, hwnd, msg, wp, lp);
                    handled = true;
                    viewChanged();
                    return res;
                }
                case VK_SHIFT: case VK_CONTROL: case VK_MENU:
                    return 0;
            }
            // Everything else (Esc, N/P, F-keys, digits...) belongs to Lister.
            PostMessageW(lister_, msg, wp, lp);
            handled = true;
            return 0;
        }
        case WM_KEYUP:
            if (wp != VK_SHIFT && wp != VK_CONTROL) {
                bool nav = wp == VK_UP || wp == VK_DOWN || wp == VK_LEFT || wp == VK_RIGHT || wp == VK_PRIOR ||
                           wp == VK_NEXT || wp == VK_HOME || wp == VK_END;
                if (!nav && !(GetKeyState(VK_CONTROL) < 0)) {
                    PostMessageW(lister_, msg, wp, lp);
                    handled = true;
                }
            }
            return 0;
        case WM_CHAR:
            handled = true;  // read-only view: swallow typed characters (no beeps)
            return 0;
        case WM_MOUSEWHEEL:
        case WM_VSCROLL:
        case WM_HSCROLL: {
            LRESULT res = CallWindowProcW(origEditProc_, hwnd, msg, wp, lp);
            handled = true;
            viewChanged();
            return res;
        }
        case WM_CONTEXTMENU:
            handled = true;
            showContextMenu(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
    }
    return 0;
}

}  // namespace shl::view
