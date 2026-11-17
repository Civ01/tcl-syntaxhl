#include "WinUtil.h"

namespace shl::win {

HINSTANCE g_instance = nullptr;

std::filesystem::path modulePath() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(g_instance, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) {
            buf.resize(n);
            return std::filesystem::path(buf);
        }
        buf.resize(buf.size() * 2);
    }
}

std::filesystem::path pluginDir() { return modulePath().parent_path(); }

std::filesystem::path iniPath() {
    auto p = modulePath();
    return p.parent_path() / (p.stem().wstring() + L".ini");
}

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(std::wstring_view w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring ansiToWide(const char* s) {
    if (!s || !*s) return {};
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n : 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
    w.resize(wcslen(w.c_str()));
    return w;
}

const AnsiConverter& ansiConverter() {
    static const AnsiConverter conv = [](std::string_view b) {
        std::u16string out;
        if (b.empty()) return out;
        // Process in chunks to stay within int limits for huge files.
        const size_t kChunk = 1u << 28;
        for (size_t off = 0; off < b.size(); off += kChunk) {
            int len = static_cast<int>(std::min(kChunk, b.size() - off));
            int n = MultiByteToWideChar(CP_ACP, 0, b.data() + off, len, nullptr, 0);
            size_t pos = out.size();
            out.resize(pos + static_cast<size_t>(n));
            MultiByteToWideChar(CP_ACP, 0, b.data() + off, len, reinterpret_cast<wchar_t*>(out.data() + pos), n);
        }
        return out;
    };
    return conv;
}

bool readFile(const std::wstring& path, std::string& out, uint64_t& size) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER li{};
    if (!GetFileSizeEx(h, &li) || li.QuadPart < 0 || static_cast<uint64_t>(li.QuadPart) > (uint64_t(1) << 31)) {
        CloseHandle(h);
        return false;  // > 2 GB: leave it to TC
    }
    size = static_cast<uint64_t>(li.QuadPart);
    out.resize(static_cast<size_t>(size));
    size_t done = 0;
    while (done < out.size()) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(out.size() - done, 64u << 20));
        DWORD got = 0;
        if (!ReadFile(h, out.data() + done, chunk, &got, nullptr) || got == 0) break;
        done += got;
    }
    CloseHandle(h);
    out.resize(done);
    return true;
}

}  // namespace shl::win
