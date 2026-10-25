#ifndef SSH_CLIENT_SFTP_CLIENT_HPP_
#define SSH_CLIENT_SFTP_CLIENT_HPP_

#include "session_store.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <string>
#include <vector>

struct RemoteEntry {
    std::wstring name;
    bool directory = false;
    unsigned long long size = 0;
    unsigned long mtime = 0;
};

class SftpConnection {
public:
    SftpConnection() = default;
    ~SftpConnection();
    SftpConnection(const SftpConnection&) = delete;
    SftpConnection& operator=(const SftpConnection&) = delete;

    bool Connect(const StoredServer& server, std::wstring& error);
    void Close();
    bool List(const std::string& path, std::vector<RemoteEntry>& out, std::wstring& error);
    bool Download(const std::string& remote, const std::wstring& local, bool directory, std::wstring& error);
    bool Upload(const std::wstring& local, const std::string& remote, bool directory, std::wstring& error);
    bool Mkdir(const std::string& path, std::wstring& error);
    bool Remove(const std::string& path, bool directory, std::wstring& error);
    bool Rename(const std::string& from, const std::string& to, std::wstring& error);
    const std::string& Home() const { return home_; }
    bool Active() const { return sftp_ != nullptr; }

    HANDLE JumpIn() const { return jumpIn_; }
    HANDLE JumpOut() const { return jumpOut_; }

private:
    bool TcpConnect(const std::string& host, const std::string& port, std::wstring& error);
    bool JumpConnect(const StoredServer& server, std::wstring& error);
    bool CheckHostKey(const std::string& host, int port, std::wstring& error);
    bool Authenticate(const StoredServer& server, std::wstring& error);
    std::wstring LastError(const wchar_t* prefix) const;

    SOCKET sock_ = INVALID_SOCKET;
    HANDLE jumpProcess_ = nullptr;
    HANDLE jumpIn_ = nullptr;
    HANDLE jumpOut_ = nullptr;
    HANDLE jumpErr_ = nullptr;
    void* session_ = nullptr;
    void* sftp_ = nullptr;
    std::string home_ = "/";
};

void StartupNetwork();
void ShutdownNetwork();

#endif
