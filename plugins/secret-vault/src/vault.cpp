#include "vault.hpp"
#include "json.hpp"
#include "text.hpp"
#include "nk_secure.hpp"

#include <windows.h>
#include <wincrypt.h>
#include <objbase.h>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <fstream>
#include <vector>

namespace {

constexpr char kMagic[] = "FARVLT1";
constexpr uint8_t kKindDpapi = 0;
constexpr uint8_t kKindAes = 1;
constexpr int kPbkdf2Rounds = 200000;

std::wstring AppDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? buf : L".";
    dir += L"\\Far Manager\\SecretVault";
    CreateDirectoryW((dir.substr(0, dir.rfind(L'\\'))).c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

bool ReadAll(const std::wstring& path, std::string& bytes) {
    FILE* file = _wfopen(path.c_str(), L"rb");
    if (!file) return false;
    char buf[4096];
    size_t n = 0;
    bytes.clear();
    while ((n = fread(buf, 1, sizeof(buf), file)) > 0) bytes.append(buf, n);
    fclose(file);
    return true;
}

bool WriteAll(const std::wstring& path, const void* data, size_t size) {
    FILE* file = _wfopen(path.c_str(), L"wb");
    if (!file) return false;
    const bool ok = size == 0 || fwrite(data, 1, size, file) == size;
    fclose(file);
    return ok;
}

bool DpapiProtect(const std::string& plain, std::string& blob, std::wstring& error) {
    DATA_BLOB in{};
    in.cbData = static_cast<DWORD>(plain.size());
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()));
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"SecretVault", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        error = L"Не удалось зашифровать хранилище.";
        return false;
    }
    blob.assign(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return true;
}

bool DpapiUnprotect(const std::string& blob, std::string& plain, std::wstring& error) {
    DATA_BLOB in{};
    in.cbData = static_cast<DWORD>(blob.size());
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(blob.data()));
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        error = L"Не удалось расшифровать хранилище этой учётной записи Windows.";
        return false;
    }
    plain.assign(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return true;
}

bool AesKey(const std::wstring& password, const unsigned char* salt, unsigned char* key, std::wstring& error) {
    const std::string pass = Utf8(password);
    if (PKCS5_PBKDF2_HMAC(pass.c_str(), static_cast<int>(pass.size()), salt, 16, kPbkdf2Rounds, EVP_sha256(), 32, key) != 1) {
        error = L"Не удалось получить ключ экспорта.";
        return false;
    }
    return true;
}

bool AesEncrypt(const std::string& plain, const std::wstring& password, std::string& blob, std::wstring& error) {
    unsigned char salt[16]{};
    unsigned char nonce[12]{};
    unsigned char key[32]{};
    if (RAND_bytes(salt, sizeof(salt)) != 1 || RAND_bytes(nonce, sizeof(nonce)) != 1) {
        error = L"Нет случайных данных для экспорта.";
        return false;
    }
    if (!AesKey(password, salt, key, error)) return false;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        error = L"Не удалось создать шифр экспорта.";
        return false;
    }
    std::string out(plain.size() + 16, '\0');
    int len = 0;
    int total = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key, nonce) == 1;
    if (ok) ok = EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(out.data()), &len,
                                   reinterpret_cast<const unsigned char*>(plain.data()), static_cast<int>(plain.size())) == 1;
    total = len;
    if (ok) ok = EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(out.data()) + total, &len) == 1;
    total += len;
    unsigned char tag[16]{};
    if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        error = L"Не удалось зашифровать экспорт.";
        return false;
    }
    out.resize(static_cast<size_t>(total));
    blob.assign(reinterpret_cast<const char*>(salt), sizeof(salt));
    blob.append(reinterpret_cast<const char*>(nonce), sizeof(nonce));
    blob += out;
    blob.append(reinterpret_cast<const char*>(tag), sizeof(tag));
    return true;
}

bool AesDecrypt(const std::string& blob, const std::wstring& password, std::string& plain, std::wstring& error) {
    if (blob.size() < 16 + 12 + 16) {
        error = L"Файл экспорта повреждён.";
        return false;
    }
    const auto* salt = reinterpret_cast<const unsigned char*>(blob.data());
    const auto* nonce = salt + 16;
    const auto* tag = reinterpret_cast<const unsigned char*>(blob.data() + blob.size() - 16);
    const auto* data = salt + 28;
    const int dataLen = static_cast<int>(blob.size() - 44);
    unsigned char key[32]{};
    if (!AesKey(password, salt, key, error)) return false;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        error = L"Не удалось создать шифр импорта.";
        return false;
    }
    plain.assign(static_cast<size_t>(dataLen), '\0');
    int len = 0;
    int total = 0;
    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key, nonce) == 1;
    if (ok) ok = EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plain.data()), &len, data, dataLen) == 1;
    total = len;
    if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, const_cast<unsigned char*>(tag)) == 1;
    if (ok) ok = EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(plain.data()) + total, &len) == 1;
    total += len;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        error = L"Неверный пароль экспорта или файл повреждён.";
        plain.clear();
        return false;
    }
    plain.resize(static_cast<size_t>(total));
    return true;
}

std::string Wrap(uint8_t kind, const std::string& payload) {
    std::string out;
    out.append(kMagic, 7);
    out.push_back(1);
    out.push_back(static_cast<char>(kind));
    out += payload;
    return out;
}

bool Unwrap(const std::string& bytes, uint8_t& kind, std::string& payload, std::wstring& error) {
    if (bytes.size() < 9 || bytes.compare(0, 7, kMagic) != 0) {
        error = L"Это не файл хранилища.";
        return false;
    }
    kind = static_cast<uint8_t>(bytes[8]);
    payload = bytes.substr(9);
    return true;
}

void MergeById(VaultData& dest, const VaultData& extra) {
    auto mergePass = [&](const PasswordRecord& item) {
        for (auto& have : dest.passwords)
            if (have.id == item.id) {
                have = item;
                return;
            }
        if (NameTaken(dest, false, item.name, item.id)) return;
        dest.passwords.push_back(item);
    };
    auto mergeKey = [&](const KeyRecord& item) {
        for (auto& have : dest.keys)
            if (have.id == item.id) {
                have = item;
                return;
            }
        if (NameTaken(dest, true, item.name, item.id)) return;
        dest.keys.push_back(item);
    };
    for (const auto& item : extra.passwords) mergePass(item);
    for (const auto& item : extra.keys) mergeKey(item);
}

}  // namespace

std::wstring VaultPath() {
    return AppDir() + L"\\vault.bin";
}

std::wstring NewRecordId() {
    GUID guid{};
    wchar_t text[40];
    if (FAILED(CoCreateGuid(&guid))) {
        static unsigned counter;
        swprintf_s(text, L"%08X-%04X-4%03X-8%03X-%012X", GetTickCount(), GetCurrentProcessId() & 0xFFFF, ++counter & 0xFFF, rand() & 0xFFF, GetTickCount());
        return text;
    }
    swprintf_s(text, L"%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
               guid.Data1, guid.Data2, guid.Data3,
               guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
               guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return text;
}

bool LoadVault(VaultData& data, std::wstring& error) {
    data = {};
    std::string bytes;
    if (!ReadAll(VaultPath(), bytes)) return true;
    uint8_t kind = 0;
    std::string payload;
    if (!Unwrap(bytes, kind, payload, error)) return false;
    std::string json;
    if (kind == kKindDpapi) {
        if (!DpapiUnprotect(payload, json, error)) return false;
    } else {
        error = L"Локальный файл должен быть защищён Windows, а не паролем экспорта.";
        return false;
    }
    return JsonToVault(json, data, error);
}

bool SaveVault(const VaultData& data, std::wstring& error) {
    const std::string json = VaultToJson(data);
    std::string blob;
    if (!DpapiProtect(json, blob, error)) return false;
    const std::string wrapped = Wrap(kKindDpapi, blob);
    if (!WriteAll(VaultPath(), wrapped.data(), wrapped.size())) {
        error = L"Не удалось записать хранилище.";
        return false;
    }
    return true;
}

bool ExportVault(const VaultData& data, const std::wstring& path, const std::wstring& password, std::wstring& error) {
    if (password.empty()) {
        error = L"Нужен пароль экспорта.";
        return false;
    }
    std::string blob;
    if (!AesEncrypt(VaultToJson(data), password, blob, error)) return false;
    const std::string wrapped = Wrap(kKindAes, blob);
    if (!WriteAll(path, wrapped.data(), wrapped.size())) {
        error = L"Не удалось записать файл экспорта.";
        return false;
    }
    return true;
}

bool ImportVaultFile(VaultData& data, const std::wstring& path, const std::wstring& password, std::wstring& error) {
    std::string bytes;
    if (!ReadAll(path, bytes)) {
        error = L"Не удалось прочитать файл импорта.";
        return false;
    }
    uint8_t kind = 0;
    std::string payload;
    if (!Unwrap(bytes, kind, payload, error)) return false;
    std::string json;
    if (kind == kKindAes) {
        if (password.empty()) {
            error = L"Нужен пароль файла.";
            return false;
        }
        if (!AesDecrypt(payload, password, json, error)) return false;
    } else if (kind == kKindDpapi) {
        if (!DpapiUnprotect(payload, json, error)) return false;
    } else {
        error = L"Неизвестный формат хранилища.";
        return false;
    }
    VaultData extra;
    if (!JsonToVault(json, extra, error)) return false;
    MergeById(data, extra);
    return true;
}

PasswordRecord* FindPassword(VaultData& data, const std::wstring& name) {
    const std::wstring key = Lower(name);
    for (auto& item : data.passwords)
        if (Lower(item.name) == key) return &item;
    return nullptr;
}

const PasswordRecord* FindPassword(const VaultData& data, const std::wstring& name) {
    return FindPassword(const_cast<VaultData&>(data), name);
}

KeyRecord* FindKey(VaultData& data, const std::wstring& name) {
    const std::wstring key = Lower(name);
    for (auto& item : data.keys)
        if (Lower(item.name) == key) return &item;
    return nullptr;
}

const KeyRecord* FindKey(const VaultData& data, const std::wstring& name) {
    return FindKey(const_cast<VaultData&>(data), name);
}

bool NameTaken(const VaultData& data, bool keys, const std::wstring& name, const std::wstring& exceptId) {
    const std::wstring key = Lower(name);
    if (keys) {
        for (const auto& item : data.keys)
            if (Lower(item.name) == key && item.id != exceptId) return true;
    } else {
        for (const auto& item : data.passwords)
            if (Lower(item.name) == key && item.id != exceptId) return true;
    }
    return false;
}

std::wstring WriteTempKey(const KeyRecord& key, std::wstring& error) {
    const std::wstring path = NkSecureTempPath(L"FarVault", L"key", L".pem", error);
    if (path.empty()) return {};
    const std::string pem = Utf8(key.privateKey);
    if (!NkWriteSecureFile(path, pem.data(), pem.size())) {
        error = L"Не удалось записать временный ключ.";
        DeleteFileW(path.c_str());
        return {};
    }
    return path;
}
