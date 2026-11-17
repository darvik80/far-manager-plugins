#include "sftp_client.hpp"

#include <libssh2.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace {

std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(needed > 0 ? needed : 0, '\0');
    if (needed > 0)
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(needed > 0 ? needed : 0, L'\0');
    if (needed > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed);
    return out;
}

std::string ShQuote(const std::string& text) {
    std::string out = "'";
    for (char ch : text) {
        if (ch == '\'') out += "'\\''";
        else out += ch;
    }
    out += '\'';
    return out;
}

std::string TrimCopy(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' || text[begin] == '\n'))
        ++begin;
    size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n'))
        --end;
    return text.substr(begin, end - begin);
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    for (;;) {
        const size_t nl = text.find('\n', start);
        const std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!line.empty()) lines.push_back(TrimCopy(line));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        const size_t tab = line.find('\t', start);
        parts.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    return parts;
}

unsigned long long ParseNumber(const std::string& text) {
    unsigned long long value = 0;
    for (char ch : text) {
        if (ch < '0' || ch > '9') break;
        value = value * 10 + static_cast<unsigned>(ch - '0');
    }
    return value;
}

std::string ToLowerAscii(const std::string& text) {
    std::string out = text;
    for (char& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

bool ContainsCi(const std::string& text, const char* needle) {
    const std::string hay = ToLowerAscii(text);
    const std::string what = ToLowerAscii(needle);
    return !what.empty() && hay.find(what) != std::string::npos;
}

// GNU findutils gives sizes and timestamps in one pass; the loop is for busybox and BSD
// finds without -printf. find is buffered so a directory it only half-reads is not listed twice
const char* kListScript =
    "cd -- %1 && pwd -P && { o=$(find -L . -maxdepth 1 -mindepth 1 -printf '%y\\t%s\\t%T@\\t%f\\n' 2>/dev/null);"
    " if [ -n \"$o\" ]; then printf '%s\\n' \"$o\";"
    " else for f in .* *; do case \"$f\" in .|..) continue ;; esac;"
    " [ -e \"$f\" ] || [ -L \"$f\" ] || continue;"
    " if [ -d \"$f\" ]; then printf 'd\\t0\\t0\\t%s\\n' \"$f\";"
    " else printf 'f\\t%s\\t%s\\t%s\\n'"
    " \"$(stat -c %s \"$f\" 2>/dev/null || stat -f %z \"$f\" 2>/dev/null || echo 0)\""
    " \"$(stat -c %Y \"$f\" 2>/dev/null || stat -f %m \"$f\" 2>/dev/null || date -r \"$f\" +%s 2>/dev/null || echo 0)\""
    " \"$f\"; fi;"
    " done; fi; }";

std::string ListCommand(const std::string& path) {
    std::string cmd = kListScript;
    const size_t mark = cmd.find("%1");
    cmd.replace(mark, 2, ShQuote(path));
    return cmd;
}

}  // namespace

void* SftpConnection::ShellOpen(const std::string& command, std::wstring& error) {
    if (!session_) {
        error = L"Нет соединения с сервером.";
        return nullptr;
    }
    auto* session = static_cast<LIBSSH2_SESSION*>(session_);
    auto* channel = libssh2_channel_open_session(session);
    if (!channel) {
        error = LastError(L"Не удалось открыть канал");
        return nullptr;
    }
    if (libssh2_channel_exec(channel, command.c_str()) != 0) {
        libssh2_channel_free(channel);
        error = LastError(L"Сервер не выполнил команду");
        return nullptr;
    }
    return channel;
}

void SftpConnection::ShellDrain(void* handle, std::string& out, std::string& errText) {
    auto* channel = static_cast<LIBSSH2_CHANNEL*>(handle);
    char buf[16384];
    for (;;) {
        const ssize_t n = libssh2_channel_read(channel, buf, sizeof(buf));
        if (n > 0) out.append(buf, static_cast<size_t>(n));
        const ssize_t e = libssh2_channel_read_stderr(channel, buf, sizeof(buf));
        if (e > 0) errText.append(buf, static_cast<size_t>(e));
        if (n <= 0 && e <= 0) break;
    }
}

int SftpConnection::ShellFinish(void* handle, const std::string& errText, std::wstring& error) {
    auto* channel = static_cast<LIBSSH2_CHANNEL*>(handle);
    const int status = libssh2_channel_get_exit_status(channel);
    libssh2_channel_close(channel);
    libssh2_channel_free(channel);
    if (status != 0) {
        const std::string message = TrimCopy(errText);
        error = message.empty() ? L"Команда завершилась с кодом " + std::to_wstring(status) : Wide(message);
    }
    return status;
}

bool SftpConnection::ShellQuery(const std::string& command, std::string& out, std::wstring& error) {
    void* handle = ShellOpen(command, error);
    if (!handle) return false;
    std::string errText;
    ShellDrain(handle, out, errText);
    return ShellFinish(handle, errText, error) == 0;
}

bool SftpConnection::ShellRun(const std::string& command, std::wstring& error) {
    std::string out;
    return ShellQuery(command, out, error);
}

bool SftpConnection::ShellMkdir(const std::string& path, std::wstring& error) {
    return ShellRun("mkdir -p -- " + ShQuote(path), error);
}

bool SftpConnection::ShellRename(const std::string& from, const std::string& to, std::wstring& error) {
    return ShellRun("mv -f -- " + ShQuote(from) + " " + ShQuote(to), error);
}

bool SftpConnection::ShellRemove(const std::string& path, bool directory, std::wstring& error) {
    if (!directory) return ShellRun("rm -f -- " + ShQuote(path), error);
    // rmdir alone is the safe first attempt: on a link to a directory it refuses instead of
    // deleting the files behind the link, which a recursive rm would not
    std::string out;
    if (ShellQuery("rmdir -- " + ShQuote(path), out, error)) return true;
    const std::wstring reason = error;
    if (!ContainsCi(Utf8(reason), "not empty")) {
        error = reason;
        return false;
    }
    std::vector<RemoteEntry> entries;
    if (!List(path, entries, error)) return false;
    for (const auto& entry : entries) {
        if (!Remove((path == "/" ? "" : path) + "/" + Utf8(entry.name), entry.directory, error)) return false;
    }
    return ShellRun("rmdir -- " + ShQuote(path), error);
}

bool SftpConnection::StartShellMode(std::wstring& error) {
    std::string out;
    if (!ShellQuery("pwd -P", out, error)) return false;
    const std::vector<std::string> lines = SplitLines(out);
    home_ = lines.empty() ? std::string("/") : lines.front();
    if (home_.empty() || home_[0] != '/') home_ = "/";
    shellMode_ = true;
    return true;
}

bool SftpConnection::ShellList(const std::string& path, std::vector<RemoteEntry>& out, std::wstring& error) {
    std::string body;
    if (!ShellQuery(ListCommand(path), body, error)) return false;
    const std::vector<std::string> lines = SplitLines(body);
    for (size_t i = 1; i < lines.size(); ++i) {
        const std::vector<std::string> fields = SplitTabs(lines[i]);
        if (fields.size() < 4) continue;
        RemoteEntry entry;
        entry.directory = fields[0] == "d";
        entry.size = ParseNumber(fields[1]);
        entry.mtime = static_cast<unsigned long>(ParseNumber(fields[2]));
        entry.name = Wide(fields[3]);
        if (entry.name.empty() || entry.name == L"." || entry.name == L"..") continue;
        out.push_back(std::move(entry));
    }
    return true;
}

bool SftpConnection::ShellDownload(const std::string& remote, const std::wstring& local, CopyMeter* meter,
                                   std::wstring& error) {
    if (meter) CopyMeterFile(*meter, local.c_str());
    void* handle = ShellOpen("cat -- " + ShQuote(remote), error);
    if (!handle) return false;
    auto* channel = static_cast<LIBSSH2_CHANNEL*>(handle);
    HANDLE file = CreateFileW(local.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        ShellFinish(handle, {}, error);
        error = L"Не удалось создать файл: " + local;
        return false;
    }
    char buf[32768];
    bool ok = true;
    for (;;) {
        if (CopyMeterAborted(meter, error)) {
            ok = false;
            break;
        }
        const ssize_t n = libssh2_channel_read(channel, buf, sizeof(buf));
        if (n > 0) {
            DWORD wrote = 0;
            if (!WriteFile(file, buf, static_cast<DWORD>(n), &wrote, nullptr) || wrote != static_cast<DWORD>(n)) {
                error = L"Не удалось записать локальный файл.";
                ok = false;
                break;
            }
            if (meter) CopyMeterAdd(*meter, static_cast<unsigned long long>(n));
            continue;
        }
        break;
    }
    std::string errText;
    if (ok) {
        std::string ignored;
        ShellDrain(handle, ignored, errText);
    }
    const int status = ShellFinish(handle, errText, error);
    CloseHandle(file);
    if (!ok) {
        if (meter && meter->cancelled) DeleteFileW(local.c_str());
        return false;
    }
    if (status != 0) {
        DeleteFileW(local.c_str());
        return false;
    }
    return true;
}

bool SftpConnection::ShellUpload(const std::wstring& local, const std::string& remote, CopyMeter* meter,
                                 std::wstring& error) {
    if (meter) CopyMeterFile(*meter, local.c_str());
    void* handle = ShellOpen("cat > " + ShQuote(remote), error);
    if (!handle) return false;
    auto* channel = static_cast<LIBSSH2_CHANNEL*>(handle);
    HANDLE file = CreateFileW(local.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        ShellFinish(handle, {}, error);
        error = L"Не удалось открыть локальный файл: " + local;
        return false;
    }
    char buf[32768];
    bool ok = true;
    for (;;) {
        if (CopyMeterAborted(meter, error)) {
            ok = false;
            break;
        }
        DWORD read = 0;
        if (!ReadFile(file, buf, sizeof(buf), &read, nullptr) || read == 0) break;
        size_t sent = 0;
        while (sent < read) {
            const ssize_t n = libssh2_channel_write(channel, buf + sent, read - sent);
            if (n <= 0) {
                error = LastError(L"Обрыв передачи");
                ok = false;
                break;
            }
            sent += static_cast<size_t>(n);
        }
        if (!ok) break;
        if (meter) CopyMeterAdd(*meter, static_cast<unsigned long long>(read));
    }
    CloseHandle(file);
    std::string errText;
    if (ok) {
        libssh2_channel_send_eof(channel);
        libssh2_channel_wait_eof(channel);
        std::string ignored;
        ShellDrain(handle, ignored, errText);
    }
    ShellFinish(handle, errText, error);
    return ok;
}

bool SftpConnection::ShellDownloadDir(const std::string& remote, const std::wstring& local, CopyMeter* meter,
                                      std::wstring& error) {
    CreateDirectoryW(local.c_str(), nullptr);
    std::vector<RemoteEntry> entries;
    if (!List(remote, entries, error)) return false;
    for (const auto& entry : entries) {
        const std::string childRemote = (remote == "/" ? "" : remote) + "/" + Utf8(entry.name);
        const std::wstring childLocal = local + L"\\" + entry.name;
        if (!Download(childRemote, childLocal, entry.directory, error, meter)) return false;
    }
    return true;
}

bool SftpConnection::ShellUploadDir(const std::wstring& local, const std::string& remote, CopyMeter* meter,
                                    std::wstring& error) {
    if (!ShellMkdir(remote, error)) return false;
    WIN32_FIND_DATAW found{};
    HANDLE search = FindFirstFileW((local + L"\\*").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) {
        error = L"Не удалось прочитать локальную папку.";
        return false;
    }
    do {
        const std::wstring name = found.cFileName;
        if (name == L"." || name == L"..") continue;
        const bool directory = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const std::wstring childLocal = local + L"\\" + name;
        const std::string childRemote = remote + "/" + Utf8(name);
        if (!Upload(childLocal, childRemote, directory, error, meter)) {
            FindClose(search);
            return false;
        }
    } while (FindNextFileW(search, &found));
    FindClose(search);
    return true;
}
