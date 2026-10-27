#ifndef SECRET_VAULT_VAULT_HPP_
#define SECRET_VAULT_VAULT_HPP_

#include <string>
#include <vector>

struct PasswordRecord {
    std::wstring id;
    std::wstring name;
    std::wstring user;
    std::wstring secret;
    std::wstring host;
    std::wstring note;
    int revision = 0;
};

struct KeyRecord {
    std::wstring id;
    std::wstring name;
    std::wstring type;
    std::wstring comment;
    std::wstring privateKey;
    std::wstring publicKey;
    int revision = 0;
};

struct VaultData {
    std::vector<PasswordRecord> passwords;
    std::vector<KeyRecord> keys;
};

std::wstring VaultPath();
std::wstring NewRecordId();
bool LoadVault(VaultData& data, std::wstring& error);
bool SaveVault(const VaultData& data, std::wstring& error);
bool ExportVault(const VaultData& data, const std::wstring& path, const std::wstring& password, std::wstring& error);
bool ImportVaultFile(VaultData& data, const std::wstring& path, const std::wstring& password, std::wstring& error);

PasswordRecord* FindPassword(VaultData& data, const std::wstring& name);
const PasswordRecord* FindPassword(const VaultData& data, const std::wstring& name);
KeyRecord* FindKey(VaultData& data, const std::wstring& name);
const KeyRecord* FindKey(const VaultData& data, const std::wstring& name);
bool NameTaken(const VaultData& data, bool keys, const std::wstring& name, const std::wstring& exceptId);

std::wstring WriteTempKey(const KeyRecord& key, std::wstring& error);

#endif
