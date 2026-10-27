#include "nk_settings.hpp"

#include <windows.h>

#include <string>

namespace {

std::wstring AppDataDir(const wchar_t* folder) {
    wchar_t app[MAX_PATH]{};
    if (!GetEnvironmentVariableW(L"APPDATA", app, MAX_PATH)) return {};
    std::wstring dir = std::wstring(app) + L"\\Far Manager\\" + folder;
    CreateDirectoryW((std::wstring(app) + L"\\Far Manager").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring ReadIni(const std::wstring& path, const wchar_t* key) {
    wchar_t value[2048]{};
    GetPrivateProfileStringW(L"server", key, L"", value, 2048, path.c_str());
    return value;
}

}  // namespace

std::wstring NkNormalizeUrl(std::wstring url) {
    while (!url.empty() && (url.front() == L' ' || url.front() == L'\t')) url.erase(url.begin());
    while (!url.empty() && (url.back() == L' ' || url.back() == L'\t' || url.back() == L'/' || url.back() == L'\\'))
        url.pop_back();
    if (url.empty()) return {};
    if (url.find(L"://") == std::wstring::npos) url = L"https://" + url;
    std::wstring lower = url;
    for (wchar_t& ch : lower)
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
    if (lower.size() >= 7 && lower.compare(lower.size() - 7, 7, L"/api/v1") == 0)
        url.resize(url.size() - 7);
    while (!url.empty() && (url.back() == L'/' || url.back() == L'\\')) url.pop_back();
    if (url == L"https://" || url == L"http://" || url == L"https:" || url == L"http:") return {};
    return url;
}

std::wstring NkSettingsPath() {
    const std::wstring dir = AppDataDir(L"NoteKeeper");
    return dir.empty() ? L"notekeeper.ini" : dir + L"\\settings.ini";
}

bool NkLoadSettings(NkSettings& settings) {
    settings = {};
    const std::wstring own = NkSettingsPath();
    settings.baseUrl = NkNormalizeUrl(ReadIni(own, L"baseUrl"));
    settings.token = NkUnprotectSecret(ReadIni(own, L"token"));
    const std::wstring vaultDir = AppDataDir(L"SecretVault");
    const std::wstring vault = vaultDir.empty() ? L"sync.ini" : vaultDir + L"\\sync.ini";
    if (settings.baseUrl.empty()) settings.baseUrl = NkNormalizeUrl(ReadIni(vault, L"baseUrl"));
    if (settings.token.empty()) settings.token = NkUnprotectSecret(ReadIni(vault, L"token"));
    if (!settings.token.empty()) {
        const std::wstring storedOwn = ReadIni(own, L"token");
        const std::wstring storedVault = ReadIni(vault, L"token");
        if (storedOwn.compare(0, 6, L"dpapi:") != 0 || !storedVault.empty())
            NkSaveSettings(settings);
    }
    return true;
}

bool NkSaveSettings(const NkSettings& settings) {
    const std::wstring url = NkNormalizeUrl(settings.baseUrl);
    const std::wstring path = NkSettingsPath();
    const std::wstring protectedToken = NkProtectSecret(settings.token);
    if (!settings.token.empty() && protectedToken.empty()) return false;
    WritePrivateProfileStringW(L"server", L"baseUrl", url.c_str(), path.c_str());
    WritePrivateProfileStringW(L"server", L"token", protectedToken.c_str(), path.c_str());
    const std::wstring vaultDir = AppDataDir(L"SecretVault");
    const std::wstring vault = vaultDir.empty() ? L"sync.ini" : vaultDir + L"\\sync.ini";
    WritePrivateProfileStringW(L"server", L"baseUrl", url.c_str(), vault.c_str());
    WritePrivateProfileStringW(L"server", L"token", nullptr, vault.c_str());
    return true;
}

bool NkConfigured(const NkSettings& settings) {
    return !NkNormalizeUrl(settings.baseUrl).empty() && !settings.token.empty();
}
