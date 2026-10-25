#ifndef IMAP_CLIENT_HPP_
#define IMAP_CLIENT_HPP_

#include <string>
#include <vector>

struct ImapSettings {
    std::wstring host;
    int port = 993;
    std::wstring login;
    std::wstring password;
    bool useSSL = true;
};

struct ImapAccount {
    std::wstring name;
    ImapSettings settings;
};

struct ImapFolder {
    std::string name;
    int messageCount = 0;
};

struct ImapMessage {
    int uid = 0;
    std::string from;
    std::string subject;
    std::string date;
    int size = 0;
    bool hasHtml = false;
    bool hasText = false;
};

struct ImapMessageBody {
    std::string from;
    std::string to;
    std::string subject;
    std::string date;
    std::string textBody;
    std::string htmlBody;
};

class ImapClient {
public:
    ImapClient();
    ~ImapClient();

    bool connect(const ImapSettings& settings);
    void disconnect();
    bool isConnected() const;

    std::vector<ImapFolder> listFolders();
    std::vector<ImapMessage> listMessages(const std::string& folder);
    ImapMessageBody fetchMessage(const std::string& folder, int uid);

    std::string getLastError() const { return m_lastError; }

private:
    struct Impl;
    Impl* m_impl = nullptr;
    std::string m_lastError;
};

#endif
