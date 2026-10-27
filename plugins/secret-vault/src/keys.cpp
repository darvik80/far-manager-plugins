#include "keys.hpp"
#include "text.hpp"
#include "nk_secure.hpp"

#include <windows.h>

#include <fstream>
#include <vector>

namespace {

std::wstring FindSshKeygen() {
    wchar_t windir[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"SystemRoot", windir, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring bundled = std::wstring(windir) + L"\\System32\\OpenSSH\\ssh-keygen.exe";
        if (GetFileAttributesW(bundled.c_str()) != INVALID_FILE_ATTRIBUTES) return bundled;
    }
    wchar_t found[MAX_PATH];
    if (SearchPathW(nullptr, L"ssh-keygen.exe", nullptr, MAX_PATH, found, nullptr)) return found;
    return {};
}

std::wstring Quote(const std::wstring& text) {
    std::wstring out = L"\"";
    for (wchar_t ch : text) {
        if (ch == L'"') out += L'\\';
        out += ch;
    }
    out += L'"';
    return out;
}

bool ReadFileText(const std::wstring& path, std::wstring& text) {
    FILE* file = _wfopen(path.c_str(), L"rb");
    if (!file) return false;
    std::string bytes;
    char buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), file)) > 0) bytes.append(buf, n);
    fclose(file);
    while (!bytes.empty() && (bytes.back() == '\n' || bytes.back() == '\r')) bytes.pop_back();
    text = Wide(bytes);
    return !text.empty();
}

bool RunCapture(const std::wstring& command, std::string& out, std::wstring& error) {
    out.clear();
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &security, 0)) {
        error = L"Не удалось создать канал для ssh-keygen.";
        return false;
    }
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nul ? nul : nullptr;
    si.hStdOutput = write;
    si.hStdError = write;
    PROCESS_INFORMATION pi{};
    std::wstring line = command;
    const BOOL started = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(write);
    if (nul) CloseHandle(nul);
    if (!started) {
        CloseHandle(read);
        error = L"Не удалось запустить ssh-keygen.";
        return false;
    }
    CloseHandle(pi.hThread);
    const DWORD wait = WaitForSingleObject(pi.hProcess, 8000);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
    }
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(read, buf, sizeof(buf), &n, nullptr) && n > 0) out.append(buf, n);
    CloseHandle(read);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (wait != WAIT_OBJECT_0) {
        error = L"ssh-keygen не ответил. Ключ, скорее всего, закрыт паролем.";
        return false;
    }
    if (code != 0) {
        error = L"ssh-keygen завершился с ошибкой.";
        return false;
    }
    return true;
}

std::wstring TempKeyPath() {
    std::wstring error;
    std::wstring path = NkSecureTempPath(L"FarVault", L"svk", nullptr, error);
    if (path.empty()) return {};
    DeleteFileW(path.c_str());
    return path;
}

void Wipe(const std::wstring& path) {
    DeleteFileW(path.c_str());
    DeleteFileW((path + L".pub").c_str());
}

}  // namespace

bool GenerateKey(const std::wstring& type, const std::wstring& comment, KeyRecord& key, std::wstring& error) {
    const std::wstring exe = FindSshKeygen();
    if (exe.empty()) {
        error = L"Нужен OpenSSH ssh-keygen.";
        return false;
    }
    const std::wstring kind = Lower(type) == L"rsa" ? L"rsa" : L"ed25519";
    const std::wstring path = TempKeyPath();
    std::wstring command = Quote(exe) + L" -q -t " + kind;
    if (kind == L"rsa") command += L" -b 3072";
    command += L" -N \"\" -f " + Quote(path);
    if (!comment.empty()) command += L" -C " + Quote(comment);
    std::string ignored;
    if (!RunCapture(command, ignored, error)) {
        Wipe(path);
        return false;
    }
    if (!ReadFileText(path, key.privateKey) || !ReadFileText(path + L".pub", key.publicKey)) {
        Wipe(path);
        error = L"Не удалось прочитать созданный ключ.";
        return false;
    }
    Wipe(path);
    key.type = kind;
    key.comment = comment;
    return true;
}

bool IsOpenSshPrivateFile(const std::wstring& path) {
    std::wstring text;
    if (!ReadFileText(path, text)) return false;
    const std::wstring low = Lower(text);
    return low.find(L"begin openssh private key") != std::wstring::npos ||
           low.find(L"begin rsa private key") != std::wstring::npos;
}

bool ImportOpenSshKey(const std::wstring& path, const std::wstring& passphrase, KeyRecord& key, std::wstring& error) {
    if (!ReadFileText(path, key.privateKey)) {
        error = L"Не удалось прочитать файл ключа.";
        return false;
    }
    const std::wstring low = Lower(key.privateKey);
    if (low.find(L"begin openssh private key") == std::wstring::npos &&
        low.find(L"begin rsa private key") == std::wstring::npos) {
        error = L"Нужен OpenSSH-ключ, не .ppk.";
        return false;
    }
    const std::wstring exe = FindSshKeygen();
    if (exe.empty()) {
        error = L"Нужен OpenSSH ssh-keygen, чтобы прочитать публичную часть.";
        return false;
    }
    const std::wstring command = Quote(exe) + L" -y -f " + Quote(path) + L" -P " + Quote(passphrase);
    std::string pub;
    if (!RunCapture(command, pub, error)) {
        if (passphrase.empty())
            error = L"Ключ закрыт паролем или файл не читается.";
        else
            error = L"Неверный пароль ключа.";
        return false;
    }
    while (!pub.empty() && (pub.back() == '\n' || pub.back() == '\r')) pub.pop_back();
    key.publicKey = Wide(pub);
    if (key.publicKey.empty()) {
        error = L"Не удалось получить публичный ключ.";
        return false;
    }
    key.type = key.publicKey.rfind(L"ssh-rsa", 0) == 0 ? L"rsa" : L"ed25519";
    const auto space = key.publicKey.find_last_of(L' ');
    if (space != std::wstring::npos && space + 1 < key.publicKey.size())
        key.comment = key.publicKey.substr(space + 1);
    return true;
}
