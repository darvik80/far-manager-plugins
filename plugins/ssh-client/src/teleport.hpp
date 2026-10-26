#ifndef SSH_CLIENT_TELEPORT_HPP_
#define SSH_CLIENT_TELEPORT_HPP_

#include "session_store.hpp"

#include <string>
#include <vector>

bool TshAvailable();
bool TshLoggedIn();
bool TshLogin(std::wstring& error);
std::wstring TshIdentityFile();
std::wstring TshCertificateFile();
std::wstring TshKnownHostsFile();
std::wstring TshExe();
std::wstring TshCluster();
std::wstring TshProxy();
std::vector<StoredServer> TshHosts();
void TshInvalidate();

#endif
