#include "sftp_client.hpp"
#include "ssh_config.hpp"
#include "teleport.hpp"
#include "ui.hpp"
#include "vault_link.hpp"

#include <libssh2.h>
#include <libssh2_sftp.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(bytes > 0 ? bytes : 0, '\0');
    if (bytes > 0)
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), bytes, nullptr, nullptr);
    return out;
}

std::wstring Wide(const char* text) {
    if (!text || !*text) return {};
    int chars = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (chars <= 0) return {};
    std::wstring out(chars - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), chars);
    return out;
}

std::wstring QuoteArg(const std::wstring& text) {
    if (text.find_first_of(L" \t\"") == std::wstring::npos) return text;
    std::wstring out = L"\"";
    for (wchar_t ch : text) {
        if (ch == L'"') out += L'\\';
        out += ch;
    }
    out += L'"';
    return out;
}

std::wstring FindSshExe() {
    wchar_t windir[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"SystemRoot", windir, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring bundled = std::wstring(windir) + L"\\System32\\OpenSSH\\ssh.exe";
        if (FileExists(bundled)) return bundled;
    }
    wchar_t found[MAX_PATH];
    DWORD got = SearchPathW(nullptr, L"ssh.exe", nullptr, MAX_PATH, found, nullptr);
    if (got > 0 && got < MAX_PATH) return found;
    return {};
}

std::string AppKnownHosts() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(buf) : L".";
    dir += L"\\Far Manager\\SshClient";
    CreateDirectoryW((dir.substr(0, dir.rfind(L'\\'))).c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    return Utf8(dir + L"\\known_hosts");
}

ssize_t JumpRecv(libssh2_socket_t, void* buffer, size_t length, int, void** abstract) {
    auto* self = static_cast<SftpConnection*>(*abstract);
    if (!self->WaitJumpData(25000)) return -1;
    DWORD read = 0;
    const DWORD want = static_cast<DWORD>(length > 1 << 20 ? 1 << 20 : length);
    if (!ReadFile(self->JumpOut(), buffer, want, &read, nullptr) || read == 0) return -1;
    return static_cast<ssize_t>(read);
}

ssize_t JumpSend(libssh2_socket_t, const void* buffer, size_t length, int, void** abstract) {
    auto* self = static_cast<SftpConnection*>(*abstract);
    const char* data = static_cast<const char*>(buffer);
    size_t sent = 0;
    while (sent < length) {
        DWORD wrote = 0;
        const DWORD chunk = static_cast<DWORD>(length - sent > 1 << 20 ? 1 << 20 : length - sent);
        if (!WriteFile(self->JumpIn(), data + sent, chunk, &wrote, nullptr) || wrote == 0) return -1;
        sent += wrote;
    }
    return static_cast<ssize_t>(sent);
}

DWORD WINAPI DrainJumpErr(LPVOID param) {
    auto* self = static_cast<SftpConnection*>(param);
    const HANDLE pipe = self->JumpErr();
    if (!pipe) return 0;
    char buf[512];
    DWORD n = 0;
    while (ReadFile(pipe, buf, sizeof(buf), &n, nullptr) && n > 0) {}
    return 0;
}

void PutU32(std::string& p, uint32_t v) {
    p.push_back(static_cast<char>(v >> 24));
    p.push_back(static_cast<char>(v >> 16));
    p.push_back(static_cast<char>(v >> 8));
    p.push_back(static_cast<char>(v));
}

void PutU64(std::string& p, uint64_t v) {
    PutU32(p, static_cast<uint32_t>(v >> 32));
    PutU32(p, static_cast<uint32_t>(v));
}

void PutStr(std::string& p, const std::string& s) {
    PutU32(p, static_cast<uint32_t>(s.size()));
    p += s;
}

bool GetU32(const std::string& p, size_t& i, uint32_t& v) {
    if (i + 4 > p.size()) return false;
    v = (static_cast<uint32_t>(static_cast<uint8_t>(p[i])) << 24) |
        (static_cast<uint32_t>(static_cast<uint8_t>(p[i + 1])) << 16) |
        (static_cast<uint32_t>(static_cast<uint8_t>(p[i + 2])) << 8) |
        static_cast<uint8_t>(p[i + 3]);
    i += 4;
    return true;
}

bool GetU64(const std::string& p, size_t& i, uint64_t& v) {
    uint32_t hi = 0, lo = 0;
    if (!GetU32(p, i, hi) || !GetU32(p, i, lo)) return false;
    v = (static_cast<uint64_t>(hi) << 32) | lo;
    return true;
}

bool GetStr(const std::string& p, size_t& i, std::string& s) {
    uint32_t n = 0;
    if (!GetU32(p, i, n) || i + n > p.size()) return false;
    s.assign(p.data() + i, n);
    i += n;
    return true;
}

bool ParseAttrs(const std::string& p, size_t& i, uint32_t& flags, uint64_t& size, uint32_t& perm, uint32_t& mtime) {
    flags = 0;
    size = 0;
    perm = 0;
    mtime = 0;
    if (!GetU32(p, i, flags)) return false;
    if (flags & 1u) {
        if (!GetU64(p, i, size)) return false;
    }
    if (flags & 2u) {
        uint32_t uid = 0, gid = 0;
        if (!GetU32(p, i, uid) || !GetU32(p, i, gid)) return false;
    }
    if (flags & 4u) {
        if (!GetU32(p, i, perm)) return false;
    }
    if (flags & 8u) {
        uint32_t atime = 0;
        if (!GetU32(p, i, atime) || !GetU32(p, i, mtime)) return false;
    }
    if (flags & 0x80000000u) {
        uint32_t n = 0;
        if (!GetU32(p, i, n)) return false;
        for (uint32_t k = 0; k < n; ++k) {
            std::string a, b;
            if (!GetStr(p, i, a) || !GetStr(p, i, b)) return false;
        }
    }
    return true;
}

int KeyMask(int type) {
    switch (type) {
        case LIBSSH2_HOSTKEY_TYPE_RSA: return LIBSSH2_KNOWNHOST_KEY_SSHRSA;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: return LIBSSH2_KNOWNHOST_KEY_ECDSA_256;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: return LIBSSH2_KNOWNHOST_KEY_ECDSA_384;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: return LIBSSH2_KNOWNHOST_KEY_ECDSA_521;
        case LIBSSH2_HOSTKEY_TYPE_ED25519: return LIBSSH2_KNOWNHOST_KEY_ED25519;
        default: return LIBSSH2_KNOWNHOST_KEY_UNKNOWN;
    }
}

bool SafeCmdTitle(const std::wstring& text) {
    if (text.empty()) return false;
    return text.find_first_of(L"&|<>\"^%") == std::wstring::npos;
}

bool LaunchConsole(const std::wstring& inner, const std::wstring& title, const std::wstring& cleanup, std::wstring& error) {
    std::wstring command = L"cmd.exe /s /c \"";
    if (SafeCmdTitle(title)) command += L"title " + title + L" & ";
    command += inner;
    command += L" || pause";
    if (!cleanup.empty()) command += L" & del /f /q " + QuoteArg(cleanup);
    command += L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOW;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                        CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &startup, &process)) {
        error = L"Не удалось открыть SSH-терминал.";
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

}  // namespace

bool OpenSshTerminal(const StoredServer& server, std::wstring& error) {
    if (server.host.empty()) {
        error = L"Host is empty.";
        return false;
    }
    std::wstring inner;
    std::wstring cleanup;
    const std::wstring title = server.name.empty() ? server.host : server.name;
    if (server.fromTeleport) {
        const std::wstring tsh = TshExe();
        if (tsh.empty()) {
            error = L"tsh не найден.";
            return false;
        }
        std::wstring target = server.user.empty() ? server.host : server.user + L"@" + server.host;
        inner = QuoteArg(tsh) + L" ssh --mfa-mode=otp " + QuoteArg(target);
    } else {
        const std::wstring ssh = FindSshExe();
        if (ssh.empty()) {
            error = L"Не найден ssh.exe (OpenSSH).";
            return false;
        }
        inner = QuoteArg(ssh) + L" -t";
        if (FileExists(SshConfigPath())) inner += L" -F " + QuoteArg(SshConfigPath());
        if (server.fromConfig) {
            inner += L" " + QuoteArg(server.name);
        } else {
            if (!server.port.empty() && server.port != L"22")
                inner += L" -p " + QuoteArg(server.port);
            if (!server.proxy.empty())
                inner += L" -J " + QuoteArg(server.proxy);
            std::wstring key = server.keyFile;
            if (key.empty() && !server.vaultKey.empty()) {
                key = VaultLookupKeyFile(server.vaultKey);
                cleanup = key;
            }
            if (key.empty()) {
                for (const auto& id : server.identityFiles) {
                    if (FileExists(id)) {
                        key = id;
                        break;
                    }
                }
            }
            if (!key.empty() && FileExists(key)) inner += L" -i " + QuoteArg(key);
            else cleanup.clear();
            std::wstring target = server.user.empty() ? server.host : server.user + L"@" + server.host;
            inner += L" " + QuoteArg(target);
        }
    }
    return LaunchConsole(inner, title, cleanup, error);
}

void StartupNetwork() {
    static bool ready = false;
    if (ready) return;
    WSADATA data{};
    WSAStartup(MAKEWORD(2, 2), &data);
    libssh2_init(0);
    ready = true;
}

void ShutdownNetwork() {
    libssh2_exit();
    WSACleanup();
}

SftpConnection::~SftpConnection() {
    Close();
}

bool SftpConnection::WaitJumpData(DWORD timeoutMs) const {
    if (closing_ || !jumpOut_) return false;
    const DWORD start = GetTickCount();
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(jumpOut_, nullptr, 0, nullptr, &avail, nullptr))
            return false;
        if (avail > 0) return true;
        DWORD code = 0;
        if (jumpProcess_ && GetExitCodeProcess(jumpProcess_, &code) && code != STILL_ACTIVE)
            return false;
        if (GetTickCount() - start >= timeoutMs) return false;
        Sleep(20);
    }
}

void SftpConnection::Close() {
    closing_ = true;
    if (jumpIn_) { CloseHandle(jumpIn_); jumpIn_ = nullptr; }
    if (jumpProcess_) {
        TerminateProcess(jumpProcess_, 0);
        WaitForSingleObject(jumpProcess_, 400);
        CloseHandle(jumpProcess_);
        jumpProcess_ = nullptr;
    }
    if (jumpOut_) { CloseHandle(jumpOut_); jumpOut_ = nullptr; }
    if (jumpErr_) { CloseHandle(jumpErr_); jumpErr_ = nullptr; }
    if (drainThread_) {
        WaitForSingleObject(drainThread_, 400);
        CloseHandle(drainThread_);
        drainThread_ = nullptr;
    }
    pipeSftp_ = false;
    sftpNextId_ = 1;
    if (sftp_) {
        libssh2_sftp_shutdown(static_cast<LIBSSH2_SFTP*>(sftp_));
        sftp_ = nullptr;
    }
    if (session_) {
        libssh2_session_free(static_cast<LIBSSH2_SESSION*>(session_));
        session_ = nullptr;
    }
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }
    home_.clear();
    closing_ = false;
}

std::wstring SftpConnection::LastError(const wchar_t* prefix) const {
    char* message = nullptr;
    if (session_) libssh2_session_last_error(static_cast<LIBSSH2_SESSION*>(session_), &message, nullptr, 0);
    std::wstring text = prefix;
    if (message && *message) text += L": " + Wide(message);
    return text;
}

bool SftpConnection::TcpConnect(const std::string& host, const std::string& port, std::wstring& error) {
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0) {
        error = L"Could not resolve " + Wide(host.c_str());
        return false;
    }
    for (addrinfo* it = result; it; it = it->ai_next) {
        SOCKET sock = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (sock == INVALID_SOCKET) continue;
        u_long nonblock = 1;
        ioctlsocket(sock, FIONBIO, &nonblock);
        connect(sock, it->ai_addr, static_cast<int>(it->ai_addrlen));
        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(sock, &writeSet);
        timeval wait{20, 0};
        const int selected = select(0, nullptr, &writeSet, nullptr, &wait);
        nonblock = 0;
        ioctlsocket(sock, FIONBIO, &nonblock);
        if (selected > 0) {
            sock_ = sock;
            break;
        }
        closesocket(sock);
    }
    freeaddrinfo(result);
    if (sock_ == INVALID_SOCKET) {
        error = L"Could not connect to " + Wide(host.c_str()) + L":" + Wide(port.c_str());
        return false;
    }
    return true;
}

bool SftpConnection::PipeConnect(const std::wstring& command, std::wstring& error) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE outRead = nullptr, outWrite = nullptr, inRead = nullptr, inWrite = nullptr, errRead = nullptr, errWrite = nullptr;
    CreatePipe(&outRead, &outWrite, &security, 0);
    CreatePipe(&inRead, &inWrite, &security, 0);
    CreatePipe(&errRead, &errWrite, &security, 0);
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = inRead;
    startup.hStdOutput = outWrite;
    startup.hStdError = errWrite;
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');
    const BOOL started = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(inRead);
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    if (!started) {
        CloseHandle(outRead);
        CloseHandle(inWrite);
        CloseHandle(errRead);
        error = L"Не удалось запустить прокси SSH.";
        return false;
    }
    CloseHandle(process.hThread);
    jumpProcess_ = process.hProcess;
    jumpIn_ = inWrite;
    jumpOut_ = outRead;
    jumpErr_ = errRead;
    drainThread_ = CreateThread(nullptr, 0, DrainJumpErr, this, 0, nullptr);
    return true;
}

bool SftpConnection::JumpConnect(const StoredServer& server, std::wstring& error) {
    const std::wstring ssh = FindSshExe();
    if (ssh.empty()) {
        error = L"OpenSSH is required for ProxyJump.";
        return false;
    }
    std::wstring command = L"\"" + ssh + L"\"";
    if (FileExists(SshConfigPath())) command += L" -F " + QuoteArg(SshConfigPath());
    command += L" -o BatchMode=yes -W " + server.host + L":" + (server.port.empty() ? L"22" : server.port);
    command += L" " + QuoteArg(server.proxy);
    return PipeConnect(command, error);
}

bool SftpConnection::TshConnect(const StoredServer& server, std::wstring& error) {
    if (!TshLoggedIn()) {
        error = L"Сессия Teleport истекла. F6 — tsh login.";
        return false;
    }
    const std::wstring ssh = FindSshExe();
    if (ssh.empty()) {
        error = L"Не найден ssh.exe (OpenSSH).";
        return false;
    }
    const std::wstring alias = server.name.empty() ? server.host : server.name;
    std::wstring target = server.user.empty() ? alias : server.user + L"@" + alias;
    std::wstring command = QuoteArg(ssh) + L" -T -o BatchMode=yes -o ConnectTimeout=20";
    if (FileExists(SshConfigPath())) command += L" -F " + QuoteArg(SshConfigPath());
    command += L" -s " + QuoteArg(target) + L" sftp";
    if (!PipeConnect(command, error)) return false;
    DWORD code = 0;
    if (jumpProcess_ && GetExitCodeProcess(jumpProcess_, &code) && code != STILL_ACTIVE) {
        error = L"ssh/tsh сразу вышел. F6 — tsh login, либо F2 — терминал.";
        return false;
    }
    return true;
}

bool SftpConnection::CheckHostKey(const std::string& host, int port, std::wstring& error, bool viaTsh) {
    auto* session = static_cast<LIBSSH2_SESSION*>(session_);
    size_t keyLen = 0;
    int keyType = 0;
    const char* key = libssh2_session_hostkey(session, &keyLen, &keyType);
    if (!key || keyLen == 0) {
        error = L"Server did not present a host key.";
        return false;
    }
    const int mask = LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | KeyMask(keyType);
    LIBSSH2_KNOWNHOSTS* hosts = libssh2_knownhost_init(session);
    if (!hosts) return true;
    const std::string userFile = Utf8(SshDirectory() + L"\\known_hosts");
    const std::string ourFile = AppKnownHosts();
    const std::string tshFile = Utf8(TshKnownHostsFile());
    libssh2_knownhost_readfile(hosts, userFile.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    libssh2_knownhost_readfile(hosts, ourFile.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    if (FileExists(TshKnownHostsFile()))
        libssh2_knownhost_readfile(hosts, tshFile.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    const int check = libssh2_knownhost_checkp(hosts, host.c_str(), port, key, keyLen, mask, nullptr);
    bool ok = check == LIBSSH2_KNOWNHOST_CHECK_MATCH;
    if (!ok && viaTsh) {
        ok = true;
    } else if (!ok) {
        const wchar_t* question = check == LIBSSH2_KNOWNHOST_CHECK_MISMATCH
            ? L"Host key MISMATCH. This may be an attack. Connect anyway?"
            : L"Unknown host key. Trust this server and remember it?";
        ok = AskYes(question);
        if (ok) {
            libssh2_knownhost_addc(hosts, host.c_str(), nullptr, key, keyLen, nullptr, 0, mask, nullptr);
            libssh2_knownhost_writefile(hosts, ourFile.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
        }
    }
    libssh2_knownhost_free(hosts);
    if (!ok) error = L"Host key was not accepted.";
    return ok;
}

bool TryKeyFile(void* sessionPtr, const std::string& user, const std::wstring& key, const std::wstring& cert, bool wipe) {
    auto* session = static_cast<LIBSSH2_SESSION*>(sessionPtr);
    if (key.empty() || !FileExists(key)) return false;
    const std::string path = Utf8(key);
    const std::string pub = !cert.empty() && FileExists(cert) ? Utf8(cert) : std::string();
    const char* publicKey = pub.empty() ? nullptr : pub.c_str();
    int rc = libssh2_userauth_publickey_fromfile(session, user.c_str(), publicKey, path.c_str(), "");
    if (rc == 0) {
        if (wipe) DeleteFileW(key.c_str());
        return true;
    }
    if (rc == LIBSSH2_ERROR_FILE) {
        const std::wstring pass = AskText(L"Key passphrase", key.c_str(), true);
        if (!pass.empty())
            rc = libssh2_userauth_publickey_fromfile(session, user.c_str(), publicKey, path.c_str(), Utf8(pass).c_str());
    }
    if (wipe) DeleteFileW(key.c_str());
    return rc == 0;
}

bool SftpConnection::Authenticate(const StoredServer& server, std::wstring& error) {
    auto* session = static_cast<LIBSSH2_SESSION*>(session_);
    const std::string user = Utf8(server.user);
    if (!server.vaultKey.empty()) {
        const std::wstring temp = VaultLookupKeyFile(server.vaultKey);
        if (temp.empty()) {
            error = L"Ключ не найден в Secret Vault / NoteKeeper: " + server.vaultKey;
            return false;
        }
        if (TryKeyFile(session, user, temp, {}, true)) return true;
    }
    if (server.fromTeleport) {
        if (TryKeyFile(session, user, TshIdentityFile(), TshCertificateFile(), false)) return true;
        if (TryKeyFile(session, user, TshIdentityFile(), {}, false)) return true;
    }
    std::vector<std::wstring> keys = server.identityFiles;
    if (!server.keyFile.empty()) keys.insert(keys.begin(), server.keyFile);
    if (keys.empty() && server.vaultKey.empty() && !server.fromTeleport) keys = DefaultIdentityFiles();
    for (const auto& key : keys) {
        if (TryKeyFile(session, user, key, {}, false)) return true;
    }

    if (server.fromTeleport) {
        error = LastError(L"Teleport: сертификат не принят. F6 — tsh login");
        return false;
    }
    std::wstring secret = server.password;
    if (secret.empty() && !server.vaultPassword.empty())
        secret = VaultLookupPassword(server.vaultPassword);
    if (secret.empty())
        secret = VaultLookupPasswordForHost(server.host, server.user);
    std::string password = Utf8(secret);
    if (password.empty()) {
        const std::wstring typed = AskText(L"Password", server.host.c_str(), true);
        password = Utf8(typed);
    }
    if (!password.empty() && libssh2_userauth_password(session, user.c_str(), password.c_str()) == 0)
        return true;
    error = LastError(L"Authentication failed");
    return false;
}

bool SftpConnection::PipeWriteAll(const void* data, size_t length) {
    const auto* p = static_cast<const uint8_t*>(data);
    size_t sent = 0;
    while (sent < length) {
        if (closing_ || !jumpIn_) return false;
        DWORD wrote = 0;
        const DWORD chunk = static_cast<DWORD>(length - sent > 1 << 16 ? 1 << 16 : length - sent);
        if (!WriteFile(jumpIn_, p + sent, chunk, &wrote, nullptr) || wrote == 0) return false;
        sent += wrote;
    }
    return true;
}

bool SftpConnection::PipeReadAll(void* data, size_t length, DWORD timeoutMs) {
    auto* p = static_cast<uint8_t*>(data);
    size_t got = 0;
    const DWORD start = GetTickCount();
    while (got < length) {
        if (closing_ || !jumpOut_) return false;
        const DWORD elapsed = GetTickCount() - start;
        if (elapsed >= timeoutMs) return false;
        if (!WaitJumpData(timeoutMs - elapsed)) return false;
        DWORD n = 0;
        const DWORD want = static_cast<DWORD>(length - got > 1 << 16 ? 1 << 16 : length - got);
        if (!ReadFile(jumpOut_, p + got, want, &n, nullptr) || n == 0) return false;
        got += n;
    }
    return true;
}

bool SftpConnection::PipeSftpRecv(uint8_t& type, uint32_t& id, std::string& payload, std::wstring& error) {
    uint8_t lenb[4];
    if (!PipeReadAll(lenb, 4, 25000)) {
        error = L"SFTP: нет ответа. F6 — tsh login, либо F2.";
        return false;
    }
    const uint32_t len = (static_cast<uint32_t>(lenb[0]) << 24) | (static_cast<uint32_t>(lenb[1]) << 16) |
        (static_cast<uint32_t>(lenb[2]) << 8) | lenb[3];
    if (len < 1 || len > 8 * 1024 * 1024) {
        error = L"SFTP: битый пакет.";
        return false;
    }
    std::string body(len, '\0');
    if (!PipeReadAll(body.data(), len, 25000)) {
        error = L"SFTP: пакет оборвался.";
        return false;
    }
    type = static_cast<uint8_t>(body[0]);
    if (type == 1 || type == 2) {
        id = 0;
        payload = body.substr(1);
        return true;
    }
    if (len < 5) {
        error = L"SFTP: короткий пакет.";
        return false;
    }
    id = (static_cast<uint32_t>(static_cast<uint8_t>(body[1])) << 24) |
        (static_cast<uint32_t>(static_cast<uint8_t>(body[2])) << 16) |
        (static_cast<uint32_t>(static_cast<uint8_t>(body[3])) << 8) |
        static_cast<uint8_t>(body[4]);
    payload = body.substr(5);
    return true;
}

bool SftpConnection::PipeSftpRpc(uint8_t type, const std::string& body, uint8_t expect, std::string& reply, uint32_t* status, std::wstring& error) {
    const uint32_t id = sftpNextId_++;
    std::string inner;
    inner.push_back(static_cast<char>(type));
    PutU32(inner, id);
    inner += body;
    std::string wire;
    PutU32(wire, static_cast<uint32_t>(inner.size()));
    wire += inner;
    if (!PipeWriteAll(wire.data(), wire.size())) {
        error = L"SFTP: запись оборвалась.";
        return false;
    }
    for (;;) {
        uint8_t rtype = 0;
        uint32_t rid = 0;
        std::string payload;
        if (!PipeSftpRecv(rtype, rid, payload, error)) return false;
        if (rid != id) continue;
        if (rtype == 101) {
            uint32_t code = 4;
            std::string msg;
            size_t i = 0;
            GetU32(payload, i, code);
            GetStr(payload, i, msg);
            if (status) *status = code;
            if (code == 0 && (expect == 101 || expect == 0)) {
                reply = payload;
                return true;
            }
            if (code == 1 && (expect == 103 || expect == 104)) {
                reply.clear();
                return true;
            }
            error = L"SFTP: " + Wide(msg.empty() ? "ошибка" : msg.c_str());
            return false;
        }
        if (expect && rtype != expect) {
            error = L"SFTP: неожиданный ответ.";
            return false;
        }
        reply = payload;
        if (status) *status = 0;
        return true;
    }
}

bool SftpConnection::PipeSftpCloseHandle(const std::string& handle, std::wstring& error) {
    std::string body;
    PutStr(body, handle);
    std::string reply;
    return PipeSftpRpc(4, body, 101, reply, nullptr, error);
}

bool SftpConnection::PipeSftpInit(std::wstring& error) {
    std::string init;
    PutU32(init, 5);
    init.push_back(1);
    PutU32(init, 3);
    if (!PipeWriteAll(init.data(), init.size())) {
        error = L"SFTP: не удалось отправить INIT.";
        return false;
    }
    uint8_t type = 0;
    uint32_t id = 0;
    std::string payload;
    if (!PipeSftpRecv(type, id, payload, error)) return false;
    if (type != 2 || payload.size() < 4) {
        error = L"SFTP: нет VERSION. F6 — tsh login.";
        return false;
    }
    pipeSftp_ = true;
    std::string body;
    PutStr(body, ".");
    std::string reply;
    if (PipeSftpRpc(16, body, 104, reply, nullptr, error)) {
        size_t i = 0;
        uint32_t count = 0;
        std::string name;
        if (GetU32(reply, i, count) && count >= 1 && GetStr(reply, i, name) && !name.empty())
            home_ = name;
        else
            home_ = "/";
        error.clear();
    } else {
        home_ = "/";
        error.clear();
    }
    return true;
}

bool SftpConnection::Connect(const StoredServer& server, std::wstring& error) {
    Close();
    StartupNetwork();
    if (server.host.empty()) {
        error = L"Host is empty.";
        return false;
    }
    if (server.user.empty()) {
        error = L"User is empty.";
        return false;
    }
    const std::string host = Utf8(server.host);
    const std::string port = Utf8(server.port.empty() ? L"22" : server.port);
    const bool viaTsh = server.fromTeleport;
    const bool jump = !viaTsh && !server.proxy.empty();
    if (viaTsh) {
        if (!TshConnect(server, error)) return false;
        if (!PipeSftpInit(error)) {
            Close();
            return false;
        }
        return true;
    } else if (jump) {
        if (!JumpConnect(server, error)) return false;
    } else if (!TcpConnect(host, port, error)) {
        return false;
    }

    session_ = libssh2_session_init();
    if (!session_) {
        error = L"Could not create an SSH session.";
        Close();
        return false;
    }
    auto* session = static_cast<LIBSSH2_SESSION*>(session_);
    *libssh2_session_abstract(session) = this;
    libssh2_session_set_blocking(session, 1);
    libssh2_session_set_timeout(session, viaTsh ? 60000 : 20000);
    if (jump || viaTsh) {
        if (sock_ == INVALID_SOCKET)
            sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock_ == INVALID_SOCKET) {
            error = L"Нет сокета для туннеля tsh.";
            Close();
            return false;
        }
        libssh2_session_callback_set2(session, LIBSSH2_CALLBACK_RECV,
                                      reinterpret_cast<libssh2_cb_generic*>(&JumpRecv));
        libssh2_session_callback_set2(session, LIBSSH2_CALLBACK_SEND,
                                      reinterpret_cast<libssh2_cb_generic*>(&JumpSend));
    }
    if (libssh2_session_handshake(session, sock_) != 0) {
        error = LastError(L"SSH handshake failed");
        Close();
        return false;
    }
    if (!CheckHostKey(host, std::atoi(port.c_str()), error, viaTsh)) {
        Close();
        return false;
    }
    if (!Authenticate(server, error)) {
        Close();
        return false;
    }
    sftp_ = libssh2_sftp_init(session);
    if (!sftp_) {
        error = LastError(L"SFTP subsystem failed");
        Close();
        return false;
    }
    char home[1024];
    if (libssh2_sftp_realpath(static_cast<LIBSSH2_SFTP*>(sftp_), ".", home, sizeof(home)) > 0)
        home_ = home;
    else
        home_ = "/";
    return true;
}

bool SftpConnection::List(const std::string& path, std::vector<RemoteEntry>& out, std::wstring& error) {
    if (pipeSftp_) {
        std::string body;
        PutStr(body, path);
        std::string reply;
        if (!PipeSftpRpc(11, body, 102, reply, nullptr, error)) return false;
        size_t hi = 0;
        std::string handle;
        if (!GetStr(reply, hi, handle)) {
            error = L"SFTP: нет handle каталога.";
            return false;
        }
        while (true) {
            std::string req;
            PutStr(req, handle);
            uint32_t status = 0;
            std::string names;
            if (!PipeSftpRpc(12, req, 104, names, &status, error)) {
                PipeSftpCloseHandle(handle, error);
                return false;
            }
            if (names.empty()) break;
            size_t i = 0;
            uint32_t count = 0;
            if (!GetU32(names, i, count)) break;
            for (uint32_t n = 0; n < count; ++n) {
                std::string filename, longname;
                uint32_t flags = 0, perm = 0, mtime = 0;
                uint64_t size = 0;
                if (!GetStr(names, i, filename) || !GetStr(names, i, longname)) break;
                ParseAttrs(names, i, flags, size, perm, mtime);
                if (filename == "." || filename == "..") continue;
                RemoteEntry entry;
                entry.name = Wide(filename.c_str());
                const bool havePerms = (flags & 4u) != 0;
                entry.directory = havePerms && ((perm & 0040000) != 0);
                if (havePerms && ((perm & 0170000) == 0120000)) {
                    std::string full = (path == "/" ? "/" : path + "/") + filename;
                    std::string st;
                    PutStr(st, full);
                    std::string attrs;
                    if (PipeSftpRpc(17, st, 105, attrs, nullptr, error)) {
                        size_t ai = 0;
                        uint32_t af = 0, ap = 0, am = 0;
                        uint64_t asz = 0;
                        if (ParseAttrs(attrs, ai, af, asz, ap, am) && (af & 4u) && (ap & 0040000))
                            entry.directory = true;
                        error.clear();
                    } else error.clear();
                }
                if (flags & 1u) entry.size = size;
                if (flags & 8u) entry.mtime = mtime;
                out.push_back(std::move(entry));
            }
        }
        std::wstring ignore;
        PipeSftpCloseHandle(handle, ignore);
        return true;
    }
    auto* sftp = static_cast<LIBSSH2_SFTP*>(sftp_);
    LIBSSH2_SFTP_HANDLE* dir = libssh2_sftp_opendir(sftp, path.c_str());
    if (!dir) {
        error = LastError(L"Cannot open directory");
        return false;
    }
    char name[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs{};
    while (true) {
        const int got = libssh2_sftp_readdir(dir, name, sizeof(name), &attrs);
        if (got < 0) {
            libssh2_sftp_closedir(dir);
            error = LastError(L"Cannot read directory");
            return false;
        }
        if (got == 0) break;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        RemoteEntry entry;
        entry.name = Wide(name);
        const bool havePerms = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) != 0;
        entry.directory = havePerms && LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
        if (havePerms && LIBSSH2_SFTP_S_ISLNK(attrs.permissions)) {
            LIBSSH2_SFTP_ATTRIBUTES target{};
            const std::string full = (path == "/" ? "/" : path + "/") + name;
            if (libssh2_sftp_stat(sftp, full.c_str(), &target) == 0 &&
                (target.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) &&
                LIBSSH2_SFTP_S_ISDIR(target.permissions))
                entry.directory = true;
        }
        if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) entry.size = attrs.filesize;
        if (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) entry.mtime = attrs.mtime;
        out.push_back(std::move(entry));
    }
    libssh2_sftp_closedir(dir);
    return true;
}

bool SftpConnection::Download(const std::string& remote, const std::wstring& local, bool directory, std::wstring& error) {
    if (pipeSftp_ && directory) {
        CreateDirectoryW(local.c_str(), nullptr);
        std::vector<RemoteEntry> entries;
        if (!List(remote, entries, error)) return false;
        for (const auto& entry : entries) {
            const std::string childRemote = (remote == "/" ? "/" : remote + "/") + Utf8(entry.name);
            const std::wstring childLocal = local + L"\\" + entry.name;
            if (!Download(childRemote, childLocal, entry.directory, error)) return false;
        }
        return true;
    }
    if (pipeSftp_) {
        std::string open;
        PutStr(open, remote);
        PutU32(open, 1);
        PutU32(open, 0);
        std::string reply;
        if (!PipeSftpRpc(3, open, 102, reply, nullptr, error)) return false;
        size_t hi = 0;
        std::string handle;
        if (!GetStr(reply, hi, handle)) {
            error = L"SFTP: нет handle файла.";
            return false;
        }
        HANDLE localFile = CreateFileW(local.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (localFile == INVALID_HANDLE_VALUE) {
            std::wstring ignore;
            PipeSftpCloseHandle(handle, ignore);
            error = L"Cannot create local file.";
            return false;
        }
        bool ok = true;
        uint64_t offset = 0;
        while (ok) {
            std::string req;
            PutStr(req, handle);
            PutU64(req, offset);
            PutU32(req, 32768);
            std::string data;
            uint32_t status = 0;
            if (!PipeSftpRpc(5, req, 103, data, &status, error)) {
                ok = false;
                break;
            }
            if (data.empty()) break;
            size_t i = 0;
            std::string chunk;
            if (!GetStr(data, i, chunk) || chunk.empty()) break;
            DWORD wrote = 0;
            if (!WriteFile(localFile, chunk.data(), static_cast<DWORD>(chunk.size()), &wrote, nullptr)) {
                ok = false;
                error = L"Cannot write local file.";
                break;
            }
            offset += chunk.size();
        }
        CloseHandle(localFile);
        std::wstring ignore;
        PipeSftpCloseHandle(handle, ignore);
        return ok;
    }
    auto* sftp = static_cast<LIBSSH2_SFTP*>(sftp_);
    if (directory) {
        CreateDirectoryW(local.c_str(), nullptr);
        std::vector<RemoteEntry> entries;
        if (!List(remote, entries, error)) return false;
        for (const auto& entry : entries) {
            const std::string childRemote = (remote == "/" ? "/" : remote + "/") + Utf8(entry.name);
            const std::wstring childLocal = local + L"\\" + entry.name;
            if (!Download(childRemote, childLocal, entry.directory, error)) return false;
        }
        return true;
    }
    LIBSSH2_SFTP_HANDLE* remoteFile = libssh2_sftp_open(sftp, remote.c_str(), LIBSSH2_FXF_READ, 0);
    if (!remoteFile) {
        error = LastError(L"Cannot download");
        return false;
    }
    HANDLE localFile = CreateFileW(local.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (localFile == INVALID_HANDLE_VALUE) {
        libssh2_sftp_close(remoteFile);
        error = L"Cannot create local file.";
        return false;
    }
    char buffer[32768];
    bool ok = true;
    while (true) {
        const ssize_t got = libssh2_sftp_read(remoteFile, buffer, sizeof(buffer));
        if (got < 0) { ok = false; error = LastError(L"Download failed"); break; }
        if (got == 0) break;
        DWORD wrote = 0;
        if (!WriteFile(localFile, buffer, static_cast<DWORD>(got), &wrote, nullptr)) {
            ok = false;
            error = L"Cannot write local file.";
            break;
        }
    }
    CloseHandle(localFile);
    libssh2_sftp_close(remoteFile);
    return ok;
}

bool SftpConnection::Upload(const std::wstring& local, const std::string& remote, bool directory, std::wstring& error) {
    if (pipeSftp_) {
        const DWORD attr = GetFileAttributesW(local.c_str());
        const bool isDir = directory || (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
        if (isDir) {
            std::wstring ignore;
            Mkdir(remote, ignore);
            WIN32_FIND_DATAW found{};
            HANDLE search = FindFirstFileW((local + L"\\*").c_str(), &found);
            if (search == INVALID_HANDLE_VALUE) return true;
            do {
                if (wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0) continue;
                const bool childDir = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                const std::wstring childLocal = local + L"\\" + found.cFileName;
                const std::string childRemote = (remote == "/" ? "/" : remote + "/") + Utf8(found.cFileName);
                if (!Upload(childLocal, childRemote, childDir, error)) {
                    FindClose(search);
                    return false;
                }
            } while (FindNextFileW(search, &found));
            FindClose(search);
            return true;
        }
        HANDLE localFile = CreateFileW(local.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (localFile == INVALID_HANDLE_VALUE) {
            error = L"Cannot open local file.";
            return false;
        }
        std::string open;
        PutStr(open, remote);
        PutU32(open, 0x1a);
        PutU32(open, 0);
        std::string reply;
        if (!PipeSftpRpc(3, open, 102, reply, nullptr, error)) {
            CloseHandle(localFile);
            return false;
        }
        size_t hi = 0;
        std::string handle;
        if (!GetStr(reply, hi, handle)) {
            CloseHandle(localFile);
            error = L"SFTP: нет handle для записи.";
            return false;
        }
        bool ok = true;
        uint64_t offset = 0;
        char buffer[32768];
        while (ok) {
            DWORD read = 0;
            if (!ReadFile(localFile, buffer, sizeof(buffer), &read, nullptr)) {
                ok = false;
                error = L"Cannot read local file.";
                break;
            }
            if (read == 0) break;
            std::string req;
            PutStr(req, handle);
            PutU64(req, offset);
            PutStr(req, std::string(buffer, read));
            std::string st;
            if (!PipeSftpRpc(6, req, 101, st, nullptr, error)) {
                ok = false;
                break;
            }
            offset += read;
        }
        CloseHandle(localFile);
        std::wstring ignore;
        PipeSftpCloseHandle(handle, ignore);
        return ok;
    }
    auto* sftp = static_cast<LIBSSH2_SFTP*>(sftp_);
    const DWORD attr = GetFileAttributesW(local.c_str());
    const bool isDir = directory || (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
    if (isDir) {
        libssh2_sftp_mkdir(sftp, remote.c_str(), 0755);
        WIN32_FIND_DATAW found{};
        HANDLE search = FindFirstFileW((local + L"\\*").c_str(), &found);
        if (search == INVALID_HANDLE_VALUE) return true;
        do {
            if (wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0) continue;
            const bool childDir = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const std::wstring childLocal = local + L"\\" + found.cFileName;
            const std::string childRemote = (remote == "/" ? "/" : remote + "/") + Utf8(found.cFileName);
            if (!Upload(childLocal, childRemote, childDir, error)) {
                FindClose(search);
                return false;
            }
        } while (FindNextFileW(search, &found));
        FindClose(search);
        return true;
    }
    HANDLE localFile = CreateFileW(local.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (localFile == INVALID_HANDLE_VALUE) {
        error = L"Cannot open local file.";
        return false;
    }
    LIBSSH2_SFTP_HANDLE* remoteFile = libssh2_sftp_open(
        sftp, remote.c_str(), LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC, 0644);
    if (!remoteFile) {
        CloseHandle(localFile);
        error = LastError(L"Cannot upload");
        return false;
    }
    char buffer[32768];
    bool ok = true;
    while (true) {
        DWORD read = 0;
        if (!ReadFile(localFile, buffer, sizeof(buffer), &read, nullptr)) {
            ok = false;
            error = L"Cannot read local file.";
            break;
        }
        if (read == 0) break;
        size_t sent = 0;
        while (sent < read) {
            const ssize_t wrote = libssh2_sftp_write(remoteFile, buffer + sent, read - sent);
            if (wrote < 0) { ok = false; error = LastError(L"Upload failed"); break; }
            sent += static_cast<size_t>(wrote);
        }
        if (!ok) break;
    }
    CloseHandle(localFile);
    libssh2_sftp_close(remoteFile);
    return ok;
}

bool SftpConnection::Mkdir(const std::string& path, std::wstring& error) {
    if (pipeSftp_) {
        std::string body;
        PutStr(body, path);
        PutU32(body, 0);
        std::string reply;
        return PipeSftpRpc(14, body, 101, reply, nullptr, error);
    }
    if (libssh2_sftp_mkdir(static_cast<LIBSSH2_SFTP*>(sftp_), path.c_str(), 0755) == 0) return true;
    error = LastError(L"Cannot create directory");
    return false;
}

bool SftpConnection::Remove(const std::string& path, bool directory, std::wstring& error) {
    if (pipeSftp_) {
        if (directory) {
            std::vector<RemoteEntry> entries;
            if (!List(path, entries, error)) return false;
            for (const auto& entry : entries) {
                const std::string child = (path == "/" ? "/" : path + "/") + Utf8(entry.name);
                if (!Remove(child, entry.directory, error)) return false;
            }
            std::string body;
            PutStr(body, path);
            std::string reply;
            return PipeSftpRpc(15, body, 101, reply, nullptr, error);
        }
        std::string body;
        PutStr(body, path);
        std::string reply;
        return PipeSftpRpc(13, body, 101, reply, nullptr, error);
    }
    auto* sftp = static_cast<LIBSSH2_SFTP*>(sftp_);
    if (directory) {
        std::vector<RemoteEntry> entries;
        if (!List(path, entries, error)) return false;
        for (const auto& entry : entries) {
            const std::string child = (path == "/" ? "/" : path + "/") + Utf8(entry.name);
            if (!Remove(child, entry.directory, error)) return false;
        }
        if (libssh2_sftp_rmdir(sftp, path.c_str()) == 0) return true;
        error = LastError(L"Cannot remove directory");
        return false;
    }
    if (libssh2_sftp_unlink(sftp, path.c_str()) == 0) return true;
    error = LastError(L"Cannot delete file");
    return false;
}

bool SftpConnection::Rename(const std::string& from, const std::string& to, std::wstring& error) {
    if (pipeSftp_) {
        std::string body;
        PutStr(body, from);
        PutStr(body, to);
        std::string reply;
        return PipeSftpRpc(18, body, 101, reply, nullptr, error);
    }
    if (libssh2_sftp_rename(static_cast<LIBSSH2_SFTP*>(sftp_), from.c_str(), to.c_str()) == 0) return true;
    error = LastError(L"Cannot rename");
    return false;
}
