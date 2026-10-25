#include "imap_client.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <regex>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")

struct ImapClient::Impl {
    SOCKET sock = INVALID_SOCKET;
    SSL* ssl = nullptr;
    SSL_CTX* sslCtx = nullptr;
    std::string host;
    int port = 993;
    std::string login;
    std::string password;
    bool useSSL = true;

    bool sendCommand(const std::string& cmd) {
        if (ssl) {
            return SSL_write(ssl, cmd.c_str(), (int)cmd.size()) > 0;
        } else {
            return send(sock, cmd.c_str(), (int)cmd.size(), 0) != SOCKET_ERROR;
        }
    }

    bool readLine(std::string& line) {
        line.clear();
        char buf[1];
        while (true) {
            int n = 0;
            if (ssl) {
                n = SSL_read(ssl, buf, 1);
            } else {
                n = recv(sock, buf, 1, 0);
            }
            if (n <= 0) return false;
            if (buf[0] == '\n') {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                return true;
            }
            line += buf[0];
        }
    }

    void close() {
        if (ssl) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            ssl = nullptr;
        }
        if (sslCtx) {
            SSL_CTX_free(sslCtx);
            sslCtx = nullptr;
        }
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
    }
};

ImapClient::ImapClient() : m_impl(new Impl()) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

ImapClient::~ImapClient() {
    disconnect();
    WSACleanup();
    delete m_impl;
}

bool ImapClient::connect(const ImapSettings& settings) {
    disconnect();

    int needed = WideCharToMultiByte(CP_UTF8, 0, settings.host.c_str(), (int)settings.host.size(), nullptr, 0, nullptr, nullptr);
    m_impl->host.resize(needed);
    WideCharToMultiByte(CP_UTF8, 0, settings.host.c_str(), (int)settings.host.size(), &m_impl->host[0], needed, nullptr, nullptr);

    needed = WideCharToMultiByte(CP_UTF8, 0, settings.login.c_str(), (int)settings.login.size(), nullptr, 0, nullptr, nullptr);
    m_impl->login.resize(needed);
    WideCharToMultiByte(CP_UTF8, 0, settings.login.c_str(), (int)settings.login.size(), &m_impl->login[0], needed, nullptr, nullptr);

    needed = WideCharToMultiByte(CP_UTF8, 0, settings.password.c_str(), (int)settings.password.size(), nullptr, 0, nullptr, nullptr);
    m_impl->password.resize(needed);
    WideCharToMultiByte(CP_UTF8, 0, settings.password.c_str(), (int)settings.password.size(), &m_impl->password[0], needed, nullptr, nullptr);

    m_impl->port = settings.port;
    m_impl->useSSL = settings.useSSL;

    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(m_impl->port);
    if (getaddrinfo(m_impl->host.c_str(), portStr.c_str(), &hints, &result) != 0) {
        m_lastError = "DNS resolution failed";
        return false;
    }

    m_impl->sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (m_impl->sock == INVALID_SOCKET) {
        freeaddrinfo(result);
        m_lastError = "Socket creation failed";
        return false;
    }

    DWORD timeout = 10000;
    setsockopt(m_impl->sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(m_impl->sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

    if (::connect(m_impl->sock, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        m_impl->close();
        freeaddrinfo(result);
        m_lastError = "Connection failed";
        return false;
    }

    freeaddrinfo(result);

    if (m_impl->useSSL) {
        SSL_library_init();
        SSL_load_error_strings();
        m_impl->sslCtx = SSL_CTX_new(TLS_client_method());
        if (!m_impl->sslCtx) {
            m_impl->close();
            m_lastError = "SSL context creation failed";
            return false;
        }

        SSL_CTX_set_verify(m_impl->sslCtx, SSL_VERIFY_NONE, nullptr);

        m_impl->ssl = SSL_new(m_impl->sslCtx);
        SSL_set_fd(m_impl->ssl, m_impl->sock);

        if (SSL_connect(m_impl->ssl) <= 0) {
            m_impl->close();
            m_lastError = "SSL handshake failed";
            return false;
        }
    }

    std::string greeting;
    if (!m_impl->readLine(greeting)) {
        disconnect();
        m_lastError = "No greeting from server";
        return false;
    }

    std::string cmd = "A001 LOGIN " + m_impl->login + " " + m_impl->password + "\r\n";
    if (!m_impl->sendCommand(cmd)) {
        disconnect();
        m_lastError = "LOGIN failed";
        return false;
    }

    std::string response;
    if (!m_impl->readLine(response) || response.find("OK") == std::string::npos) {
        disconnect();
        m_lastError = "Authentication failed";
        return false;
    }

    return true;
}

void ImapClient::disconnect() {
    if (m_impl) m_impl->close();
}

bool ImapClient::isConnected() const {
    return m_impl && m_impl->sock != INVALID_SOCKET;
}

static bool ParseListName(const std::string& line, std::string& name) {
    auto pos = line.find(" LIST ");
    if (pos == std::string::npos) pos = line.find(" LSUB ");
    if (pos == std::string::npos) return false;
    pos = line.find('(', pos);
    if (pos == std::string::npos) return false;
    int depth = 0;
    for (; pos < line.size(); ++pos) {
        if (line[pos] == '(') ++depth;
        else if (line[pos] == ')') {
            --depth;
            if (depth == 0) {
                ++pos;
                break;
            }
        }
    }
    while (pos < line.size() && line[pos] == ' ') ++pos;
    if (line.compare(pos, 3, "NIL") == 0) {
        pos += 3;
    } else if (pos < line.size() && line[pos] == '"') {
        ++pos;
        while (pos < line.size() && line[pos] != '"')
            pos += (line[pos] == '\\' && pos + 1 < line.size()) ? 2 : 1;
        if (pos < line.size()) ++pos;
    } else {
        while (pos < line.size() && line[pos] != ' ') ++pos;
    }
    while (pos < line.size() && line[pos] == ' ') ++pos;
    if (pos >= line.size()) return false;

    name.clear();
    if (line[pos] == '"') {
        for (++pos; pos < line.size() && line[pos] != '"';) {
            if (line[pos] == '\\' && pos + 1 < line.size()) {
                name.push_back(line[pos + 1]);
                pos += 2;
            } else {
                name.push_back(line[pos++]);
            }
        }
    } else {
        name = line.substr(pos);
        while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
    }
    return !name.empty();
}

std::vector<ImapFolder> ImapClient::listFolders() {
    std::vector<ImapFolder> folders;
    if (!isConnected()) return folders;

    m_impl->sendCommand("A002 LIST \"\" \"*\"\r\n");

    std::string line;
    while (m_impl->readLine(line)) {
        if (line.find("A002 OK") != std::string::npos) break;
        ImapFolder folder;
        if (ParseListName(line, folder.name)) folders.push_back(folder);
    }

    return folders;
}

std::vector<ImapMessage> ImapClient::listMessages(const std::string& folder) {
    std::vector<ImapMessage> messages;
    if (!isConnected()) return messages;

    m_impl->sendCommand("A003 SELECT \"" + folder + "\"\r\n");
    std::string line;
    while (m_impl->readLine(line)) {
        if (line.find("A003 OK") != std::string::npos) break;
    }

    m_impl->sendCommand("A004 FETCH 1:* (UID FLAGS INTERNALDATE RFC822.SIZE BODY.PEEK[HEADER.FIELDS (FROM SUBJECT)])\r\n");

    ImapMessage currentMsg;
    while (m_impl->readLine(line)) {
        if (line.find("A004 OK") != std::string::npos) break;

        std::regex fetchRegex("\\* (\\d+) FETCH");
        std::regex uidRegex("UID (\\d+)");
        std::regex fromRegex("FROM: (.+)", std::regex::icase);
        std::regex subjectRegex("SUBJECT: (.+)", std::regex::icase);
        std::regex dateRegex("INTERNALDATE \"(.+)\"", std::regex::icase);
        std::regex sizeRegex("RFC822.SIZE (\\d+)");

        std::smatch match;
        if (std::regex_search(line, match, fetchRegex)) {
            if (currentMsg.uid != 0) {
                messages.push_back(currentMsg);
            }
            currentMsg = ImapMessage();
            currentMsg.uid = std::stoi(match[1].str());
        } else if (std::regex_search(line, match, uidRegex)) {
            currentMsg.uid = std::stoi(match[1].str());
        } else if (std::regex_search(line, match, fromRegex)) {
            currentMsg.from = match[1].str();
        } else if (std::regex_search(line, match, subjectRegex)) {
            currentMsg.subject = match[1].str();
        } else if (std::regex_search(line, match, dateRegex)) {
            currentMsg.date = match[1].str();
        } else if (std::regex_search(line, match, sizeRegex)) {
            currentMsg.size = std::stoi(match[1].str());
        }
    }

    if (currentMsg.uid != 0) {
        messages.push_back(currentMsg);
    }

    std::reverse(messages.begin(), messages.end());
    return messages;
}

static std::string LowerCopy(std::string text) {
    for (char& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

static int HexValue(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static std::string Base64Decode(const std::string& text) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int value = 0;
    int bits = 0;
    for (unsigned char ch : text) {
        if (ch == '=' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t') continue;
        const char* found = std::strchr(alphabet, ch);
        if (!found) continue;
        value = (value << 6) | static_cast<int>(found - alphabet);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((value >> bits) & 0xFF));
        }
    }
    return out;
}

static std::string DecodeQuotedPrintable(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '=' && i + 1 < text.size() && (text[i + 1] == '\n' || text[i + 1] == '\r')) {
            if (text[i + 1] == '\r' && i + 2 < text.size() && text[i + 2] == '\n') i += 2;
            else ++i;
            continue;
        }
        if (text[i] == '=' && i + 2 < text.size() && HexValue(text[i + 1]) >= 0 && HexValue(text[i + 2]) >= 0) {
            out.push_back(static_cast<char>((HexValue(text[i + 1]) << 4) | HexValue(text[i + 2])));
            i += 2;
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

static std::string HeaderField(const std::string& headers, const char* name) {
    const std::string want = LowerCopy(name) + ":";
    std::string value;
    bool capture = false;
    size_t i = 0;
    while (i < headers.size()) {
        size_t end = headers.find('\n', i);
        if (end == std::string::npos) end = headers.size();
        std::string line = headers.substr(i, end - i);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
            if (capture) value += line;
        } else {
            capture = false;
            const std::string low = LowerCopy(line);
            if (low.rfind(want, 0) == 0) {
                value = line.substr(want.size());
                while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(value.begin());
                capture = true;
            }
        }
        i = end + 1;
    }
    return value;
}

static std::string HeaderParam(const std::string& header, const char* key) {
    const std::string needle = LowerCopy(key) + "=";
    const auto pos = LowerCopy(header).find(needle);
    if (pos == std::string::npos) return {};
    size_t start = pos + needle.size();
    if (start < header.size() && header[start] == '"') {
        const auto end = header.find('"', start + 1);
        return end == std::string::npos ? header.substr(start + 1) : header.substr(start + 1, end - start - 1);
    }
    const auto end = header.find_first_of(" ;\t\r\n", start);
    return header.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

static std::string ToUtf8(const std::string& bytes, const std::string& charset) {
    if (bytes.empty()) return {};
    const std::string name = LowerCopy(charset);
    UINT codePage = CP_UTF8;
    if (name.find("1251") != std::string::npos) codePage = 1251;
    else if (name.find("koi8") != std::string::npos) codePage = 20866;
    else if (name.find("866") != std::string::npos) codePage = 866;
    else if (name.find("8859-1") != std::string::npos) codePage = 28591;
    const int chars = MultiByteToWideChar(codePage, 0, bytes.c_str(), static_cast<int>(bytes.size()), nullptr, 0);
    if (chars <= 0) return bytes;
    std::wstring wide(chars, L'\0');
    MultiByteToWideChar(codePage, 0, bytes.c_str(), static_cast<int>(bytes.size()), wide.data(), chars);
    const int outChars = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), chars, nullptr, 0, nullptr, nullptr);
    if (outChars <= 0) return bytes;
    std::string out(outChars, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), chars, out.data(), outChars, nullptr, nullptr);
    return out;
}

static std::string DecodeBody(const std::string& bytes, const std::string& encoding, const std::string& charset) {
    const std::string kind = LowerCopy(encoding);
    std::string decoded = bytes;
    if (kind.find("base64") != std::string::npos) decoded = Base64Decode(bytes);
    else if (kind.find("quoted-printable") != std::string::npos) decoded = DecodeQuotedPrintable(bytes);
    return ToUtf8(decoded, charset);
}

static void AppendPart(ImapMessageBody& body, const std::string& block);

static void AppendMultipart(ImapMessageBody& body, const std::string& content, const std::string& boundary) {
    const std::string marker = "--" + boundary;
    size_t pos = 0;
    while (pos < content.size()) {
        const auto at = content.find(marker, pos);
        if (at == std::string::npos) break;
        size_t start = at + marker.size();
        if (content.compare(start, 2, "--") == 0) break;
        if (start < content.size() && content[start] == '\r') ++start;
        if (start < content.size() && content[start] == '\n') ++start;
        const auto next = content.find(marker, start);
        AppendPart(body, content.substr(start, next == std::string::npos ? std::string::npos : next - start));
        if (next == std::string::npos) break;
        pos = next;
    }
}

static void AppendPart(ImapMessageBody& body, const std::string& block) {
    size_t split = block.find("\r\n\r\n");
    size_t skip = 4;
    if (split == std::string::npos) {
        split = block.find("\n\n");
        skip = 2;
    }
    if (split == std::string::npos) return;
    const std::string headers = block.substr(0, split);
    const std::string content = block.substr(split + skip);
    const std::string type = HeaderField(headers, "Content-Type");
    const std::string boundary = HeaderParam(type, "boundary");
    if (!boundary.empty()) {
        AppendMultipart(body, content, boundary);
        return;
    }
    const std::string low = LowerCopy(type);
    if (!low.empty() && low.find("text/") == std::string::npos) return;
    const std::string decoded = DecodeBody(content, HeaderField(headers, "Content-Transfer-Encoding"), HeaderParam(type, "charset"));
    if (low.find("text/html") != std::string::npos) body.htmlBody += decoded;
    else body.textBody += decoded;
}

static std::string ExtractMail(std::string text) {
    const auto nl = text.find('\n');
    if (nl != std::string::npos && text.find("FETCH") < nl) text.erase(0, nl + 1);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    if (!text.empty() && text.back() == ')') text.pop_back();
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    return text;
}

static void FillMail(ImapMessageBody& body, const std::string& mail) {
    size_t split = mail.find("\r\n\r\n");
    size_t skip = 4;
    if (split == std::string::npos) {
        split = mail.find("\n\n");
        skip = 2;
    }
    if (split == std::string::npos) {
        body.textBody = mail;
        return;
    }
    const std::string headers = mail.substr(0, split);
    body.from = HeaderField(headers, "From");
    body.to = HeaderField(headers, "To");
    body.subject = HeaderField(headers, "Subject");
    body.date = HeaderField(headers, "Date");
    AppendPart(body, mail);
}

ImapMessageBody ImapClient::fetchMessage(const std::string& folder, int uid) {
    ImapMessageBody body;
    if (!isConnected()) return body;

    m_impl->sendCommand("A005 SELECT \"" + folder + "\"\r\n");
    std::string line;
    while (m_impl->readLine(line)) {
        if (line.find("A005 OK") != std::string::npos) break;
    }

    m_impl->sendCommand("A006 UID FETCH " + std::to_string(uid) + " (UID BODY.PEEK[])\r\n");

    std::string fullMessage;
    while (m_impl->readLine(line)) {
        if (line.find("A006 OK") != std::string::npos) break;
        fullMessage += line + "\n";
    }

    FillMail(body, ExtractMail(fullMessage));
    return body;
}
