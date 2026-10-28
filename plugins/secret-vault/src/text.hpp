#ifndef SECRET_VAULT_TEXT_HPP_
#define SECRET_VAULT_TEXT_HPP_

#include <string>
#include <windows.h>

inline std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    const int chars = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(chars > 0 ? chars : 0, L'\0');
    if (chars > 0)
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), chars);
    return out;
}

inline std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(bytes > 0 ? bytes : 0, '\0');
    if (bytes > 0)
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), bytes, nullptr, nullptr);
    return out;
}

inline std::wstring Lower(std::wstring text) {
    for (auto& ch : text) ch = static_cast<wchar_t>(towlower(ch));
    return text;
}

inline std::wstring FileTimeToIsoUtc(const FILETIME& ft) {
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&ft, &st)) return {};
    wchar_t buf[32]{};
    swprintf_s(buf, L"%04u-%02u-%02uT%02u:%02u:%02uZ",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

inline std::wstring IsoUtcNow() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    return FileTimeToIsoUtc(ft);
}

#endif
