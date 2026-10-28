#include "session_store.hpp"
#include "ssh_config.hpp"
#include "teleport.hpp"
#include "vault_link.hpp"

#include <windows.h>

#include <algorithm>

namespace {

std::wstring AppDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? buf : L".";
    dir += L"\\Far Manager\\SshClient";
    CreateDirectoryW((dir.substr(0, dir.rfind(L'\\'))).c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring Trim(const std::wstring& text) {
    size_t begin = 0;
    while (begin < text.size() && (text[begin] == L' ' || text[begin] == L'\t')) ++begin;
    size_t end = text.size();
    while (end > begin && (text[end - 1] == L' ' || text[end - 1] == L'\t' || text[end - 1] == L'\r')) --end;
    return text.substr(begin, end - begin);
}

std::wstring Unescape(const std::wstring& text) {
    std::wstring out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\\' && i + 1 < text.size()) {
            const wchar_t next = text[++i];
            if (next == L'n') out += L'\n';
            else if (next == L'r') out += L'\r';
            else out += next;
        } else {
            out += text[i];
        }
    }
    return out;
}

std::wstring Escape(const std::wstring& text) {
    std::wstring out;
    for (wchar_t ch : text) {
        if (ch == L'\\') out += L"\\\\";
        else if (ch == L'\n') out += L"\\n";
        else if (ch == L'\r') out += L"\\r";
        else out += ch;
    }
    return out;
}

void SetField(StoredServer& server, const std::wstring& key, const std::wstring& value) {
    if (key == L"host") server.host = value;
    else if (key == L"port") server.port = value.empty() ? L"22" : value;
    else if (key == L"user") server.user = value;
    else if (key == L"key") server.keyFile = value;
    else if (key == L"vault_key") server.vaultKey = value;
    else if (key == L"vault_password") server.vaultPassword = value;
    else if (key == L"proxy") server.proxy = value;
    else if (key == L"teleport") server.fromTeleport = value == L"1" || value == L"true" || value == L"True";
    else if (key == L"password") server.password = Unescape(value);
}

std::wstring Lower(std::wstring text) {
    for (auto& ch : text) ch = towlower(ch);
    return text;
}

}  // namespace

std::wstring ServersPath() {
    return AppDir() + L"\\sessions.ini";
}

std::vector<StoredServer> LoadServers() {
    std::vector<StoredServer> servers;
    FILE* file = _wfopen(ServersPath().c_str(), L"rb");
    if (!file) return servers;
    std::string bytes;
    char buf[2048];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), file)) > 0) bytes.append(buf, n);
    fclose(file);
    int needed = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (needed <= 0) return servers;
    std::wstring text(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), needed);

    StoredServer current;
    bool open = false;
    size_t start = 0;
    auto flush = [&] {
        if (open && !current.name.empty() && !current.host.empty())
            servers.push_back(current);
        current = StoredServer{};
        open = false;
    };
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i != text.size() && text[i] != L'\n') continue;
        std::wstring line = Trim(text.substr(start, i - start));
        start = i + 1;
        if (line.empty()) continue;
        if (line.front() == L'[' && line.back() == L']') {
            flush();
            current.name = line.substr(1, line.size() - 2);
            open = true;
            continue;
        }
        const auto eq = line.find(L'=');
        if (!open || eq == std::wstring::npos) continue;
        SetField(current, Lower(Trim(line.substr(0, eq))), line.substr(eq + 1));
    }
    flush();
    static bool migratedOnce = false;
    bool migrated = false;
    if (!migratedOnce && VaultPluginPresent()) {
        migratedOnce = true;
        for (auto& server : servers) {
            if (server.vaultPassword.empty() && !server.password.empty()) {
                if (VaultStorePassword(server.name, server.password)) {
                    server.vaultPassword = server.name;
                    server.password.clear();
                    migrated = true;
                }
            }
            if (server.vaultKey.empty() && !server.keyFile.empty()) {
                const std::wstring stored = IdentityStore(server.keyFile);
                if (FileExists(ResolveIdentityPath(stored))) {
                    if (stored != server.keyFile) {
                        server.keyFile = stored;
                        migrated = true;
                    }
                } else if (server.keyFile.find(L'\\') == std::wstring::npos &&
                           server.keyFile.find(L'/') == std::wstring::npos &&
                           server.keyFile.find(L'.') == std::wstring::npos) {
                    server.vaultKey = server.keyFile;
                    server.keyFile.clear();
                    migrated = true;
                }
            }
        }
        if (migrated) SaveServers(servers);
    }
    return servers;
}

bool SaveServers(const std::vector<StoredServer>& servers) {
    std::wstring text;
    for (const auto& server : servers) {
        if (server.fromConfig || server.name.empty() || server.host.empty()) continue;
        text += L"[" + server.name + L"]\r\n";
        text += L"host=" + server.host + L"\r\n";
        text += L"port=" + (server.port.empty() ? L"22" : server.port) + L"\r\n";
        text += L"user=" + server.user + L"\r\n";
        text += L"key=" + server.keyFile + L"\r\n";
        text += L"vault_key=" + server.vaultKey + L"\r\n";
        text += L"vault_password=" + server.vaultPassword + L"\r\n";
        text += L"proxy=" + server.proxy + L"\r\n";
        if (server.fromTeleport) text += L"teleport=1\r\n";
        if (server.vaultPassword.empty())
            text += L"password=" + Escape(server.password) + L"\r\n";
    }
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(bytes > 0 ? bytes : 0, '\0');
    if (bytes > 0)
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), bytes, nullptr, nullptr);
    FILE* file = _wfopen(ServersPath().c_str(), L"wb");
    if (!file) return false;
    if (!utf8.empty()) fwrite(utf8.data(), 1, utf8.size(), file);
    fclose(file);
    return true;
}

std::vector<StoredServer> CombinedSites() {
    std::vector<StoredServer> sites = LoadServers();
    std::vector<std::wstring> names;
    names.reserve(sites.size());
    for (const auto& site : sites) names.push_back(Lower(site.name));

    for (const auto& host : TshHosts()) {
        if (std::find(names.begin(), names.end(), Lower(host.name)) != names.end()) continue;
        names.push_back(Lower(host.name));
        sites.push_back(host);
    }
    for (const auto& host : LoadSshHosts()) {
        if (std::find(names.begin(), names.end(), Lower(host.alias)) != names.end()) continue;
        StoredServer site;
        site.name = host.alias;
        site.host = host.hostName;
        site.port = host.port.empty() ? L"22" : host.port;
        site.user = host.user;
        site.proxy = host.proxyJump;
        site.fromConfig = true;
        site.keyLabel = host.keyLabel;
        site.identityFiles = host.identityFiles;
        sites.push_back(std::move(site));
    }
    for (auto& site : sites) {
        if (!site.keyLabel.empty()) continue;
        if (!site.keyFile.empty()) {
            const auto pos = site.keyFile.find_last_of(L"\\/");
            site.keyLabel = pos == std::wstring::npos ? site.keyFile : site.keyFile.substr(pos + 1);
        } else if (!site.identityFiles.empty()) {
            const auto& path = site.identityFiles.front();
            const auto pos = path.find_last_of(L"\\/");
            site.keyLabel = pos == std::wstring::npos ? path : path.substr(pos + 1);
        } else if (site.fromTeleport) {
            site.keyLabel = L"teleport";
        } else {
            site.keyLabel = site.password.empty() ? L"ssh-agent" : L"password";
        }
    }
    return sites;
}
