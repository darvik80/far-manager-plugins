#ifndef SECRET_VAULT_SYNC_HPP_
#define SECRET_VAULT_SYNC_HPP_

#include "vault.hpp"

struct SyncSettings {
    std::wstring baseUrl;
    std::wstring token;
    std::wstring lastSync;
};

std::wstring SyncSettingsPath();
bool LoadSyncSettings(SyncSettings& settings);
bool SaveSyncSettings(const SyncSettings& settings);
bool SyncConfigured(const SyncSettings& settings);

bool PullVault(VaultData& data, SyncSettings& settings, std::wstring& error);
bool PushVault(VaultData& data, const SyncSettings& settings, bool overwrite, std::wstring& error);
bool SyncVault(VaultData& data, SyncSettings& settings, bool overwrite, std::wstring& error);

#endif
