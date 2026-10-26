#ifndef SECRET_VAULT_KEYS_HPP_
#define SECRET_VAULT_KEYS_HPP_

#include "vault.hpp"

bool GenerateKey(const std::wstring& type, const std::wstring& comment, KeyRecord& key, std::wstring& error);
bool ImportOpenSshKey(const std::wstring& path, const std::wstring& passphrase, KeyRecord& key, std::wstring& error);
bool IsOpenSshPrivateFile(const std::wstring& path);

#endif
