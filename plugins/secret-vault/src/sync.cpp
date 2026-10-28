#include "sync.hpp"
#include "text.hpp"
#include "nk_secure.hpp"
#include "nk_settings.hpp"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace {

struct HttpResult {
    int status = 0;
    std::string body;
};

struct RemoteItem {
    std::wstring id;
    std::wstring type;
    std::wstring title;
    std::wstring username;
    std::wstring host;
    std::wstring url;
    std::wstring password;
    std::wstring privateKey;
    std::wstring secret;
    std::wstring extraJson;
    std::wstring deletedAt;
    std::wstring updatedAt;
    int revision = 1;
};

std::wstring TrimSlash(std::wstring url) {
    while (!url.empty() && (url.back() == L'/' || url.back() == L'\\')) url.pop_back();
    return url;
}

void SkipWs(const std::string& text, size_t& i) {
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
}

bool ParseJsonString(const std::string& text, size_t& i, std::wstring& out) {
    SkipWs(text, i);
    if (i >= text.size() || text[i] != '"') return false;
    ++i;
    std::string raw;
    while (i < text.size()) {
        const char ch = text[i++];
        if (ch == '"') {
            out = Wide(raw);
            return true;
        }
        if (ch != '\\') {
            raw.push_back(ch);
            continue;
        }
        if (i >= text.size()) return false;
        const char next = text[i++];
        switch (next) {
        case '"':
        case '\\':
        case '/': raw.push_back(next); break;
        case 'n': raw.push_back('\n'); break;
        case 'r': raw.push_back('\r'); break;
        case 't': raw.push_back('\t'); break;
        case 'u': {
            if (i + 4 > text.size()) return false;
            unsigned code = 0;
            for (int n = 0; n < 4; ++n) {
                const char hex = text[i++];
                code <<= 4;
                if (hex >= '0' && hex <= '9') code |= hex - '0';
                else if (hex >= 'a' && hex <= 'f') code |= hex - 'a' + 10;
                else if (hex >= 'A' && hex <= 'F') code |= hex - 'A' + 10;
                else return false;
            }
            wchar_t wide[2] = { static_cast<wchar_t>(code), 0 };
            char utf8[8]{};
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, 1, utf8, sizeof(utf8), nullptr, nullptr);
            if (bytes > 0) raw.append(utf8, bytes);
            break;
        }
        default: return false;
        }
    }
    return false;
}

bool ExpectColon(const std::string& text, size_t& i) {
    SkipWs(text, i);
    if (i >= text.size() || text[i] != ':') return false;
    ++i;
    return true;
}

bool SkipJson(const std::string& text, size_t& i);

bool SkipJson(const std::string& text, size_t& i) {
    SkipWs(text, i);
    if (i >= text.size()) return false;
    if (text[i] == '"') {
        std::wstring dummy;
        return ParseJsonString(text, i, dummy);
    }
    if (text[i] == '{') {
        ++i;
        SkipWs(text, i);
        if (i < text.size() && text[i] == '}') {
            ++i;
            return true;
        }
        for (;;) {
            std::wstring key;
            if (!ParseJsonString(text, i, key) || !ExpectColon(text, i) || !SkipJson(text, i)) return false;
            SkipWs(text, i);
            if (i < text.size() && text[i] == ',') {
                ++i;
                continue;
            }
            SkipWs(text, i);
            if (i < text.size() && text[i] == '}') {
                ++i;
                return true;
            }
            return false;
        }
    }
    if (text[i] == '[') {
        ++i;
        SkipWs(text, i);
        if (i < text.size() && text[i] == ']') {
            ++i;
            return true;
        }
        for (;;) {
            if (!SkipJson(text, i)) return false;
            SkipWs(text, i);
            if (i < text.size() && text[i] == ',') {
                ++i;
                continue;
            }
            SkipWs(text, i);
            if (i < text.size() && text[i] == ']') {
                ++i;
                return true;
            }
            return false;
        }
    }
    if (text.compare(i, 4, "null") == 0) { i += 4; return true; }
    if (text.compare(i, 4, "true") == 0) { i += 4; return true; }
    if (text.compare(i, 5, "false") == 0) { i += 5; return true; }
    if (text[i] == '-' || std::isdigit(static_cast<unsigned char>(text[i]))) {
        if (text[i] == '-') ++i;
        while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.' || text[i] == 'e' || text[i] == 'E' || text[i] == '+'))
            ++i;
        return true;
    }
    return false;
}

bool ParseNumber(const std::string& text, size_t& i, int& value) {
    SkipWs(text, i);
    if (i >= text.size() || (!std::isdigit(static_cast<unsigned char>(text[i])) && text[i] != '-')) return false;
    size_t start = i;
    if (text[i] == '-') ++i;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
    value = atoi(text.substr(start, i - start).c_str());
    return true;
}

bool ParseObject(const std::string& text, size_t& i, std::vector<std::pair<std::string, std::wstring>>& fields, int* revision) {
    SkipWs(text, i);
    if (i >= text.size() || text[i] != '{') return false;
    ++i;
    SkipWs(text, i);
    if (i < text.size() && text[i] == '}') {
        ++i;
        return true;
    }
    for (;;) {
        std::wstring keyWide;
        if (!ParseJsonString(text, i, keyWide) || !ExpectColon(text, i)) return false;
        const std::string key = Utf8(keyWide);
        SkipWs(text, i);
        if (i < text.size() && text[i] == '"') {
            std::wstring value;
            if (!ParseJsonString(text, i, value)) return false;
            fields.emplace_back(key, std::move(value));
        } else if (revision && key == "revision") {
            if (!ParseNumber(text, i, *revision)) return false;
        } else if (!SkipJson(text, i)) {
            return false;
        }
        SkipWs(text, i);
        if (i < text.size() && text[i] == ',') {
            ++i;
            continue;
        }
        SkipWs(text, i);
        if (i < text.size() && text[i] == '}') {
            ++i;
            return true;
        }
        return false;
    }
}

const std::wstring* Field(const std::vector<std::pair<std::string, std::wstring>>& fields, const char* name) {
    for (const auto& field : fields)
        if (field.first == name) return &field.second;
    return nullptr;
}

std::wstring FieldOr(const std::vector<std::pair<std::string, std::wstring>>& fields, const char* name) {
    const auto* value = Field(fields, name);
    return value ? *value : std::wstring{};
}

bool ParseRemoteItem(const std::string& json, size_t& i, RemoteItem& item) {
    std::vector<std::pair<std::string, std::wstring>> fields;
    int revision = 1;
    if (!ParseObject(json, i, fields, &revision)) return false;
    item.id = FieldOr(fields, "id");
    item.type = FieldOr(fields, "type");
    item.title = FieldOr(fields, "title");
    item.username = FieldOr(fields, "username");
    item.host = FieldOr(fields, "host");
    item.url = FieldOr(fields, "url");
    item.password = FieldOr(fields, "password");
    item.privateKey = FieldOr(fields, "privateKey");
    item.secret = FieldOr(fields, "secret");
    item.extraJson = FieldOr(fields, "extraJson");
    item.deletedAt = FieldOr(fields, "deletedAt");
    item.updatedAt = FieldOr(fields, "updatedAt");
    if (item.updatedAt.empty()) item.updatedAt = FieldOr(fields, "updated_at");
    if (item.updatedAt.empty()) item.updatedAt = FieldOr(fields, "modifiedAt");
    item.revision = revision;
    return true;
}

bool ParseItemArrayAt(const std::string& json, size_t& i, std::vector<RemoteItem>& items) {
    if (i >= json.size() || json[i] != '[') return false;
    ++i;
    SkipWs(json, i);
    if (i < json.size() && json[i] == ']') {
        ++i;
        return true;
    }
    for (;;) {
        RemoteItem item;
        if (!ParseRemoteItem(json, i, item)) return false;
        if (!item.id.empty()) items.push_back(std::move(item));
        SkipWs(json, i);
        if (i < json.size() && json[i] == ',') {
            ++i;
            continue;
        }
        SkipWs(json, i);
        if (i < json.size() && json[i] == ']') {
            ++i;
            return true;
        }
        return false;
    }
}

bool ParseItemsArray(const std::string& json, const char* arrayName, std::vector<RemoteItem>& items,
                     std::vector<std::wstring>& deletedIds, std::wstring& serverTime) {
    size_t i = 0;
    SkipWs(json, i);
    if (i >= json.size() || json[i] != '{') return false;
    ++i;
    while (i < json.size()) {
        SkipWs(json, i);
        if (i < json.size() && json[i] == '}') return true;
        std::wstring keyWide;
        if (!ParseJsonString(json, i, keyWide) || !ExpectColon(json, i)) return false;
        const std::string key = Utf8(keyWide);
        SkipWs(json, i);
        if (key == arrayName && i < json.size() && json[i] == '[') {
            if (!ParseItemArrayAt(json, i, items)) return false;
        } else if (key == "deletedIds" && i < json.size() && json[i] == '[') {
            ++i;
            SkipWs(json, i);
            if (i < json.size() && json[i] == ']') {
                ++i;
            } else {
                for (;;) {
                    std::wstring id;
                    if (!ParseJsonString(json, i, id)) return false;
                    deletedIds.push_back(id);
                    SkipWs(json, i);
                    if (i < json.size() && json[i] == ',') {
                        ++i;
                        continue;
                    }
                    SkipWs(json, i);
                    if (i < json.size() && json[i] == ']') {
                        ++i;
                        break;
                    }
                    return false;
                }
            }
        } else if (key == "serverTime") {
            if (!ParseJsonString(json, i, serverTime) && !SkipJson(json, i)) return false;
        } else if (!SkipJson(json, i)) {
            return false;
        }
        SkipWs(json, i);
        if (i < json.size() && json[i] == ',') {
            ++i;
            continue;
        }
        SkipWs(json, i);
        return i < json.size() && json[i] == '}';
    }
    return true;
}

bool ParsePullBody(const std::string& json, std::vector<RemoteItem>& items, std::vector<std::wstring>& deletedIds,
                   std::wstring& serverTime) {
    size_t i = 0;
    SkipWs(json, i);
    if (i < json.size() && json[i] == '[') return ParseItemArrayAt(json, i, items);
    return ParseItemsArray(json, "items", items, deletedIds, serverTime);
}

std::string JsonEscape(const std::wstring& wide) {
    const std::string text = Utf8(wide);
    std::string out;
    out.reserve(text.size() + 8);
    for (unsigned char ch : text) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) {
                char buf[8];
                sprintf_s(buf, "\\u%04x", ch);
                out += buf;
            } else {
                out.push_back(static_cast<char>(ch));
            }
        }
    }
    return out;
}

std::string PasswordJson(const PasswordRecord& item) {
    std::ostringstream body;
    body << "{\"id\":\"" << JsonEscape(item.id)
         << "\",\"title\":\"" << JsonEscape(item.name)
         << "\",\"type\":\"password\""
         << ",\"username\":\"" << JsonEscape(item.user)
         << "\",\"host\":\"" << JsonEscape(item.host)
         << "\",\"password\":\"" << JsonEscape(item.secret)
         << "\",\"extraJson\":\"" << JsonEscape(item.note) << "\"}";
    return body.str();
}

std::string KeyJson(const KeyRecord& item) {
    std::wstring extra = L"{\"type\":\"" + item.type + L"\",\"comment\":\"" + item.comment + L"\",\"public\":\"" + item.publicKey + L"\"}";
    std::ostringstream body;
    body << "{\"id\":\"" << JsonEscape(item.id)
         << "\",\"title\":\"" << JsonEscape(item.name)
         << "\",\"type\":\"ssh_key\""
         << ",\"privateKey\":\"" << JsonEscape(item.privateKey)
         << "\",\"extraJson\":\"" << JsonEscape(extra) << "\"}";
    return body.str();
}

bool LooksLikeLocalDateTime(const std::wstring& text) {
    if (text.size() < 19) return false;
    if (text.find(L'Z') != std::wstring::npos) return false;
    if (text.find(L'+') != std::wstring::npos) return false;
    return text[4] == L'-' && text[7] == L'-' && (text[10] == L'T' || text[10] == L' ');
}

std::wstring EncodeSince(const std::wstring& text) {
    std::wstring encoded;
    for (wchar_t ch : text) {
        if (ch == L':') encoded += L"%3A";
        else if (ch == L' ') encoded += L"%20";
        else encoded += ch;
    }
    return encoded;
}

std::wstring SanitizeServerTime(std::wstring text) {
    if (!text.empty() && (text.back() == L'Z' || text.back() == L'z')) text.pop_back();
    const size_t plus = text.find(L'+', 10);
    if (plus != std::wstring::npos) text.resize(plus);
    const size_t tee = text.find(L'T');
    if (tee != std::wstring::npos) {
        const size_t minus = text.find(L'-', tee);
        if (minus != std::wstring::npos) text.resize(minus);
    }
    return LooksLikeLocalDateTime(text) ? text : std::wstring{};
}

std::wstring HttpError(const HttpResult& res) {
    std::wstring msg = L"Не удалось скачать vault (" + std::to_wstring(res.status) + L")";
    if (res.body.empty()) {
        msg += L". Проверь URL и PAT.";
        return msg;
    }
    std::string snippet = res.body.substr(0, 160);
    for (char& ch : snippet)
        if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
    msg += L". " + Wide(snippet);
    return msg;
}

HttpResult Request(const std::wstring& method, const std::wstring& url, const std::wstring& token,
                   const std::string& body, const std::wstring& extraHeader) {
    HttpResult result;
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{};
    wchar_t path[4096]{};
    wchar_t extra[2048]{};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 4096;
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 2048;
    if (!NkUrlIsSecure(url)) {
        result.status = -1;
        return result;
    }
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
        result.status = -1;
        return result;
    }
    std::wstring object = path;
    object += extra;
    HINTERNET session = WinHttpOpen(L"SecretVault/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return result;
    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return result;
    }
    const DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, method.c_str(), object.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return result;
    }
    std::wstring headers = L"Authorization: Bearer " + token + L"\r\nAccept: application/json\r\n";
    if (!body.empty()) headers += L"Content-Type: application/json\r\n";
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
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
}

void ApplyRemote(VaultData& data, const RemoteItem& remote) {
    if (!remote.deletedAt.empty() && remote.deletedAt != L"null") {
        data.passwords.erase(std::remove_if(data.passwords.begin(), data.passwords.end(),
                                            [&](const PasswordRecord& item) { return item.id == remote.id; }),
                             data.passwords.end());
        data.keys.erase(std::remove_if(data.keys.begin(), data.keys.end(),
                                       [&](const KeyRecord& item) { return item.id == remote.id; }),
                        data.keys.end());
        return;
    }
    if (remote.type == L"ssh_key") {
        KeyRecord* existing = nullptr;
        for (auto& item : data.keys)
            if (item.id == remote.id) existing = &item;
        if (existing && existing->revision >= remote.revision) return;
        KeyRecord item = existing ? *existing : KeyRecord{};
        item.id = remote.id;
        item.name = remote.title;
        item.privateKey = remote.privateKey;
        item.updatedAt = remote.updatedAt;
        item.revision = remote.revision;
        if (!remote.extraJson.empty()) {
            size_t i = 0;
            std::vector<std::pair<std::string, std::wstring>> fields;
            if (ParseObject(Utf8(remote.extraJson), i, fields, nullptr)) {
                item.type = FieldOr(fields, "type");
                item.comment = FieldOr(fields, "comment");
                item.publicKey = FieldOr(fields, "public");
            } else {
                item.comment = remote.extraJson;
            }
        }
        if (item.type.empty()) item.type = L"ed25519";
        if (existing) *existing = std::move(item);
        else data.keys.push_back(std::move(item));
        return;
    }
    PasswordRecord* existing = nullptr;
    for (auto& item : data.passwords)
        if (item.id == remote.id) existing = &item;
    if (existing && existing->revision >= remote.revision) return;
    PasswordRecord item = existing ? *existing : PasswordRecord{};
    item.id = remote.id;
    item.name = remote.title;
    item.user = remote.username;
    item.host = remote.host.empty() ? remote.url : remote.host;
    item.secret = remote.password.empty() ? remote.secret : remote.password;
    item.note = remote.extraJson;
    item.updatedAt = remote.updatedAt;
    item.revision = remote.revision;
    if (existing) *existing = std::move(item);
    else data.passwords.push_back(std::move(item));
}

int PushBody(const SyncSettings& settings, const std::wstring& id, int revision, const std::string& body, bool overwrite, std::wstring& error) {
    const std::wstring url = TrimSlash(settings.baseUrl) + L"/api/v1/vault/items/" + id;
    std::wstring extra;
    if (!overwrite && revision > 0)
        extra = L"If-Match: " + std::to_wstring(revision);
    HttpResult put = Request(L"PUT", url, settings.token, body, extra);
    if (put.status == 404 || put.status == 405) {
        HttpResult created = Request(L"POST", TrimSlash(settings.baseUrl) + L"/api/v1/vault/items", settings.token, body, {});
        if (created.status < 200 || created.status >= 300) {
            error = L"Сервер не принял новую запись (" + std::to_wstring(created.status) + L").";
            return created.status;
        }
        return created.status;
    }
    if (put.status == 409) return 409;
    if (put.status < 200 || put.status >= 300) {
        error = L"Ошибка записи на сервер (" + std::to_wstring(put.status) + L").";
        return put.status;
    }
    return put.status;
}

}  // namespace

std::wstring SyncSettingsPath() {
    std::wstring path = VaultPath();
    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L"sync.ini";
    return path.substr(0, slash + 1) + L"sync.ini";
}

bool LoadSyncSettings(SyncSettings& settings) {
    settings = {};
    const std::wstring path = SyncSettingsPath();
    wchar_t url[1024]{};
    wchar_t since[128]{};
    GetPrivateProfileStringW(L"server", L"baseUrl", L"", url, 1024, path.c_str());
    GetPrivateProfileStringW(L"server", L"lastSync", L"", since, 128, path.c_str());
    settings.baseUrl = NkNormalizeUrl(url);
    settings.lastSync = since;
    NkSettings nk;
    NkLoadSettings(nk);
    if (settings.baseUrl.empty()) settings.baseUrl = nk.baseUrl;
    settings.token = nk.token;
    return true;
}

bool SaveSyncSettings(const SyncSettings& settings) {
    const std::wstring path = SyncSettingsPath();
    const std::wstring url = NkNormalizeUrl(settings.baseUrl);
    WritePrivateProfileStringW(L"server", L"baseUrl", url.c_str(), path.c_str());
    WritePrivateProfileStringW(L"server", L"token", nullptr, path.c_str());
    WritePrivateProfileStringW(L"server", L"lastSync", settings.lastSync.c_str(), path.c_str());
    NkSettings nk;
    nk.baseUrl = url;
    nk.token = settings.token;
    return NkSaveSettings(nk);
}

bool SyncConfigured(const SyncSettings& settings) {
    return !settings.baseUrl.empty() && !settings.token.empty();
}

bool PullVault(VaultData& data, SyncSettings& settings, std::wstring& error) {
    if (!SyncConfigured(settings)) return true;
    const std::wstring base = TrimSlash(NkNormalizeUrl(settings.baseUrl));
    if (!NkUrlIsSecure(base)) {
        error = L"Нужен HTTPS (http:// только для localhost).";
        return false;
    }
    std::wstring syncUrl = base + L"/api/v1/vault/sync";
    const bool haveSince = LooksLikeLocalDateTime(settings.lastSync);
    if (haveSince) syncUrl += L"?since=" + EncodeSince(settings.lastSync);

    HttpResult res = Request(L"GET", syncUrl, settings.token, {}, {});
    if (res.status != 200 && haveSince) {
        settings.lastSync.clear();
        res = Request(L"GET", base + L"/api/v1/vault/sync", settings.token, {}, {});
    }

    std::vector<RemoteItem> items;
    std::vector<std::wstring> deletedIds;
    std::wstring serverTime;
    if (res.status != 200 || !ParsePullBody(res.body, items, deletedIds, serverTime)) {
        HttpResult list = Request(L"GET", base + L"/api/v1/vault/items", settings.token, {}, {});
        items.clear();
        deletedIds.clear();
        serverTime.clear();
        if (list.status != 200) {
            error = HttpError(list.status != 0 ? list : res);
            return false;
        }
        if (!ParsePullBody(list.body, items, deletedIds, serverTime)) {
            error = L"Сервер вернул непонятный JSON.";
            return false;
        }
    }

    for (const auto& id : deletedIds) {
        data.passwords.erase(std::remove_if(data.passwords.begin(), data.passwords.end(),
                                            [&](const PasswordRecord& item) { return item.id == id; }),
                             data.passwords.end());
        data.keys.erase(std::remove_if(data.keys.begin(), data.keys.end(),
                                       [&](const KeyRecord& item) { return item.id == id; }),
                        data.keys.end());
    }
    for (const auto& item : items) ApplyRemote(data, item);
    const std::wstring stamp = SanitizeServerTime(serverTime);
    if (!stamp.empty()) settings.lastSync = stamp;
    SaveSyncSettings(settings);
    return true;
}

bool PushVault(VaultData& data, const SyncSettings& settings, bool overwrite, std::wstring& error) {
    if (!SyncConfigured(settings)) return true;
    for (auto& item : data.passwords) {
        const int status = PushBody(settings, item.id, item.revision, PasswordJson(item), overwrite, error);
        if (status == 409) {
            error = L"Конфликт: «" + item.name + L"». Перезаписать сервер?";
            return false;
        }
        if (status < 200 || status >= 300) return false;
        if (item.revision < 1) item.revision = 1;
        else ++item.revision;
    }
    for (auto& item : data.keys) {
        const int status = PushBody(settings, item.id, item.revision, KeyJson(item), overwrite, error);
        if (status == 409) {
            error = L"Конфликт: «" + item.name + L"». Перезаписать сервер?";
            return false;
        }
        if (status < 200 || status >= 300) return false;
        if (item.revision < 1) item.revision = 1;
        else ++item.revision;
    }
    return true;
}

bool SyncVault(VaultData& data, SyncSettings& settings, bool overwrite, std::wstring& error) {
    if (!PushVault(data, settings, overwrite, error)) return false;
    return PullVault(data, settings, error);
}
