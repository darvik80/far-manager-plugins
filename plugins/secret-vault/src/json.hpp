#ifndef SECRET_VAULT_JSON_HPP_
#define SECRET_VAULT_JSON_HPP_

#include "vault.hpp"

#include <string>

std::string VaultToJson(const VaultData& data);
bool JsonToVault(const std::string& json, VaultData& data, std::wstring& error);

#endif
