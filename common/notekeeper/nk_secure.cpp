#include "nk_secure.hpp"
#include "nk_text.hpp"

#include <windows.h>
#include <wincrypt.h>
#include <aclapi.h>

#include <vector>

namespace {

PTOKEN_USER CurrentUser(std::vector<BYTE>& buffer) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return nullptr;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    buffer.resize(size);
    const BOOL ok = GetTokenInformation(token, TokenUser, buffer.data(), size, &size);
    CloseHandle(token);
    if (!ok || buffer.empty()) return nullptr;
    return reinterpret_cast<PTOKEN_USER>(buffer.data());
}

std::wstring HostOf(std::wstring url) {
    url = NkLower(url);
    const auto scheme = url.find(L"://");
    if (scheme == std::wstring::npos) return {};
    std::wstring host = url.substr(scheme + 3);
    const auto at = host.find(L'@');
    if (at != std::wstring::npos) host = host.substr(at + 1);
    const auto slash = host.find_first_of(L"/\\");
    if (slash != std::wstring::npos) host.resize(slash);
    if (!host.empty() && host.front() == L'[') {
        const auto rb = host.find(L']');
        host = (rb == std::wstring::npos) ? host : host.substr(1, rb - 1);
    } else {
        const auto first = host.find(L':');
        const auto last = host.rfind(L':');
        if (first != std::wstring::npos && first == last) host.resize(first);
    }
    return host;
}

}  // namespace

bool NkUrlIsSecure(const std::wstring& url) {
    std::wstring full = url;
    while (!full.empty() && (full.front() == L' ' || full.front() == L'\t')) full.erase(full.begin());
    if (full.find(L"://") == std::wstring::npos) full = L"https://" + full;
    const std::wstring lower = NkLower(full);
    if (lower.compare(0, 8, L"https://") == 0) return true;
    if (lower.compare(0, 7, L"http://") != 0) return false;
    const std::wstring host = HostOf(full);
    return host == L"127.0.0.1" || host == L"localhost" || host == L"::1";
}

std::wstring NkProtectSecret(const std::wstring& plain) {
    if (plain.empty()) return {};
    if (plain.compare(0, 6, L"dpapi:") == 0) return plain;
    const std::string utf8 = NkUtf8(plain);
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(utf8.data()));
    in.cbData = static_cast<DWORD>(utf8.size());
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"NoteKeeper PAT", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};
    DWORD chars = 0;
    CryptBinaryToStringW(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &chars);
    std::wstring b64(chars > 0 ? chars : 0, L'\0');
    if (chars > 0)
        CryptBinaryToStringW(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64.data(), &chars);
    LocalFree(out.pbData);
    while (!b64.empty() && (b64.back() == L'\0' || b64.back() == L'\r' || b64.back() == L'\n')) b64.pop_back();
    return L"dpapi:" + b64;
}

std::wstring NkUnprotectSecret(const std::wstring& stored) {
    if (stored.empty()) return {};
    if (stored.compare(0, 6, L"dpapi:") != 0) return stored;
    const std::wstring b64 = stored.substr(6);
    DWORD bytes = 0;
    if (!CryptStringToBinaryW(b64.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &bytes, nullptr, nullptr) || bytes == 0)
        return {};
    std::vector<BYTE> blob(bytes);
    if (!CryptStringToBinaryW(b64.c_str(), 0, CRYPT_STRING_BASE64, blob.data(), &bytes, nullptr, nullptr))
        return {};
    DATA_BLOB in{};
    in.pbData = blob.data();
    in.cbData = bytes;
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};
    const std::string utf8(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return NkWide(utf8);
}

bool NkRestrictToCurrentUser(const std::wstring& path) {
    if (path.empty()) return false;
    std::vector<BYTE> sid;
    PTOKEN_USER user = CurrentUser(sid);
    if (!user) return false;
    EXPLICIT_ACCESS_W access{};
    access.grfAccessPermissions = GENERIC_ALL;
    access.grfAccessMode = SET_ACCESS;
    access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_USER;
    access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(user->User.Sid);
    PACL acl = nullptr;
    if (SetEntriesInAclW(1, &access, nullptr, &acl) != ERROR_SUCCESS) return false;
    const DWORD err = SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
                                            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                            nullptr, nullptr, acl, nullptr);
    if (err == ERROR_SUCCESS) {
        SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                              user->User.Sid, nullptr, nullptr, nullptr);
    }
    LocalFree(acl);
    return err == ERROR_SUCCESS;
}

void NkNormalizePem(std::string& pem) {
    if (pem.size() >= 3 &&
        static_cast<unsigned char>(pem[0]) == 0xEF &&
        static_cast<unsigned char>(pem[1]) == 0xBB &&
        static_cast<unsigned char>(pem[2]) == 0xBF) {
        pem.erase(0, 3);
    }
    std::string out;
    out.reserve(pem.size() + 8);
    for (char ch : pem) {
        if (ch != '\r') out.push_back(ch);
    }
    if (out.find("BEGIN") != std::string::npos && out.find('\n') == std::string::npos) {
        std::string unesc;
        unesc.reserve(out.size());
        for (size_t i = 0; i < out.size(); ++i) {
            if (out[i] == '\\' && i + 1 < out.size()) {
                if (out[i + 1] == 'n') { unesc.push_back('\n'); ++i; continue; }
                if (out[i + 1] == 'r') { ++i; continue; }
            }
            unesc.push_back(out[i]);
        }
        out.swap(unesc);
    }
    const auto begin = out.find("-----BEGIN ");
    const auto endMark = out.find("-----END ");
    if (begin != std::string::npos && endMark != std::string::npos && endMark > begin) {
        auto headerEnd = out.find("-----", begin + 5);
        auto footerEnd = out.find("-----", endMark + 5);
        if (headerEnd != std::string::npos && footerEnd != std::string::npos) {
            headerEnd += 5;
            footerEnd += 5;
            const bool oneLine = out.find('\n', begin) > endMark || out.find('\n', begin) == std::string::npos;
            if (oneLine) {
                std::string header = out.substr(begin, headerEnd - begin);
                std::string footer = out.substr(endMark, footerEnd - endMark);
                std::string b64;
                for (size_t i = headerEnd; i < endMark; ++i) {
                    const unsigned char ch = static_cast<unsigned char>(out[i]);
                    if (ch != ' ' && ch != '\n' && ch != '\t') b64.push_back(out[i]);
                }
                if (!b64.empty()) {
                    std::string wrapped = header;
                    wrapped += '\n';
                    for (size_t i = 0; i < b64.size(); i += 70) {
                        wrapped.append(b64, i, b64.size() - i < 70 ? b64.size() - i : 70);
                        wrapped += '\n';
                    }
                    wrapped += footer;
                    wrapped += '\n';
                    out.swap(wrapped);
                }
            }
        }
    }
    if (!out.empty() && out.back() != '\n') out += '\n';
    pem.swap(out);
}

std::wstring NkSecureTempDir(const wchar_t* leaf) {
    wchar_t app[MAX_PATH]{};
    if (!GetEnvironmentVariableW(L"APPDATA", app, MAX_PATH) || !app[0]) return {};
    std::wstring dir = app;
    if (dir.back() != L'\\') dir += L'\\';
    dir += L"Far Manager";
    CreateDirectoryW(dir.c_str(), nullptr);
    dir += L'\\';
    dir += (leaf && *leaf) ? leaf : L"FarNoteKeeper";
    CreateDirectoryW(dir.c_str(), nullptr);
    NkRestrictToCurrentUser(dir);
    return dir;
}

std::wstring NkSecureTempPath(const wchar_t* leaf, const wchar_t* prefix, const wchar_t* ext,
                              std::wstring& error) {
    const std::wstring dir = NkSecureTempDir(leaf);
    if (dir.empty()) {
        error = L"Нет временного каталога.";
        return {};
    }
    wchar_t path[MAX_PATH]{};
    if (!GetTempFileNameW(dir.c_str(), prefix && *prefix ? prefix : L"nk", 0, path)) {
        error = L"Не удалось создать временный файл.";
        return {};
    }
    std::wstring out = path;
    NkRestrictToCurrentUser(out);
    if (ext && *ext) {
        const std::wstring named = out + ext;
        if (!MoveFileExW(out.c_str(), named.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(out.c_str());
            error = L"Не удалось создать временный файл.";
            return {};
        }
        NkRestrictToCurrentUser(named);
        return named;
    }
    return out;
}

bool NkWriteSecureFile(const std::wstring& path, const void* data, size_t size) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = (size == 0) || WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || written != size) {
        DeleteFileW(path.c_str());
        return false;
    }
    NkRestrictToCurrentUser(path);
    return true;
}

void NkWipeTempPrefix(const wchar_t* leaf, const wchar_t* prefix) {
    const std::wstring dir = NkSecureTempDir(leaf);
    if (dir.empty()) return;
    std::wstring pattern = dir + L'\\';
    pattern += (prefix && *prefix) ? prefix : L"";
    pattern += L'*';
    WIN32_FIND_DATAW find{};
    HANDLE handle = FindFirstFileW(pattern.c_str(), &find);
    if (handle == INVALID_HANDLE_VALUE) return;
    do {
        if (find.cFileName[0] == L'.') continue;
        if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        DeleteFileW((dir + L'\\' + find.cFileName).c_str());
    } while (FindNextFileW(handle, &find));
    FindClose(handle);
}
