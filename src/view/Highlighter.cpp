#include "Highlighter.h"

#include <ole2.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>

#include <cstdio>
#include <map>

#include "plugin/WinUtil.h"

namespace shl::view {

int measureCharTwips(HWND hwnd, const FontSpec& font) {
    HDC dc = GetDC(hwnd);
    int dpiY = GetDeviceCaps(dc, LOGPIXELSY), dpiX = GetDeviceCaps(dc, LOGPIXELSX);
    HFONT f = CreateFontW(-MulDiv(font.pointSize, dpiY, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          FIXED_PITCH | FF_MODERN, font.face.c_str());
    HGDIOBJ old = SelectObject(dc, f);
    SIZE sz{};
    GetTextExtentPoint32W(dc, L"0000000000", 10, &sz);
    SelectObject(dc, old);
    DeleteObject(f);
    ReleaseDC(hwnd, dc);
    int twips = dpiX > 0 ? MulDiv(sz.cx, 1440, dpiX * 10) : 120;
    return twips > 0 ? twips : 120;
}

namespace {

void appendEscaped(std::string& out, std::u16string_view s) {
    for (char16_t c : s) {
        switch (c) {
            case u'\\': out += "\\\\"; break;
            case u'{': out += "\\{"; break;
            case u'}': out += "\\}"; break;
            case u'\r': out += "\\par\n"; break;
            case u'\n': out += "\\par\n"; break;
            case u'\t': out += "\\tab "; break;
            default:
                if (c >= 0x20 && c < 0x80) {
                    out.push_back(static_cast<char>(c));
                } else if (c < 0x20) {
                    out.push_back(' ');
                } else {
                    char buf[16];
                    std::snprintf(buf, sizeof buf, "\\u%d?", static_cast<int>(static_cast<int16_t>(c)));
                    out += buf;
                }
        }
    }
}

std::string colorEntry(Color c) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "\\red%u\\green%u\\blue%u;", (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    return buf;
}

}  // namespace

std::string buildRtf(std::u16string_view text, const std::vector<Token>& tokens, const Theme& theme,
                     const FontSpec& font) {
    std::map<Color, int> colorIndex;
    std::vector<Color> colors;
    auto idx = [&](Color c) {
        auto it = colorIndex.find(c);
        if (it != colorIndex.end()) return it->second;
        colors.push_back(c);
        int i = static_cast<int>(colors.size());  // index 0 = auto
        colorIndex[c] = i;
        return i;
    };
    idx(theme.foreground);  // \cf1

    // Precompute per-class control words.
    std::string classCtl[kTokenClassCount];
    for (size_t k = 0; k < kTokenClassCount; ++k) {
        const Style& s = theme.styles[k];
        std::string ctl = "{\\cf" + std::to_string(idx(s.fg));
        if (s.bg) ctl += "\\highlight" + std::to_string(idx(*s.bg));
        if (s.flags & kBold) ctl += "\\b";
        if (s.flags & kItalic) ctl += "\\i";
        if (s.flags & kUnderline) ctl += "\\ul";
        ctl += " ";
        classCtl[k] = std::move(ctl);
    }

    // Style key per class; classes that look like plain text get key 0 and
    // produce no run at all (fewer runs = much faster RichEdit loading).
    auto plain = [&](size_t k) {
        const Style& st = theme.styles[k];
        return st.fg == theme.foreground && !st.bg && st.flags == 0;
    };
    auto sameLook = [&](TokenClass a, TokenClass b) {
        const Style& x = theme.style(a);
        const Style& y = theme.style(b);
        return x.fg == y.fg && x.bg == y.bg && x.flags == y.flags;
    };
    auto onlyBlanks = [&](size_t from, size_t to) {
        for (size_t i = from; i < to; ++i)
            if (text[i] != u' ' && text[i] != u'\t') return false;
        return true;
    };

    std::string body;
    body.reserve(text.size() + tokens.size() * 8 + 64);
    size_t pos = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        Token t = tokens[i];
        if (t.start < pos || t.start + t.length > text.size()) continue;
        size_t k = static_cast<size_t>(t.cls);
        if (plain(k)) continue;
        // Extend over following tokens with the same look, separated only by
        // spaces/tabs (invisible unless the style has a background/underline).
        const Style& st = theme.styles[k];
        bool canBridge = !st.bg && !(st.flags & kUnderline);
        size_t end = t.start + t.length;
        while (i + 1 < tokens.size()) {
            const Token& n = tokens[i + 1];
            if (n.start < end || n.start + n.length > text.size()) break;
            if (!sameLook(t.cls, n.cls)) break;
            if (n.start != end && !(canBridge && onlyBlanks(end, n.start))) break;
            end = n.start + n.length;
            ++i;
        }
        appendEscaped(body, text.substr(pos, t.start - pos));
        body += classCtl[k];
        appendEscaped(body, text.substr(t.start, end - t.start));
        body += "}";
        pos = end;
    }
    appendEscaped(body, text.substr(pos));

    std::string out;
    out.reserve(body.size() + 512);
    out += "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\\deftab" + std::to_string(font.tabTwips);
    out += "{\\fonttbl{\\f0\\fmodern\\fcharset1 ";
    appendEscaped(out, std::u16string(font.face.begin(), font.face.end()));
    out += ";}}{\\colortbl;";
    for (Color c : colors) out += colorEntry(c);
    out += "}\\pard\\plain\\f0\\fs" + std::to_string(font.pointSize * 2) + "\\cf1 ";
    out += body;
    out += "}";
    return out;
}

namespace {
struct StreamCookie {
    const std::string* data;
    size_t pos;
};
DWORD CALLBACK streamCallback(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* pcb) {
    auto* c = reinterpret_cast<StreamCookie*>(cookie);
    size_t left = c->data->size() - c->pos;
    size_t n = std::min<size_t>(left, static_cast<size_t>(cb));
    memcpy(buf, c->data->data() + c->pos, n);
    c->pos += n;
    *pcb = static_cast<LONG>(n);
    return 0;
}

void setDefaultFormat(HWND edit, const Theme& theme, const FontSpec& font) {
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_BACKCOLOR | CFM_CHARSET;
    cf.yHeight = font.pointSize * 20;
    cf.crTextColor = win::toColorRef(theme.foreground);
    cf.dwEffects = CFE_AUTOBACKCOLOR;
    cf.bCharSet = DEFAULT_CHARSET;
    wcsncpy_s(cf.szFaceName, font.face.c_str(), _TRUNCATE);
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&cf));
}

void setTabStops(HWND edit, const FontSpec& font) {
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof pf;
    pf.dwMask = PFM_TABSTOPS;
    pf.cTabCount = MAX_TAB_STOPS;
    for (int i = 0; i < MAX_TAB_STOPS; ++i) pf.rgxTabs[i] = (i + 1) * font.tabTwips;
    SendMessageW(edit, EM_SETSEL, 0, -1);
    SendMessageW(edit, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
    SendMessageW(edit, EM_SETSEL, 0, 0);
}
}  // namespace

bool streamRtf(HWND edit, const std::string& rtf) {
    StreamCookie cookie{&rtf, 0};
    EDITSTREAM es{};
    es.dwCookie = reinterpret_cast<DWORD_PTR>(&cookie);
    es.pfnCallback = streamCallback;
    SendMessageW(edit, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&es));
    return es.dwError == 0;
}

void setPlainText(HWND edit, std::u16string_view text, const Theme& theme, const FontSpec& font) {
    SETTEXTEX st{ST_DEFAULT, 1200};
    SendMessageW(edit, EM_SETTEXTEX, reinterpret_cast<WPARAM>(&st), reinterpret_cast<LPARAM>(L""));
    setDefaultFormat(edit, theme, font);
    setTabStops(edit, font);
    std::wstring w(text.begin(), text.end());
    SETTEXTEX ins{ST_SELECTION, 1200};
    SendMessageW(edit, EM_SETTEXTEX, reinterpret_cast<WPARAM>(&ins), reinterpret_cast<LPARAM>(w.c_str()));
    setTabStops(edit, font);  // in case pasted paragraphs did not inherit them
    SendMessageW(edit, EM_SETSEL, 0, 0);
}

namespace {
// IID_ITextDocument {8CC497C0-A1DF-11CE-8098-00AA0047BE5D}
const IID kIID_ITextDocument = {0x8CC497C0, 0xA1DF, 0x11CE, {0x80, 0x98, 0x00, 0xAA, 0x00, 0x47, 0xBE, 0x5D}};

ITextDocument* textDocument(HWND edit) {
    IRichEditOle* ole = nullptr;
    if (!SendMessageW(edit, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&ole)) || !ole) return nullptr;
    ITextDocument* doc = nullptr;
    ole->QueryInterface(kIID_ITextDocument, reinterpret_cast<void**>(&doc));
    ole->Release();
    return doc;
}

bool isPlainLook(const Style& s, const Theme& theme) {
    return s.fg == theme.foreground && !s.bg && s.flags == 0;
}

// Fallback without TOM: selection-based formatting. Restores selection and the
// first visible line (scroll positions are 16-bit in RichEdit, lines are not).
void applyTokensWithSelection(HWND edit, const std::vector<Token>& tokens, const Theme& theme) {
    CHARRANGE sel{};
    SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
    LRESULT firstLine = SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    LRESULT mask = SendMessageW(edit, EM_SETEVENTMASK, 0, 0);
    SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
    SendMessageW(edit, EM_HIDESELECTION, TRUE, 0);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_BACKCOLOR;
    for (const auto& t : tokens) {
        const Style& s = theme.style(t.cls);
        if (t.cls == TokenClass::Default || isPlainLook(s, theme)) continue;
        cf.crTextColor = win::toColorRef(s.fg);
        cf.dwEffects = 0;
        if (s.flags & kBold) cf.dwEffects |= CFE_BOLD;
        if (s.flags & kItalic) cf.dwEffects |= CFE_ITALIC;
        if (s.flags & kUnderline) cf.dwEffects |= CFE_UNDERLINE;
        if (s.bg) cf.crBackColor = win::toColorRef(*s.bg);
        else cf.dwEffects |= CFE_AUTOBACKCOLOR;
        CHARRANGE r{static_cast<LONG>(t.start), static_cast<LONG>(t.start + t.length)};
        SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&r));
        SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    }
    SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&sel));
    LRESULT nowLine = SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    if (nowLine != firstLine) SendMessageW(edit, EM_LINESCROLL, 0, firstLine - nowLine);
    SendMessageW(edit, EM_HIDESELECTION, FALSE, 0);
    SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
    SendMessageW(edit, EM_SETEVENTMASK, 0, mask);
    InvalidateRect(edit, nullptr, FALSE);
}
}  // namespace

void applyTokens(HWND edit, const std::vector<Token>& tokens, const Theme& theme) {
    if (tokens.empty()) return;
    ITextDocument* doc = textDocument(edit);
    if (!doc) {
        applyTokensWithSelection(edit, tokens, theme);
        return;
    }
    // TOM ranges format text without touching the selection or scroll position.
    long freezeCount = 0;
    doc->Freeze(&freezeCount);
    for (const auto& t : tokens) {
        const Style& s = theme.style(t.cls);
        if (t.cls == TokenClass::Default || isPlainLook(s, theme)) continue;
        ITextRange* range = nullptr;
        if (FAILED(doc->Range(static_cast<long>(t.start), static_cast<long>(t.start + t.length), &range)) || !range)
            continue;
        ITextFont* font = nullptr;
        if (SUCCEEDED(range->GetFont(&font)) && font) {
            font->SetForeColor(static_cast<long>(win::toColorRef(s.fg)));
            font->SetBackColor(s.bg ? static_cast<long>(win::toColorRef(*s.bg)) : static_cast<long>(tomAutoColor));
            font->SetBold((s.flags & kBold) ? tomTrue : tomFalse);
            font->SetItalic((s.flags & kItalic) ? tomTrue : tomFalse);
            font->SetUnderline((s.flags & kUnderline) ? tomSingle : tomNone);
            font->Release();
        }
        range->Release();
    }
    doc->Unfreeze(&freezeCount);
    doc->Release();
    InvalidateRect(edit, nullptr, FALSE);
}

}  // namespace shl::view
