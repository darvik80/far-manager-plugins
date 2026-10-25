#include "sftp_client.hpp"
#include "ssh_config.hpp"
#include "ui.hpp"

#include <libssh2.h>
#include <libssh2_sftp.h>

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

}  // namespace

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

void SftpConnection::Close() {
    if (sftp_) {
        libssh2_sftp_shutdown(static_cast<LIBSSH2_SFTP*>(sftp_));
        sftp_ = nullptr;
    }
    if (session_) {
        libssh2_session_disconnect(static_cast<LIBSSH2_SESSION*>(session_), "bye");
        libssh2_session_free(static_cast<LIBSSH2_SESSION*>(session_));
        session_ = nullptr;
    }
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }
    if (jumpIn_) { CloseHandle(jumpIn_); jumpIn_ = nullptr; }
    if (jumpOut_) { CloseHandle(jumpOut_); jumpOut_ = nullptr; }
    if (jumpErr_) { CloseHandle(jumpErr_); jumpErr_ = nullptr; }
    if (jumpProcess_) {
        WaitForSingleObject(jumpProcess_, 200);
        TerminateProcess(jumpProcess_, 0);
        CloseHandle(jumpProcess_);
        jumpProcess_ = nullptr;
    }
    home_.clear();
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

bool SftpConnection::JumpConnect(const StoredServer& server, std::wstring& error) {
    const std::wstring ssh = FindSshExe();
    if (ssh.empty()) {
        error = L"OpenSSH is required for ProxyJump.";
        return false;
    }
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE outRead = nullptr, outWrite = nullptr, inRead = nullptr, inWrite = nullptr, errRead = nullptr, errWrite = nullptr;
    CreatePipe(&outRead, &outWrite, &security, 0);
    CreatePipe(&inRead, &inWrite, &security, 0);
    CreatePipe(&errRead, &errWrite, &security, 0);
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

    std::wstring command = L"\"" + ssh + L"\"";
    if (FileExists(SshConfigPath())) command += L" -F " + QuoteArg(SshConfigPath());
    command += L" -o BatchMode=yes -W " + server.host + L":" + (server.port.empty() ? L"22" : server.port);
    command += L" " + QuoteArg(server.proxy);

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
        error = L"Could not start ssh.exe for ProxyJump.";
        return false;
    }
    CloseHandle(process.hThread);
    jumpProcess_ = process.hProcess;
    jumpIn_ = inWrite;
    jumpOut_ = outRead;
    jumpErr_ = errRead;
    return true;
}

bool SftpConnection::CheckHostKey(const std::string& host, int port, std::wstring& error) {
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
    libssh2_knownhost_readfile(hosts, userFile.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    libssh2_knownhost_readfile(hosts, ourFile.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    const int check = libssh2_knownhost_checkp(hosts, host.c_str(), port, key, keyLen, mask, nullptr);
    bool ok = check == LIBSSH2_KNOWNHOST_CHECK_MATCH;
    if (!ok) {
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

bool SftpConnection::Authenticate(const StoredServer& server, std::wstring& error) {
    auto* session = static_cast<LIBSSH2_SESSION*>(session_);
    const std::string user = Utf8(server.user);
    std::vector<std::wstring> keys = server.identityFiles;
    if (!server.keyFile.empty()) keys.insert(keys.begin(), server.keyFile);
    if (keys.empty()) keys = DefaultIdentityFiles();

    for (const auto& key : keys) {
        if (!FileExists(key)) continue;
        const std::string path = Utf8(key);
        int rc = libssh2_userauth_publickey_fromfile(session, user.c_str(), nullptr, path.c_str(), "");
        if (rc == 0) return true;
        if (rc == LIBSSH2_ERROR_FILE) {
            const std::wstring pass = AskText(L"Key passphrase", key.c_str(), true);
            if (pass.empty()) continue;
            rc = libssh2_userauth_publickey_fromfile(session, user.c_str(), nullptr, path.c_str(), Utf8(pass).c_str());
            if (rc == 0) return true;
        }
    }

    std::string password = Utf8(server.password);
    if (password.empty()) {
        const std::wstring typed = AskText(L"Password", server.host.c_str(), true);
        password = Utf8(typed);
    }
    if (!password.empty() && libssh2_userauth_password(session, user.c_str(), password.c_str()) == 0)
        return true;
    error = LastError(L"Authentication failed");
    return false;
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
    const bool jump = !server.proxy.empty();
    if (jump) {
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
    libssh2_session_set_timeout(session, 20000);
    if (jump) {
        libssh2_session_callback_set(session, LIBSSH2_CALLBACK_RECV, reinterpret_cast<void*>(&JumpRecv));
        libssh2_session_callback_set(session, LIBSSH2_CALLBACK_SEND, reinterpret_cast<void*>(&JumpSend));
    }
    if (libssh2_session_handshake(session, jump ? INVALID_SOCKET : sock_) != 0) {
        error = LastError(L"SSH handshake failed");
        Close();
        return false;
    }
    if (!CheckHostKey(host, std::atoi(port.c_str()), error)) {
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
    if (libssh2_sftp_mkdir(static_cast<LIBSSH2_SFTP*>(sftp_), path.c_str(), 0755) == 0) return true;
    error = LastError(L"Cannot create directory");
    return false;
}

bool SftpConnection::Remove(const std::string& path, bool directory, std::wstring& error) {
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
    if (libssh2_sftp_rename(static_cast<LIBSSH2_SFTP*>(sftp_), from.c_str(), to.c_str()) == 0) return true;
    error = LastError(L"Cannot rename");
    return false;
}
