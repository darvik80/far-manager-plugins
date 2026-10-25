#include "ssh_config.hpp"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <set>

namespace {

std::wstring HomeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return buf;
    n = GetEnvironmentVariableW(L"HOME", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return buf;
    return L".";
}

std::wstring ToLower(std::wstring s) {
    for (auto& ch : s) ch = towlower(ch);
    return s;
}

std::wstring BaseName(const std::wstring& path) {
    const auto pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

std::wstring FullPath(const std::wstring& path) {
    wchar_t buf[MAX_PATH];
    DWORD n = GetFullPathNameW(path.c_str(), MAX_PATH, buf, nullptr);
    if (n == 0 || n >= MAX_PATH) return path;
    return buf;
}

bool IsAbsolute(const std::wstring& path) {
    if (path.size() >= 2 && path[1] == L':') return true;
    if (!path.empty() && (path[0] == L'\\' || path[0] == L'/')) return true;
    return false;
}

std::wstring ExpandUserPath(std::wstring path) {
    if (!path.empty() && path[0] == L'~' && (path.size() == 1 || path[1] == L'/' || path[1] == L'\\'))
        path = HomeDir() + path.substr(1);
    for (auto& ch : path)
        if (ch == L'/') ch = L'\\';
    return path;
}

std::wstring ReplaceAll(std::wstring text, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return text;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::wstring::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

struct Block {
    std::vector<std::wstring> patterns;
    std::wstring hostName;
    std::wstring user;
    std::wstring port;
    std::wstring proxyJump;
    std::vector<std::wstring> identityFiles;
    bool hostNameSet = false;
    bool userSet = false;
    bool portSet = false;
    bool proxySet = false;
    bool skip = false;
};

bool GlobMatch(const wchar_t* pat, const wchar_t* text) {
    while (*pat == L'*') {
        ++pat;
        if (!*pat) return true;
        for (const wchar_t* rest = text; *rest; ++rest)
            if (GlobMatch(pat, rest)) return true;
        return false;
    }
    if (!*pat) return !*text;
    if (!*text) return false;
    if (*pat != L'?' && towlower(*pat) != towlower(*text)) return false;
    return GlobMatch(pat + 1, text + 1);
}

bool BlockMatches(const Block& block, const std::wstring& host) {
    bool positive = false;
    for (const auto& pattern : block.patterns) {
        if (pattern.empty()) continue;
        if (pattern[0] == L'!') {
            if (GlobMatch(pattern.c_str() + 1, host.c_str())) return false;
        } else if (GlobMatch(pattern.c_str(), host.c_str())) {
            positive = true;
        }
    }
    return positive;
}

std::vector<std::wstring> Tokenize(const std::wstring& line) {
    std::vector<std::wstring> tokens;
    std::wstring current;
    bool quoted = false;
    bool any = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const wchar_t ch = line[i];
        if (!quoted && ch == L'#') break;
        if (ch == L'"') {
            quoted = !quoted;
            any = true;
            continue;
        }
        if (!quoted && (ch == L' ' || ch == L'\t' || ch == L'=')) {
            if (any) {
                tokens.push_back(current);
                current.clear();
                any = false;
            }
            continue;
        }
        current.push_back(ch);
        any = true;
    }
    if (any) tokens.push_back(current);
    return tokens;
}

std::wstring ReadText(const std::wstring& path) {
    FILE* file = _wfopen(path.c_str(), L"rb");
    if (!file) return {};
    std::string bytes;
    char buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), file)) > 0)
        bytes.append(buf, n);
    fclose(file);
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }
    if (bytes.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    UINT codePage = CP_UTF8;
    if (needed <= 0) {
        codePage = CP_ACP;
        needed = MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    }
    if (needed <= 0) return {};
    std::wstring text(needed, L'\0');
    MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), needed);
    return text;
}

std::vector<std::wstring> LogicalLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    std::wstring current;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r') continue;
        if (text[i] != L'\n') {
            current.push_back(text[i]);
            continue;
        }
        if (!current.empty() && current.back() == L'\\') {
            current.pop_back();
            continue;
        }
        lines.push_back(current);
        current.clear();
    }
    if (!current.empty()) lines.push_back(current);
    return lines;
}

std::vector<std::wstring> ExpandInclude(const std::wstring& raw) {
    std::wstring path = ExpandUserPath(raw);
    if (!IsAbsolute(path))
        path = SshDirectory() + L"\\" + path;

    std::vector<std::wstring> files;
    if (path.find_first_of(L"*?") == std::wstring::npos) {
        if (FileExists(path)) files.push_back(FullPath(path));
        return files;
    }

    const auto slash = path.find_last_of(L'\\');
    const std::wstring dir = slash == std::wstring::npos ? L"." : path.substr(0, slash);
    WIN32_FIND_DATAW data{};
    HANDLE found = FindFirstFileW(path.c_str(), &data);
    if (found == INVALID_HANDLE_VALUE) return files;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        files.push_back(FullPath(dir + L"\\" + data.cFileName));
    } while (FindNextFileW(found, &data));
    FindClose(found);
    std::sort(files.begin(), files.end());
    return files;
}

void ParseFile(const std::wstring& path, std::vector<Block>& blocks, std::set<std::wstring>& seen, int depth) {
    const std::wstring full = ToLower(FullPath(path));
    if (!seen.insert(full).second || depth > 5) return;

    Block* current = nullptr;
    bool skippingMatch = false;
    for (const auto& line : LogicalLines(ReadText(path))) {
        const auto tokens = Tokenize(line);
        if (tokens.empty()) continue;
        const std::wstring key = ToLower(tokens[0]);
        if (key == L"include") {
            for (size_t i = 1; i < tokens.size(); ++i)
                for (const auto& file : ExpandInclude(tokens[i]))
                    ParseFile(file, blocks, seen, depth + 1);
            current = blocks.empty() ? nullptr : &blocks.back();
            continue;
        }
        if (key == L"host") {
            skippingMatch = false;
            blocks.push_back(Block{});
            current = &blocks.back();
            for (size_t i = 1; i < tokens.size(); ++i)
                current->patterns.push_back(tokens[i]);
            continue;
        }
        if (key == L"match") {
            skippingMatch = true;
            current = nullptr;
            continue;
        }
        if (skippingMatch || !current || tokens.size() < 2) continue;
        const std::wstring value = tokens[1];
        if (key == L"hostname" && !current->hostNameSet) {
            current->hostName = value;
            current->hostNameSet = true;
        } else if (key == L"user" && !current->userSet) {
            current->user = value;
            current->userSet = true;
        } else if (key == L"port" && !current->portSet) {
            current->port = value;
            current->portSet = true;
        } else if (key == L"proxyjump" && !current->proxySet) {
            current->proxyJump = value;
            current->proxySet = true;
        } else if (key == L"identityfile") {
            current->identityFiles.push_back(value);
        }
    }
}

bool IsConcreteHost(const std::wstring& pattern) {
    if (pattern.empty() || pattern[0] == L'!') return false;
    return pattern.find_first_of(L"*?") == std::wstring::npos;
}

std::wstring ExpandIdentity(const std::wstring& raw, const std::wstring& host, const std::wstring& user) {
    std::wstring path = ReplaceAll(raw, L"%h", host);
    path = ReplaceAll(path, L"%r", user);
    path = ExpandUserPath(path);
    if (!IsAbsolute(path))
        path = SshDirectory() + L"\\" + path;
    return path;
}

std::wstring KeyLabel(const std::vector<std::wstring>& keys) {
    if (keys.empty()) {
        const wchar_t* defaults[] = { L"id_ed25519", L"id_ecdsa", L"id_rsa" };
        for (const wchar_t* name : defaults) {
            if (FileExists(SshDirectory() + L"\\" + name))
                return name;
        }
        return L"ssh-agent";
    }

    std::wstring found;
    int extra = 0;
    bool ppk = false;
    for (const auto& key : keys) {
        const std::wstring base = BaseName(key);
        if (base.size() >= 4 && ToLower(base.substr(base.size() - 4)) == L".ppk")
            ppk = true;
        if (!FileExists(key)) continue;
        if (found.empty()) found = base;
        else ++extra;
    }
    if (!found.empty()) {
        if (extra > 0) found += L" +" + std::to_wstring(extra);
        return found;
    }
    if (ppk) return L"ppk, not OpenSSH";
    return BaseName(keys.front()) + L" missing";
}

struct Resolved {
    std::wstring user;
    std::wstring hostName;
    std::wstring port;
    std::wstring proxyJump;
    std::vector<std::wstring> keys;
};

Resolved Resolve(const std::vector<Block>& blocks, const std::wstring& alias) {
    Resolved result;
    bool hostSet = false;
    bool userSet = false;
    bool portSet = false;
    bool proxySet = false;
    for (const auto& block : blocks) {
        if (block.skip || !BlockMatches(block, alias)) continue;
        if (!hostSet && block.hostNameSet) {
            result.hostName = block.hostName;
            hostSet = true;
        }
        if (!userSet && block.userSet) {
            result.user = block.user;
            userSet = true;
        }
        if (!portSet && block.portSet) {
            result.port = block.port;
            portSet = true;
        }
        if (!proxySet && block.proxySet) {
            result.proxyJump = block.proxyJump;
            proxySet = true;
        }
        const std::wstring host = result.hostName.empty() ? alias : result.hostName;
        for (const auto& identity : block.identityFiles)
            result.keys.push_back(ExpandIdentity(identity, host, result.user));
    }
    return result;
}

}  // namespace

std::wstring SshDirectory() {
    return HomeDir() + L"\\.ssh";
}

std::wstring SshConfigPath() {
    return SshDirectory() + L"\\config";
}

bool FileExists(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::vector<std::wstring> DefaultIdentityFiles() {
    std::vector<std::wstring> files;
    const wchar_t* names[] = { L"id_ed25519", L"id_ecdsa", L"id_rsa" };
    for (const wchar_t* name : names) {
        std::wstring path = SshDirectory() + L"\\" + name;
        if (FileExists(path)) files.push_back(path);
    }
    return files;
}

std::vector<SshHost> LoadSshHosts() {
    std::vector<Block> blocks;
    std::set<std::wstring> seen;
    if (FileExists(SshConfigPath()))
        ParseFile(SshConfigPath(), blocks, seen, 0);

    std::vector<SshHost> hosts;
    std::set<std::wstring> aliases;
    for (const auto& block : blocks) {
        for (const auto& pattern : block.patterns) {
            if (!IsConcreteHost(pattern)) continue;
            if (!aliases.insert(ToLower(pattern)).second) continue;
            const Resolved resolved = Resolve(blocks, pattern);
            SshHost host;
            host.alias = pattern;
            host.user = resolved.user;
            host.hostName = resolved.hostName.empty() ? pattern : resolved.hostName;
            host.port = resolved.port;
            host.proxyJump = resolved.proxyJump;
            host.keyLabel = KeyLabel(resolved.keys);
            host.identityFiles = resolved.keys;
            if (host.identityFiles.empty())
                host.identityFiles = DefaultIdentityFiles();
            hosts.push_back(std::move(host));
        }
    }
    return hosts;
}
