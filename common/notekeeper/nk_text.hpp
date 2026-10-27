#ifndef NK_TEXT_HPP_
#define NK_TEXT_HPP_

#include <string>
#include <windows.h>

inline std::wstring NkWide(const std::string& text) {
    if (text.empty()) return {};
    const int chars = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(chars > 0 ? chars : 0, L'\0');
    if (chars > 0)
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), chars);
    return out;
}

inline std::string NkUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(bytes > 0 ? bytes : 0, '\0');
    if (bytes > 0)
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), bytes, nullptr, nullptr);
    return out;
}

inline std::wstring NkLower(std::wstring text) {
    for (auto& ch : text) ch = static_cast<wchar_t>(towlower(ch));
    return text;
}

inline std::wstring NkTrimSlash(std::wstring url) {
    while (!url.empty() && (url.back() == L'/' || url.back() == L'\\')) url.pop_back();
    return url;
}

inline std::wstring NkExtractHost(std::wstring text) {
    text = NkLower(text);
    while (!text.empty() && (text.front() == L' ' || text.front() == L'\t')) text.erase(text.begin());
    const auto scheme = text.find(L"://");
    if (scheme != std::wstring::npos) text = text.substr(scheme + 3);
    const auto at = text.find(L'@');
    if (at != std::wstring::npos) text = text.substr(at + 1);
    const auto slash = text.find_first_of(L"/\\");
    if (slash != std::wstring::npos) text.resize(slash);
    if (!text.empty() && text.front() == L'[') {
        const auto rb = text.find(L']');
        text = (rb == std::wstring::npos) ? text : text.substr(1, rb - 1);
    } else {
        const auto first = text.find(L':');
        const auto last = text.rfind(L':');
        if (first != std::wstring::npos && first == last) text.resize(first);
    }
    while (!text.empty() && text.back() == L'.') text.pop_back();
    return text;
}

inline bool NkIsoToFileTime(const std::wstring& iso, FILETIME* out) {
    if (!out || iso.size() < 19) return false;
    const wchar_t* p = iso.c_str();
    auto need = [&](wchar_t ch) {
        if (*p != ch) return false;
        ++p;
        return true;
    };
    auto num = [&](int digits) {
        int value = 0;
        for (int i = 0; i < digits; ++i) {
            if (*p < L'0' || *p > L'9') return -1;
            value = value * 10 + (*p++ - L'0');
        }
        return value;
    };
    const int year = num(4);
    if (year < 1970 || !need(L'-')) return false;
    const int month = num(2);
    if (month < 1 || !need(L'-')) return false;
    const int day = num(2);
    if (day < 1 || (*p != L'T' && *p != L' ')) return false;
    ++p;
    const int hour = num(2);
    if (hour < 0 || !need(L':')) return false;
    const int minute = num(2);
    if (minute < 0 || !need(L':')) return false;
    int second = num(2);
    if (second < 0) second = 0;
    SYSTEMTIME utc{};
    utc.wYear = static_cast<WORD>(year);
    utc.wMonth = static_cast<WORD>(month);
    utc.wDay = static_cast<WORD>(day);
    utc.wHour = static_cast<WORD>(hour);
    utc.wMinute = static_cast<WORD>(minute);
    utc.wSecond = static_cast<WORD>(second);
    FILETIME fileTime{};
    if (!SystemTimeToFileTime(&utc, &fileTime)) return false;
    if (*p == L'.') {
        ++p;
        while (*p >= L'0' && *p <= L'9') ++p;
    }
    long offsetMin = 0;
    if (*p == L'+' || *p == L'-') {
        const int sign = *p == L'+' ? 1 : -1;
        ++p;
        const int oh = num(2);
        if (oh < 0) return false;
        if (*p == L':') ++p;
        int om = 0;
        if (*p >= L'0' && *p <= L'9') om = num(2);
        if (om < 0) om = 0;
        offsetMin = sign * (oh * 60 + om);
    }
    ULARGE_INTEGER value;
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    value.QuadPart -= static_cast<long long>(offsetMin) * 60LL * 10000000LL;
    out->dwLowDateTime = value.LowPart;
    out->dwHighDateTime = value.HighPart;
    return true;
}

inline std::wstring NkIsoToLocal(const std::wstring& iso) {
    FILETIME utc{};
    if (!NkIsoToFileTime(iso, &utc)) return iso;
    FILETIME local{};
    if (!FileTimeToLocalFileTime(&utc, &local)) return iso;
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&local, &st)) return iso;
    wchar_t buf[20]{};
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return buf;
}

inline std::wstring NkLocalToIso(const std::wstring& local) {
    if (local.empty()) return {};
    std::wstring text = local;
    for (auto& ch : text)
        if (ch == L'T') ch = L' ';
    SYSTEMTIME localTime{};
    if (swscanf_s(text.c_str(), L"%hu-%hu-%hu %hu:%hu",
                  &localTime.wYear, &localTime.wMonth, &localTime.wDay,
                  &localTime.wHour, &localTime.wMinute) < 5)
        return local;
    SYSTEMTIME utc{};
    if (!TzSpecificLocalTimeToSystemTime(nullptr, &localTime, &utc))
        utc = localTime;
    wchar_t buf[32]{};
    swprintf_s(buf, L"%04u-%02u-%02uT%02u:%02u:%02uZ",
               utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);
    return buf;
}

#endif
