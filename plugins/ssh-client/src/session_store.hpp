#ifndef SSH_CLIENT_SESSION_STORE_HPP_
#define SSH_CLIENT_SESSION_STORE_HPP_

#include <string>
#include <vector>

struct StoredServer {
    std::wstring name;
    std::wstring host;
    std::wstring port = L"22";
    std::wstring user;
    std::wstring keyFile;
    std::wstring proxy;
    std::wstring password;
    bool fromConfig = false;
    std::wstring keyLabel;
    std::vector<std::wstring> identityFiles;
};

std::wstring ServersPath();
std::vector<StoredServer> LoadServers();
bool SaveServers(const std::vector<StoredServer>& servers);
std::vector<StoredServer> CombinedSites();

#endif
