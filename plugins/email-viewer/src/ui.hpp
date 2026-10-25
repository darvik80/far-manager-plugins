#ifndef UI_HPP_
#define UI_HPP_

#include "far/plugin.hpp"
#include "imap_client.hpp"
#include <string>
#include <vector>

bool ShowSettingsDialog(const PluginStartupInfo& info, ImapAccount& account);
void ShowAccountList(const PluginStartupInfo& info, const std::vector<ImapAccount>& accounts, int& selectedIndex);
void ConfigureAccounts(const PluginStartupInfo& info, std::vector<ImapAccount>& accounts);
void ShowFolderList(const PluginStartupInfo& info, ImapClient& client, std::string& selectedFolder);
void ShowMessageList(const PluginStartupInfo& info, ImapClient& client, const std::string& folder);
void ShowMessageView(const PluginStartupInfo& info, const ImapMessageBody& msg);

#endif
