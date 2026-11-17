// Windows helpers shared by the plugin layer.
#pragma once

#include <windows.h>

#include <filesystem>
#include <string>

#include "core/TextDecoder.h"
#include "core/Theme.h"

namespace shl::win {

extern HINSTANCE g_instance;

std::filesystem::path modulePath();
std::filesystem::path pluginDir();
std::filesystem::path iniPath();  // <module stem>.ini next to the DLL

std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view w);
std::wstring ansiToWide(const char* s);
inline std::wstring toW(std::u16string_view s) { return std::wstring(s.begin(), s.end()); }
inline std::u16string toU16(std::wstring_view s) { return std::u16string(s.begin(), s.end()); }

const AnsiConverter& ansiConverter();  // MultiByteToWideChar(CP_ACP)

// Reads a whole file; returns false on failure. `size` receives the file size.
bool readFile(const std::wstring& path, std::string& out, uint64_t& size);

inline COLORREF toColorRef(Color c) {
    return RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

}  // namespace shl::win
