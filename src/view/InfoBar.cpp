#include "InfoBar.h"

#include <windowsx.h>

#include "plugin/WinUtil.h"

namespace shl::view {

namespace {
const wchar_t kClass[] = L"TclSyntaxHL_InfoBar";

void registerClass() {
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = DefWindowProcW;  // replaced per instance via SetWindowLongPtr
    wc.hInstance = win::g_instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    done = true;
}
}  // namespace

bool InfoBar::create(HWND parent) {
    registerClass();
    hwnd_ = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, parent, nullptr,
                            win::g_instance, nullptr);
    if (!hwnd_) return false;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowLongPtrW(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&InfoBar::proc));

    NONCLIENTMETRICSW ncm{sizeof ncm};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    font_ = CreateFontIndirectW(&ncm.lfMessageFont);
    HDC dc = GetDC(hwnd_);
    HGDIOBJ old = SelectObject(dc, font_);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);
    lineHeight_ = tm.tmHeight;
    return true;
}

void InfoBar::setColors(COLORREF bg, COLORREF fg) {
    bg_ = bg;
    fg_ = fg;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void InfoBar::setText(const std::wstring& text) {
    text_ = text;
    visible_ = !text_.empty();
    if (hwnd_) {
        ShowWindow(hwnd_, visible_ ? SW_SHOWNA : SW_HIDE);
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

int InfoBar::height() const { return visible_ ? lineHeight_ + 8 : 0; }

void InfoBar::move(int x, int y, int w) {
    if (hwnd_) MoveWindow(hwnd_, x, y, w, height(), TRUE);
}

RECT InfoBar::closeRect() const {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int h = rc.bottom - rc.top;
    return RECT{rc.right - h, rc.top, rc.right, rc.bottom};
}

LRESULT CALLBACK InfoBar::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<InfoBar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH br = CreateSolidBrush(self->bg_);
            FillRect(dc, &rc, br);
            DeleteObject(br);
            // Bottom separator line.
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
            HGDIOBJ oldPen = SelectObject(dc, pen);
            MoveToEx(dc, rc.left, rc.bottom - 1, nullptr);
            LineTo(dc, rc.right, rc.bottom - 1);
            SelectObject(dc, oldPen);
            DeleteObject(pen);

            HGDIOBJ oldFont = SelectObject(dc, self->font_);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, self->fg_);
            RECT close = self->closeRect();
            RECT textRc{rc.left + 8, rc.top, close.left - 4, rc.bottom};
            DrawTextW(dc, self->text_.c_str(), static_cast<int>(self->text_.size()), &textRc,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            if (self->hotClose_) {
                HBRUSH hb = CreateSolidBrush(RGB(200, 80, 80));
                FillRect(dc, &close, hb);
                DeleteObject(hb);
                SetTextColor(dc, RGB(255, 255, 255));
            }
            DrawTextW(dc, L"✕", 1, &close, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
            SelectObject(dc, oldFont);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT close = self->closeRect();
            bool hot = PtInRect(&close, pt) != FALSE;
            if (hot != self->hotClose_) {
                self->hotClose_ = hot;
                InvalidateRect(hwnd, &close, FALSE);
                TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (self->hotClose_) {
                self->hotClose_ = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT close = self->closeRect();
            if (PtInRect(&close, pt)) {
                self->setText(L"");
                if (self->onClosed) self->onClosed();
            }
            return 0;
        }
        case WM_NCDESTROY:
            if (self->font_) DeleteObject(self->font_);
            self->font_ = nullptr;
            self->hwnd_ = nullptr;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace shl::view
