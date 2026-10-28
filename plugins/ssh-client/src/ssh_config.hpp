#ifndef SSH_CLIENT_CONFIG_HPP_
#define SSH_CLIENT_CONFIG_HPP_

#include <string>
#include <vector>

struct SshHost {
    std::wstring alias;
    std::wstring user;
    std::wstring hostName;
    std::wstring port;
    std::wstring proxyJump;
    std::wstring keyLabel;
    std::vector<std::wstring> identityFiles;
};

std::vector<std::wstring> DefaultIdentityFiles();

std::wstring SshDirectory();
std::wstring SshConfigPath();
bool FileExists(const std::wstring& path);
std::wstring ResolveIdentityPath(const std::wstring& spec);
std::wstring IdentityStore(const std::wstring& spec);
std::wstring IdentityLabel(const std::wstring& spec);

// Hosts in the order they appear in ~/.ssh/config. Wildcard blocks stay as defaults.
std::vector<SshHost> LoadSshHosts();

#endif
