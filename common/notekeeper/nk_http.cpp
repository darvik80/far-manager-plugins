#include "nk_http.hpp"
#include "nk_secure.hpp"

#include <windows.h>
#include <winhttp.h>

#include <string>
#include <vector>

NkHttpResult NkRequest(const std::wstring& method, const std::wstring& url, const std::wstring& token,
                       const std::string& body, const std::wstring& extraHeader) {
    NkHttpResult result;
    std::wstring full = url;
    if (full.find(L"://") == std::wstring::npos) full = L"https://" + full;
    if (!NkUrlIsSecure(full)) {
        result.status = -1;
        return result;
    }
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{};
    wchar_t path[4096]{};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 4096;
    if (!WinHttpCrackUrl(full.c_str(), 0, 0, &uc)) {
        result.status = -1;
        result.winError = GetLastError();
        return result;
    }
    HINTERNET session = WinHttpOpen(L"NoteKeeperFar/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        result.winError = GetLastError();
        return result;
    }
    DWORD timeout = 15000;
    WinHttpSetOption(session, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(session, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(session, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    if (!connect) {
        result.winError = GetLastError();
        WinHttpCloseHandle(session);
        return result;
    }
    const DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, method.c_str(), path, nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        result.winError = GetLastError();
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return result;
    }
    std::wstring headers = L"Authorization: Bearer " + token +
                           L"\r\nContent-Type: application/json; charset=utf-8\r\nAccept: application/json\r\n";
    if (!extraHeader.empty()) headers += extraHeader + L"\r\n";
    BOOL ok = WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1),
                                 body.empty() ? WINHTTP_NO_REQUEST_DATA : static_cast<LPVOID>(const_cast<char*>(body.data())),
                                 static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    if (ok) ok = WinHttpReceiveResponse(request, nullptr);
    if (ok) {
        DWORD status = 0;
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        result.status = static_cast<int>(status);
        std::string collected;
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(request, &avail) && avail > 0) {
            std::vector<char> buf(avail);
            DWORD read = 0;
            if (!WinHttpReadData(request, buf.data(), avail, &read)) break;
            collected.append(buf.data(), read);
        }
        result.body = std::move(collected);
    } else {
        result.winError = GetLastError();
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
}
