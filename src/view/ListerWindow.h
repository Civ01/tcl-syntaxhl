// The plugin window handed to Total Commander: container with an InfoBar on
// top and a read-only RichEdit below.
#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "Highlighter.h"
#include "InfoBar.h"
#include "core/Config.h"
#include "core/LazyModel.h"
#include "core/Theme.h"

namespace shl::view {

enum class RenderMode { Full, Lazy, Plain };

class ListerWindow {
public:
    // Returns nullptr when the file is not handled (TC then tries other plugins).
    static ListerWindow* create(HWND parent, const std::wstring& file, int showFlags);
    static ListerWindow* fromHwnd(HWND hwnd);

    HWND hwnd() const { return hwnd_; }

    // (Re)loads a file into this window. Returns false if it is not handled.
    bool load(const std::wstring& file, int showFlags);
    int search(const std::wstring& text, int flags);
    int command(int cmd, int param);

private:
    ListerWindow() = default;
    bool createWindows(HWND parent);
    void layout();
    void applyWrap(bool wrap);
    void viewChanged();      // scroll/size/zoom: lazy painting + percent
    void paintVisible();
    void reportPercent();
    void setPercent(int pct);
    void gotoLine();
    void showContextMenu(int x, int y);
    void openSettings();
    size_t lineOfOffset(size_t off) const;
    size_t currentLine() const;
    void scrollToCenter(LONG cp);
    LONG topCp() const;
    LONG bottomCp() const;
    void scrollTopToDisplayLine(LONG displayLine);

    static LRESULT CALLBACK containerProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK editProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleEditMessage(HWND, UINT, WPARAM, LPARAM, bool& handled);

    HWND hwnd_ = nullptr;
    HWND edit_ = nullptr;
    HWND lister_ = nullptr;  // TC's Lister (or Quick View) window
    WNDPROC origEditProc_ = nullptr;
    InfoBar info_;

    std::wstring file_;
    int showFlags_ = 0;
    bool quickView_ = false;
    bool wrap_ = false;
    bool dark_ = false;
    RenderMode mode_ = RenderMode::Full;

    Config cfg_;
    Theme theme_;
    FontSpec font_;
    LexerPtr lexer_;
    std::u16string text_;              // normalised ('\r' line ends)
    std::vector<uint32_t> lineStarts_; // for go-to-line
    std::unique_ptr<LazyModel> lazy_;  // only in Lazy mode
    std::vector<Token> pending_;       // lazy tokens not applied yet (sorted)
    int lastPercent_ = -1;
    LONG lastTopCp_ = -1, lastBottomCp_ = -1;
};

}  // namespace shl::view
