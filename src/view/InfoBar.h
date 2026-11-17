// Thin notice strip shown above the text view (hidden when there is no notice).
#pragma once

#include <windows.h>

#include <functional>
#include <string>

namespace shl::view {

class InfoBar {
public:
    bool create(HWND parent);
    void setColors(COLORREF bg, COLORREF fg);
    void setText(const std::wstring& text);  // empty -> hidden
    bool visible() const { return visible_; }
    int height() const;                       // 0 when hidden
    void move(int x, int y, int w);
    HWND hwnd() const { return hwnd_; }

    std::function<void()> onClosed;  // user clicked the close button

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    RECT closeRect() const;

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    std::wstring text_;
    COLORREF bg_ = RGB(255, 244, 206), fg_ = RGB(0, 0, 0);
    bool visible_ = false;
    bool hotClose_ = false;
    int lineHeight_ = 16;
};

}  // namespace shl::view
