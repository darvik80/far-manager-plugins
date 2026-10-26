#include "teleport.hpp"
#include "ssh_config.hpp"
#include "ui.hpp"

#include <windows.h>

#include <string>
#include <vector>

namespace {

struct Cache {
    DWORD tick = 0;
    bool present = false;
    bool loggedIn = false;
    std::wstring exe;
    std::wstring user;
    std::wstring cluster;
    std::wstring proxy;
    std::vector<StoredServer> nodes;
    bool nodesLoaded = false;
};

Cache g_cache;
const DWORD kTtlMs = 15000;

std::wstring HomeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? buf : L".";
}

std::wstring TshDir() {
    return HomeDir() + L"\\.tsh";
}

std::wstring Quote(const std::wstring& text) {
    if (text.find_first_of(L" \t\"") == std::wstring::npos) return text;
    std::wstring out = L"\"";
    for (wchar_t ch : text) {
        if (ch == L'"') out += L'\\';
        out += ch;
    }
    out += L'"';
    return out;
}

std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    const int chars = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (chars <= 0) return {};
    std::wstring out(chars, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), chars);
    return out;
}

std::string JsonString(const std::string& text, const char* key) {
    const std::string needle = std::string("\"") + key + "\":";
    size_t pos = text.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) ++pos;
    if (pos >= text.size() || text[pos] != '"') return {};
    ++pos;
    std::string out;
    while (pos < text.size() && text[pos] != '"') {
        if (text[pos] == '\\' && pos + 1 < text.size()) {
            out += text[pos + 1];
            pos += 2;
            continue;
        }
        out += text[pos++];
    }
    return out;
}

std::wstring ProxyFromUrl(std::wstring url) {
    if (url.rfind(L"https://", 0) == 0) url = url.substr(8);
    else if (url.rfind(L"http://", 0) == 0) url = url.substr(7);
    while (!url.empty() && url.back() == L'/') url.pop_back();
    return url;
}

std::wstring FindTshExe() {
    const std::wstring home = HomeDir();
    const std::wstring candidates[] = {
        home + L"\\scoop\\apps\\teleport\\current\\tsh.exe",
        L"C:\\Program Files\\Teleport\\tsh.exe",
        L"C:\\Program Files\\Teleport Connect\\resources\\bin\\tsh.exe",
        home + L"\\scoop\\shims\\tsh.exe",
        home + L"\\AppData\\Local\\Programs\\teleport-connect\\resources\\bin\\tsh.exe",
    };
    for (const auto& path : candidates) {
        if (FileExists(path)) return path;
    }
    wchar_t found[MAX_PATH];
    if (SearchPathW(nullptr, L"tsh.exe", nullptr, MAX_PATH, found, nullptr)) return found;
    return {};
}

bool RunHidden(const std::wstring& command, std::string& out, DWORD& code, DWORD waitMs) {
    out.clear();
    code = 1;
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &security, 0)) return false;
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = write;
    startup.hStdError = write;
    PROCESS_INFORMATION process{};
    std::wstring line = command;
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        CloseHandle(read);
        CloseHandle(write);
        return false;
    }
    CloseHandle(write);
    CloseHandle(process.hThread);
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(read, buf, sizeof(buf), &n, nullptr) && n > 0) out.append(buf, n);
    CloseHandle(read);
    if (WaitForSingleObject(process.hProcess, waitMs) != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hProcess);
        return false;
    }
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    return true;
}

void ApplyStatus(const std::string& json, bool loggedIn) {
    g_cache.loggedIn = loggedIn;
    const std::wstring user = Wide(JsonString(json, "username"));
    const std::wstring cluster = Wide(JsonString(json, "cluster"));
    const std::wstring url = Wide(JsonString(json, "profile_url"));
    if (!user.empty()) g_cache.user = user;
    if (!cluster.empty()) g_cache.cluster = cluster;
    if (!url.empty()) g_cache.proxy = ProxyFromUrl(url);
    if (g_cache.proxy.empty() && FileExists(TshDir() + L"\\current-profile")) {
        FILE* file = _wfopen((TshDir() + L"\\current-profile").c_str(), L"rb");
        if (file) {
            char buf[256]{};
            const size_t n = fread(buf, 1, sizeof(buf) - 1, file);
            fclose(file);
            std::wstring name = Wide(std::string(buf, n));
            while (!name.empty() && (name.back() == L'\n' || name.back() == L'\r' || name.back() == L' ')) name.pop_back();
            if (!name.empty()) g_cache.cluster = name;
        }
    }
}

bool RefreshStatus() {
    const DWORD now = GetTickCount();
    if (g_cache.tick && now - g_cache.tick < kTtlMs) return g_cache.loggedIn;
    g_cache.tick = now;
    g_cache.exe = FindTshExe();
    g_cache.present = !g_cache.exe.empty();
    g_cache.loggedIn = false;
    g_cache.nodesLoaded = false;
    g_cache.nodes.clear();
    if (!g_cache.present) return false;
    std::string json;
    DWORD code = 1;
    const std::wstring command = Quote(g_cache.exe) + L" status --format=json";
    RunHidden(command, json, code, 15000);
    ApplyStatus(json, code == 0);
    return g_cache.loggedIn;
}

}  // namespace

void TshInvalidate() {
    g_cache = {};
}

bool TshAvailable() {
    RefreshStatus();
    return g_cache.present;
}

bool TshLoggedIn() {
    return RefreshStatus();
}

std::wstring TshExe() {
    RefreshStatus();
    return g_cache.exe;
}

std::wstring TshCluster() {
    RefreshStatus();
    return g_cache.cluster;
}

std::wstring TshProxy() {
    RefreshStatus();
    if (!g_cache.proxy.empty()) return g_cache.proxy;
    const std::wstring yaml = TshDir() + L"\\" + g_cache.cluster + L".yaml";
    if (g_cache.cluster.empty() || !FileExists(yaml)) return {};
    FILE* file = _wfopen(yaml.c_str(), L"rb");
    if (!file) return {};
    char buf[512]{};
    const size_t n = fread(buf, 1, sizeof(buf) - 1, file);
    fclose(file);
    const std::string text(buf, n);
    const std::string key = "web_proxy_addr:";
    const auto pos = text.find(key);
    if (pos == std::string::npos) return {};
    size_t i = pos + key.size();
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
    size_t j = i;
    while (j < text.size() && text[j] != '\r' && text[j] != '\n') ++j;
    return Wide(text.substr(i, j - i));
}

std::wstring TshIdentityFile() {
    RefreshStatus();
    if (g_cache.cluster.empty() || g_cache.user.empty()) return {};
    return TshDir() + L"\\keys\\" + g_cache.cluster + L"\\" + g_cache.user;
}

std::wstring TshCertificateFile() {
    RefreshStatus();
    if (g_cache.cluster.empty() || g_cache.user.empty()) return {};
    return TshDir() + L"\\keys\\" + g_cache.cluster + L"\\" + g_cache.user + L"-ssh\\" + g_cache.cluster + L"-cert.pub";
}

std::wstring TshKnownHostsFile() {
    return TshDir() + L"\\known_hosts";
}

bool TshLogin(std::wstring& error) {
    RefreshStatus();
    if (!g_cache.present) {
        error = L"tsh не найден. Установи Teleport.";
        return false;
    }
    std::wstring inner = Quote(g_cache.exe) + L" login --mfa-mode=otp";
    if (!g_cache.proxy.empty()) inner += L" --proxy=" + Quote(g_cache.proxy);
    std::wstring command = L"cmd.exe /c \"" + inner + L" & if errorlevel 1 pause\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOW;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &startup, &process)) {
        error = L"Не удалось запустить tsh login.";
        return false;
    }
    CloseHandle(process.hThread);
    const DWORD wait = WaitForSingleObject(process.hProcess, 10 * 60 * 1000);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    TshInvalidate();
    if (wait != WAIT_OBJECT_0) {
        error = L"tsh login не завершился.";
        return false;
    }
    if (code != 0) {
        error = L"tsh login не удался. В окне tsh введи пароль и OTP.";
        return false;
    }
    return TshLoggedIn();
}

std::vector<StoredServer> TshHosts() {
    if (!RefreshStatus() || !g_cache.present) return {};
    if (g_cache.nodesLoaded) return g_cache.nodes;
    g_cache.nodesLoaded = true;
    std::string text;
    DWORD code = 1;
    RunHidden(Quote(g_cache.exe) + L" ls --format=names", text, code, 20000);
    if (code != 0) return {};
    StoredServer site;
    site.user = g_cache.user;
    site.port = L"3022";
    site.fromTeleport = true;
    site.keyLabel = L"teleport";
    std::string line;
    auto flush = [&] {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) return;
        site.name = Wide(line);
        site.host = site.name;
        g_cache.nodes.push_back(site);
        line.clear();
    };
    for (char ch : text) {
        if (ch == '\n') flush();
        else line += ch;
    }
    flush();
    return g_cache.nodes;
}
