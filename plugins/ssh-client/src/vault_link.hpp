#ifndef SSH_CLIENT_VAULT_LINK_HPP_
#define SSH_CLIENT_VAULT_LINK_HPP_

#include <string>
#include <vector>

bool VaultPluginPresent();
std::wstring VaultCall(const std::wstring& verb, const std::wstring& kind, const std::wstring& name, const std::wstring& secret = {});
std::vector<std::wstring> VaultNames(bool keys);
std::wstring VaultLookupPassword(const std::wstring& name);
std::wstring VaultLookupPasswordForHost(const std::wstring& host, const std::wstring& user);
std::wstring VaultLookupKeyFile(const std::wstring& name);
std::wstring VaultLookupKeyPem(const std::wstring& name);
std::wstring VaultLookupKeyPub(const std::wstring& name);
bool VaultStorePassword(const std::wstring& name, const std::wstring& secret);
bool VaultStoreKeyFile(const std::wstring& name, const std::wstring& path);

#endif
